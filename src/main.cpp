#include "config/config_loader.h"
#include "feed/feed_handler.h"
#include "engine/processing_thread.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <csignal>
#include <atomic>
#include <iostream>

namespace {
    std::atomic<bool> g_running{true};

    void signal_handler(int signum) {
        spdlog::info("Received signal {}, shutting down...", signum);
        g_running.store(false, std::memory_order_relaxed);
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
    std::string config_path = "config/dev.yaml";
    if (argc > 1) {
        config_path = argv[1];
    }

    mde::config::AppConfig config;
    try {
        config = mde::config::load_config(config_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        return 1;
    }

    setup_logging(config.logging);

    spdlog::info("=== {} v0.1.0 ===", config.name);
    spdlog::info("Config loaded from: {}", config_path);
    {
        std::string symbols_str;
        for (size_t i = 0; i < config.feed.symbols.size(); ++i) {
            if (i > 0) symbols_str += ", ";
            symbols_str += config.feed.symbols[i];
        }
        spdlog::info("Symbols: {}", symbols_str);
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Pipeline: Feed → SPSC Queue → Processing Thread
    // Queue is heap-allocated (8192 * ~5KB = ~40MB, too large for stack)
    auto queue = std::make_unique<mde::feed::FeedQueue>();

    mde::feed::FeedHandler feed_handler(config.feed, *queue);
    feed_handler.start(g_running);

    mde::engine::ProcessingThread processor(*queue);
    processor.start(g_running);

    spdlog::info("Pipeline started: feed → queue → processor");

    // Main thread: periodic status reporting
    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (g_running.load(std::memory_order_relaxed)) {
            spdlog::info("Status | recv={} processed={} parse_err={} queue_full={} queue_depth={} | "
                         "latency(last): parse={}us queue={}us total={}us",
                feed_handler.message_count(),
                processor.processed_count(),
                feed_handler.parse_error_count(),
                feed_handler.queue_full_count(),
                queue->size_approx(),
                processor.last_parse_latency_us(),
                processor.last_queue_latency_us(),
                processor.last_total_latency_us());
        }
    }

    // Graceful shutdown: stop feed first, then let processor drain
    spdlog::info("Stopping feed handler...");
    feed_handler.stop();
    spdlog::info("Stopping processor...");
    processor.stop();
    spdlog::info("Engine stopped. Final: recv={} processed={} parse_err={} queue_full={}",
        feed_handler.message_count(),
        processor.processed_count(),
        feed_handler.parse_error_count(),
        feed_handler.queue_full_count());
    return 0;
}
