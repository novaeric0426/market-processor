#include "config/config_loader.h"
#include "feed/feed_handler.h"
#include "engine/processing_thread.h"
#include "engine/signal_detector.h"
#include "output/disk_logger.h"
#include "output/metrics_server.h"
#include "output/ws_server.h"
#include "replay/replay_engine.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <nlohmann/json.hpp>

#include <csignal>
#include <atomic>
#include <iostream>
#include <cstring>
#include <chrono>

namespace {
    std::atomic<bool> g_running{true};
    auto g_start_time = std::chrono::steady_clock::now();

    void signal_handler(int signum) {
        spdlog::info("Received signal {}, shutting down...", signum);
        g_running.store(false, std::memory_order_relaxed);
    }

    struct CliArgs {
        std::string config_path = "config/dev.yaml";
        std::string replay_path;
        mde::replay::PlaybackSpeed speed = mde::replay::PlaybackSpeed::REALTIME;
    };

    CliArgs parse_args(int argc, char* argv[]) {
        CliArgs args;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
                args.replay_path = argv[++i];
            } else if (std::strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
                ++i;
                if (std::strcmp(argv[i], "1x") == 0 || std::strcmp(argv[i], "1") == 0)
                    args.speed = mde::replay::PlaybackSpeed::REALTIME;
                else if (std::strcmp(argv[i], "2x") == 0 || std::strcmp(argv[i], "2") == 0)
                    args.speed = mde::replay::PlaybackSpeed::FAST_2X;
                else if (std::strcmp(argv[i], "5x") == 0 || std::strcmp(argv[i], "5") == 0)
                    args.speed = mde::replay::PlaybackSpeed::FAST_5X;
                else if (std::strcmp(argv[i], "max") == 0 || std::strcmp(argv[i], "0") == 0)
                    args.speed = mde::replay::PlaybackSpeed::MAX;
                else
                    spdlog::warn("Unknown speed '{}', using 1x", argv[i]);
            } else {
                args.config_path = argv[i];
            }
        }
        return args;
    }

    // Build JSON snapshot for /stats and WebSocket push.
    std::string build_stats_json(
        const mde::engine::ProcessingThread& processor,
        const mde::feed::FeedHandler* feed,
        const mde::replay::ReplayEngine* replay,
        const mde::feed::FeedQueue& queue,
        const mde::output::DiskLogger* recorder)
    {
        auto snapshot = processor.take_snapshot();
        auto uptime_s = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - g_start_time).count();

        nlohmann::json j;
        j["uptime_s"] = uptime_s;
        j["mode"] = replay ? "replay" : "live";
        j["processed"] = snapshot.processed_count;
        j["trades_processed"] = snapshot.trades_processed;
        j["signals_fired"] = snapshot.signals_fired;
        j["queue_depth"] = queue.size_approx();

        // Latency
        j["latency"]["parse_us"] = snapshot.last_parse_us;
        j["latency"]["queue_us"] = snapshot.last_queue_us;
        j["latency"]["total_us"] = snapshot.last_total_us;

        // Feed metrics
        if (feed) {
            j["feed"]["received"] = feed->message_count();
            j["feed"]["parse_errors"] = feed->parse_error_count();
            j["feed"]["queue_full"] = feed->queue_full_count();
        }

        // Replay metrics
        if (replay) {
            j["replay"]["replayed"] = replay->replayed_count();
            j["replay"]["total"] = replay->total_records();
            j["replay"]["finished"] = replay->is_finished();
            j["replay"]["paused"] = replay->is_paused();
            auto spd = replay->speed();
            const char* speed_str = "1x";
            if (spd == mde::replay::PlaybackSpeed::FAST_2X) speed_str = "2x";
            else if (spd == mde::replay::PlaybackSpeed::FAST_5X) speed_str = "5x";
            else if (spd == mde::replay::PlaybackSpeed::MAX) speed_str = "max";
            j["replay"]["speed"] = speed_str;
        }

        // Recording
        if (recorder) {
            j["recording"]["records"] = recorder->record_count();
        }

        // Recent signals
        j["recent_signals"] = nlohmann::json::array();
        for (const auto& sig : snapshot.recent_signals) {
            j["recent_signals"].push_back({
                {"type", mde::engine::signal_type_name(sig.type)},
                {"symbol", sig.symbol},
                {"value", sig.value},
                {"threshold", sig.threshold},
                {"timestamp_us", sig.timestamp_us}
            });
        }

        // Per-symbol order book + aggregation
        j["symbols"] = nlohmann::json::array();
        for (const auto& sym : snapshot.symbols) {
            nlohmann::json sj;
            sj["symbol"] = sym.symbol;
            sj["best_bid"] = sym.best_bid;
            sj["best_ask"] = sym.best_ask;
            sj["spread"] = sym.spread;
            sj["mid_price"] = sym.mid_price;
            sj["update_count"] = sym.update_count;
            sj["aggregation"]["bid_vwap"] = sym.aggregation.bid_vwap;
            sj["aggregation"]["ask_vwap"] = sym.aggregation.ask_vwap;
            sj["aggregation"]["mid_price_sma"] = sym.aggregation.mid_price_sma;
            sj["aggregation"]["spread_sma"] = sym.aggregation.spread_sma;
            sj["aggregation"]["bid_ask_imbalance"] = sym.aggregation.bid_ask_imbalance;

            // Order book depth (top 20 levels each side for dashboard)
            nlohmann::json bids_arr = nlohmann::json::array();
            int count = 0;
            for (const auto& [price, qty] : sym.bids) {
                if (++count > 20) break;
                bids_arr.push_back({{"price", price}, {"qty", qty}});
            }
            sj["bids"] = bids_arr;

            nlohmann::json asks_arr = nlohmann::json::array();
            count = 0;
            for (const auto& [price, qty] : sym.asks) {
                if (++count > 20) break;
                asks_arr.push_back({{"price", price}, {"qty", qty}});
            }
            sj["asks"] = asks_arr;

            j["symbols"].push_back(sj);
        }

        return j.dump();
    }
}

void setup_logging(const mde::config::LoggingConfig& log_cfg) {
    std::vector<spdlog::sink_ptr> sinks;

    if (log_cfg.console) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }
    if (log_cfg.file.enabled) {
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_cfg.file.path,
            log_cfg.file.max_size_mb * 1024 * 1024,
            log_cfg.file.max_files
        ));
    }

    auto logger = std::make_shared<spdlog::logger>("mde", sinks.begin(), sinks.end());

    if (log_cfg.level == "trace")         logger->set_level(spdlog::level::trace);
    else if (log_cfg.level == "debug")    logger->set_level(spdlog::level::debug);
    else if (log_cfg.level == "info")     logger->set_level(spdlog::level::info);
    else if (log_cfg.level == "warn")     logger->set_level(spdlog::level::warn);
    else if (log_cfg.level == "error")    logger->set_level(spdlog::level::err);
    else if (log_cfg.level == "critical") logger->set_level(spdlog::level::critical);

    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
}

int main(int argc, char* argv[]) {
    auto cli = parse_args(argc, argv);
    bool replay_mode = !cli.replay_path.empty();

    mde::config::AppConfig config;
    try {
        config = mde::config::load_config(cli.config_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        return 1;
    }

    setup_logging(config.logging);
    g_start_time = std::chrono::steady_clock::now();

    spdlog::info("=== {} v0.1.0 ===", config.name);
    spdlog::info("Config loaded from: {}", cli.config_path);
    spdlog::info("Mode: {}", replay_mode ? "REPLAY" : "LIVE");

    if (!replay_mode) {
        std::string symbols_str;
        for (size_t i = 0; i < config.feed.symbols.size(); ++i) {
            if (i > 0) symbols_str += ", ";
            symbols_str += config.feed.symbols[i];
        }
        spdlog::info("Symbols: {}", symbols_str);
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Pipeline: Feed/Replay → SPSC Queue → Processing Thread
    auto queue = std::make_unique<mde::feed::FeedQueue>();

    // Signal detection conditions
    std::vector<mde::engine::SignalCondition> signals = {
        {mde::engine::SignalType::SPREAD_WIDE, 50.0},
        {mde::engine::SignalType::IMBALANCE_BID, 0.7},
        {mde::engine::SignalType::IMBALANCE_ASK, 0.7},
        {mde::engine::SignalType::PRICE_DEVIATION, 0.001},
        {mde::engine::SignalType::TRADE_IMBALANCE_BUY, 0.8, 3'000'000},
        {mde::engine::SignalType::TRADE_IMBALANCE_SELL, 0.8, 3'000'000},
        {mde::engine::SignalType::VOLUME_SPIKE, 3.0},       // 3x SMA
        {mde::engine::SignalType::BOOK_PRESSURE_BID, 0.15},
        {mde::engine::SignalType::BOOK_PRESSURE_ASK, 0.15},
    };

    // --- Recording (live mode only) ---
    std::unique_ptr<mde::output::DiskLogger> disk_logger;
    if (!replay_mode && config.recording.enabled) {
        disk_logger = std::make_unique<mde::output::DiskLogger>();
        if (!disk_logger->open_with_timestamp(config.recording.output_dir)) {
            spdlog::error("Failed to open recording file, continuing without recording");
            disk_logger.reset();
        }
    }

    mde::engine::ProcessingThread processor(*queue, std::move(signals),
                                            disk_logger ? disk_logger.get() : nullptr);

    // --- Data source ---
    std::unique_ptr<mde::feed::FeedHandler> feed_handler;
    std::unique_ptr<mde::replay::ReplayEngine> replay_engine;

    if (replay_mode) {
        replay_engine = std::make_unique<mde::replay::ReplayEngine>(*queue);
        if (!replay_engine->load(cli.replay_path)) {
            spdlog::error("Failed to load replay file: {}", cli.replay_path);
            return 1;
        }
        replay_engine->set_speed(cli.speed);
        replay_engine->start(g_running);
    } else {
        feed_handler = std::make_unique<mde::feed::FeedHandler>(config.feed, *queue);
        feed_handler->start(g_running);
    }

    processor.start(g_running);
    spdlog::info("Pipeline started");

    // --- Monitoring servers ---
    auto stats_fn = [&]() -> std::string {
        return build_stats_json(processor,
            feed_handler.get(), replay_engine.get(),
            *queue, disk_logger.get());
    };

    mde::output::MetricsServer metrics_server(config.server.metrics_port, stats_fn);
    metrics_server.start();

    // Replay control handler for WebSocket commands
    mde::output::ReplayController replay_ctrl;
    if (replay_engine) {
        replay_ctrl = [&](const std::string& msg) {
            try {
                auto j = nlohmann::json::parse(msg);
                auto cmd = j.value("cmd", "");
                if (cmd == "pause") replay_engine->pause();
                else if (cmd == "resume") replay_engine->resume();
                else if (cmd == "speed") {
                    auto val = j.value("value", "1x");
                    if (val == "1x") replay_engine->set_speed(mde::replay::PlaybackSpeed::REALTIME);
                    else if (val == "2x") replay_engine->set_speed(mde::replay::PlaybackSpeed::FAST_2X);
                    else if (val == "5x") replay_engine->set_speed(mde::replay::PlaybackSpeed::FAST_5X);
                    else if (val == "max") replay_engine->set_speed(mde::replay::PlaybackSpeed::MAX);
                }
            } catch (...) {
                spdlog::debug("WsServer: invalid command: {}", msg);
            }
        };
    }

    mde::output::WsServer ws_server(config.server.ws_port, stats_fn, replay_ctrl);
    ws_server.start();

    spdlog::info("Monitoring: HTTP :{} | WS :{}", config.server.metrics_port, config.server.ws_port);

    // Main thread: periodic status reporting
    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!g_running.load(std::memory_order_relaxed)) break;

        if (replay_mode) {
            spdlog::info("Status [REPLAY] | replayed={}/{} processed={} signals={} "
                         "queue_depth={} ws_clients={} finished={} | "
                         "latency(last): parse={}us queue={}us total={}us",
                replay_engine->replayed_count(),
                replay_engine->total_records(),
                processor.processed_count(),
                processor.signals_fired(),
                queue->size_approx(),
                ws_server.client_count(),
                replay_engine->is_finished() ? "yes" : "no",
                processor.last_parse_latency_us(),
                processor.last_queue_latency_us(),
                processor.last_total_latency_us());

            if (replay_engine->is_finished() && queue->empty()) {
                spdlog::info("Replay complete, shutting down");
                g_running.store(false, std::memory_order_relaxed);
            }
        } else {
            spdlog::info("Status | recv={} processed={} trades={} signals={} parse_err={} queue_full={} "
                         "queue_depth={} ws_clients={}{} | "
                         "latency(last): parse={}us queue={}us total={}us",
                feed_handler->message_count(),
                processor.processed_count(),
                processor.trades_processed(),
                processor.signals_fired(),
                feed_handler->parse_error_count(),
                feed_handler->queue_full_count(),
                queue->size_approx(),
                ws_server.client_count(),
                disk_logger ? " rec=" + std::to_string(disk_logger->record_count()) : "",
                processor.last_parse_latency_us(),
                processor.last_queue_latency_us(),
                processor.last_total_latency_us());
        }
    }

    // Graceful shutdown
    spdlog::info("Stopping monitoring servers...");
    ws_server.stop();
    metrics_server.stop();

    if (replay_mode) {
        spdlog::info("Stopping replay engine...");
        replay_engine->stop();
    } else {
        spdlog::info("Stopping feed handler...");
        feed_handler->stop();
    }

    spdlog::info("Stopping processor...");
    processor.stop();

    if (disk_logger) {
        disk_logger->close();
    }

    spdlog::info("Engine stopped. processed={} signals={}",
        processor.processed_count(), processor.signals_fired());
    return 0;
}
