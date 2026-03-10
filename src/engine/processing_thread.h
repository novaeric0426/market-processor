#pragma once

#include "feed/feed_handler.h"
#include "core/types.h"
#include "core/clock.h"

#include <atomic>
#include <thread>
#include <cstdint>

namespace mde::engine {

// Consumes messages from the SPSC queue on a dedicated thread.
// Phase 2: logs dequeue latency. Phase 3+: feeds into OrderBook/Aggregator.
class ProcessingThread {
public:
    explicit ProcessingThread(feed::FeedQueue& queue);
    ~ProcessingThread();

    void start(std::atomic<bool>& running);
    void stop();

    uint64_t processed_count() const { return processed_.load(std::memory_order_relaxed); }

    // Latency stats (microseconds)
    int64_t last_parse_latency_us() const { return last_parse_us_.load(std::memory_order_relaxed); }
    int64_t last_queue_latency_us() const { return last_queue_us_.load(std::memory_order_relaxed); }
    int64_t last_total_latency_us() const { return last_total_us_.load(std::memory_order_relaxed); }

private:
    void run(std::atomic<bool>& running);
    void process(core::QueueMessage& msg);

    feed::FeedQueue& queue_;
    std::thread thread_;
    std::atomic<uint64_t> processed_{0};
    std::atomic<int64_t> last_parse_us_{0};
    std::atomic<int64_t> last_queue_us_{0};
    std::atomic<int64_t> last_total_us_{0};
};

} // namespace mde::engine
