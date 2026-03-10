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

    // Parse JSON into DepthUpdate
    core::QueueMessage msg;
    msg.type = core::MessageType::DEPTH_UPDATE;
    msg.depth.ts_received = ts_received;

    if (!parser_.parse(std::string_view(data, len), msg.depth)) {
        parse_errors_.fetch_add(1, std::memory_order_relaxed);
        if (count <= 5) {
            spdlog::warn("[msg #{}] Parse failed ({} bytes)", count, len);
        }
        return;
    }

    auto ts_parsed = core::Clock::now();
    msg.depth.ts_parsed = ts_parsed;
    msg.depth.ts_enqueued = ts_parsed; // Best approximation before push

    // Capture values before move
    auto symbol = msg.depth.symbol;
    auto bid_count = msg.depth.bid_count;
    auto ask_count = msg.depth.ask_count;

    if (count == 1) {
        spdlog::info("First message parsed: {} bids={} asks={} ({} bytes)",
            symbol, bid_count, ask_count, len);
    }

    if (count <= 3) {
        auto parse_us = core::Clock::elapsed_us(ts_received, ts_parsed);
        spdlog::debug("[msg #{}] parse_latency={}us symbol={} bids={} asks={}",
            count, parse_us, symbol, bid_count, ask_count);
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
