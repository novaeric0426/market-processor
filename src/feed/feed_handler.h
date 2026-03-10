#pragma once

#include "config/config_loader.h"
#include "feed/ws_client.h"
#include "feed/depth_parser.h"
#include "core/types.h"
#include "core/spsc_queue.h"

#include <atomic>
#include <thread>
#include <memory>
#include <cstdint>

namespace mde::feed {

// Queue type: 8192 slots, power of two
using FeedQueue = core::SPSCQueue<core::QueueMessage, 8192>;

// Manages the feed thread lifecycle.
// Receives raw JSON → parses with simdjson → enqueues into SPSC queue.
class FeedHandler {
public:
    FeedHandler(const config::FeedConfig& feed_cfg, FeedQueue& queue);
    ~FeedHandler();

    // Start the feed thread. Non-blocking.
    void start(std::atomic<bool>& running);

    // Stop the feed thread and wait for it to join.
    void stop();

    uint64_t message_count() const { return msg_count_.load(std::memory_order_relaxed); }
    uint64_t parse_error_count() const { return parse_errors_.load(std::memory_order_relaxed); }
    uint64_t queue_full_count() const { return queue_full_.load(std::memory_order_relaxed); }

private:
    void on_message(const char* data, std::size_t len);

    config::FeedConfig feed_cfg_;
    FeedQueue& queue_;
    DepthParser parser_;
    std::unique_ptr<WsClient> ws_client_;
    std::thread feed_thread_;
    std::atomic<uint64_t> msg_count_{0};
    std::atomic<uint64_t> parse_errors_{0};
    std::atomic<uint64_t> queue_full_{0};
};

} // namespace mde::feed
