#include "feed/feed_handler.h"
#include "core/clock.h"
#include "core/thread_utils.h"

#include <spdlog/spdlog.h>

namespace mde::feed {

FeedHandler::FeedHandler(const config::FeedConfig& feed_cfg, FeedQueue& queue)
    : feed_cfg_(feed_cfg)
    , queue_(queue) {}

FeedHandler::~FeedHandler() {
    stop();
}

void FeedHandler::start(std::atomic<bool>& running) {
    ws_client_ = std::make_unique<WsClient>(
        feed_cfg_,
        [this](const char* data, std::size_t len) {
            on_message(data, len);
        }
    );

    feed_thread_ = std::thread([this, &running]() {
        core::set_thread_name("mde_feed");
        core::set_thread_affinity(0);  // Pin to CPU 0 (Linux only)
        spdlog::info("Feed thread started");
        ws_client_->run(running);
        spdlog::info("Feed thread exited");
    });
}

void FeedHandler::stop() {
    if (ws_client_) {
        ws_client_->stop();
    }
    if (feed_thread_.joinable()) {
        feed_thread_.join();
    }
}

void FeedHandler::on_message(const char* data, std::size_t len) {
    auto ts_received = core::Clock::now();
    auto count = msg_count_.fetch_add(1, std::memory_order_relaxed) + 1;

    std::string_view json(data, len);
    core::QueueMessage msg;

    // Detect message type from raw JSON to avoid double-parsing.
    // Trade messages contain "\"e\":\"trade\"", depth contains "\"e\":\"depthUpdate\"".
    if (json.find("\"trade\"") != std::string_view::npos) {
        msg.type = core::MessageType::TRADE;
        msg.trade.ts_received = ts_received;

        if (!trade_parser_.parse(json, msg.trade)) {
            parse_errors_.fetch_add(1, std::memory_order_relaxed);
            if (count <= 5) {
                spdlog::warn("[msg #{}] Trade parse failed ({} bytes)", count, len);
            }
            return;
        }

        auto ts_parsed = core::Clock::now();
        msg.trade.ts_parsed = ts_parsed;
        msg.trade.ts_enqueued = ts_parsed;

        if (count <= 3) {
            auto parse_us = core::Clock::elapsed_us(ts_received, ts_parsed);
            spdlog::debug("[msg #{}] trade {} price={:.2f} qty={:.4f} side={} parse={}us",
                count, msg.trade.symbol, msg.trade.price, msg.trade.quantity,
                msg.trade.is_buyer_maker ? "sell" : "buy", parse_us);
        }
    } else {
        msg.type = core::MessageType::DEPTH_UPDATE;
        msg.depth.ts_received = ts_received;

        if (!depth_parser_.parse(json, msg.depth)) {
            parse_errors_.fetch_add(1, std::memory_order_relaxed);
            if (count <= 5) {
                spdlog::warn("[msg #{}] Depth parse failed ({} bytes)", count, len);
            }
            return;
        }

        auto ts_parsed = core::Clock::now();
        msg.depth.ts_parsed = ts_parsed;
        msg.depth.ts_enqueued = ts_parsed;

        if (count == 1) {
            spdlog::info("First depth message: {} bids={} asks={} ({} bytes)",
                msg.depth.symbol, msg.depth.bid_count, msg.depth.ask_count, len);
        }

        if (count <= 3) {
            auto parse_us = core::Clock::elapsed_us(ts_received, ts_parsed);
            spdlog::debug("[msg #{}] depth {} bids={} asks={} parse={}us",
                count, msg.depth.symbol, msg.depth.bid_count, msg.depth.ask_count, parse_us);
        }
    }

    // Enqueue
    if (!queue_.try_push(std::move(msg))) {
        queue_full_.fetch_add(1, std::memory_order_relaxed);
        if (queue_full_.load(std::memory_order_relaxed) % 100 == 1) {
            spdlog::warn("Queue full, dropping message (total drops: {})",
                queue_full_.load(std::memory_order_relaxed));
        }
    }
}

} // namespace mde::feed
