#pragma once

#include "feed/feed_handler.h"
#include "engine/order_book.h"
#include "engine/aggregator.h"
#include "engine/signal_detector.h"
#include "output/disk_logger.h"
#include "core/types.h"
#include "core/clock.h"

#include <atomic>
#include <thread>
#include <cstdint>
#include <unordered_map>
#include <memory>

namespace mde::engine {

// Consumes messages from the SPSC queue on a dedicated thread.
// Pipeline: dequeue → record (optional) → order book update → aggregation → signal detection.
class ProcessingThread {
public:
    ProcessingThread(feed::FeedQueue& queue,
                     std::vector<SignalCondition> signal_conditions = {},
                     output::DiskLogger* recorder = nullptr);
    ~ProcessingThread();

    void start(std::atomic<bool>& running);
    void stop();

    uint64_t processed_count() const { return processed_.load(std::memory_order_relaxed); }
    uint64_t signals_fired() const { return signals_fired_.load(std::memory_order_relaxed); }

    // Latency stats (microseconds)
    int64_t last_parse_latency_us() const { return last_parse_us_.load(std::memory_order_relaxed); }
    int64_t last_queue_latency_us() const { return last_queue_us_.load(std::memory_order_relaxed); }
    int64_t last_total_latency_us() const { return last_total_us_.load(std::memory_order_relaxed); }

    // Access order book for a symbol (thread-safe only from processing thread).
    const OrderBook* get_order_book(const std::string& symbol) const;
    const AggregatorSnapshot* get_aggregator(const std::string& symbol) const;

private:
    struct SymbolState {
        OrderBook book;
        Aggregator aggregator;
        SymbolState(const std::string& sym) : book(sym) {}
    };

    void run(std::atomic<bool>& running);
    void process(core::QueueMessage& msg);
    SymbolState& get_or_create_state(const std::string& symbol);

    feed::FeedQueue& queue_;
    output::DiskLogger* recorder_;   // Non-owning, nullable. Records every processed message.
    std::thread thread_;
    std::unordered_map<std::string, std::unique_ptr<SymbolState>> states_;
    SignalDetector signal_detector_;
    std::vector<SignalCondition> signal_conditions_;

    std::atomic<uint64_t> processed_{0};
    std::atomic<uint64_t> signals_fired_{0};
    std::atomic<int64_t> last_parse_us_{0};
    std::atomic<int64_t> last_queue_us_{0};
    std::atomic<int64_t> last_total_us_{0};
};

} // namespace mde::engine
