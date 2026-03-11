#pragma once

#include "feed/feed_handler.h"
#include "engine/order_book.h"
#include "engine/aggregator.h"
#include "engine/trade_aggregator.h"
#include "engine/signal_detector.h"
#include "output/disk_logger.h"
#include "core/types.h"
#include "core/clock.h"

#include <atomic>
#include <thread>
#include <mutex>
#include <deque>
#include <cstdint>
#include <unordered_map>
#include <map>
#include <memory>
#include <vector>

namespace mde::engine {

// Thread-safe snapshot of a single symbol's state.
struct SymbolSnapshot {
    std::string symbol;
    std::map<double, double, std::greater<>> bids;  // price→qty, descending
    std::map<double, double> asks;                    // price→qty, ascending
    AggregatorSnapshot aggregation;
    double best_bid = 0.0;
    double best_ask = 0.0;
    double spread = 0.0;
    double mid_price = 0.0;
    uint64_t update_count = 0;
};

// Complete engine state snapshot, safe to read from any thread.
struct EngineSnapshot {
    std::vector<SymbolSnapshot> symbols;
    std::vector<Signal> recent_signals;  // Most recent signals (newest first)
    uint64_t processed_count = 0;
    uint64_t trades_processed = 0;
    uint64_t signals_fired = 0;
    int64_t last_parse_us = 0;
    int64_t last_queue_us = 0;
    int64_t last_total_us = 0;
};

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
    uint64_t trades_processed() const { return trades_processed_.load(std::memory_order_relaxed); }
    uint64_t signals_fired() const { return signals_fired_.load(std::memory_order_relaxed); }

    // Latency stats (microseconds)
    int64_t last_parse_latency_us() const { return last_parse_us_.load(std::memory_order_relaxed); }
    int64_t last_queue_latency_us() const { return last_queue_us_.load(std::memory_order_relaxed); }
    int64_t last_total_latency_us() const { return last_total_us_.load(std::memory_order_relaxed); }

    // Access order book for a symbol (thread-safe only from processing thread).
    const OrderBook* get_order_book(const std::string& symbol) const;
    const AggregatorSnapshot* get_aggregator(const std::string& symbol) const;

    // Thread-safe snapshot of all symbol states + metrics.
    // Safe to call from any thread (mutex-protected).
    EngineSnapshot take_snapshot() const;

private:
    struct SymbolState {
        OrderBook book;
        Aggregator aggregator;
        TradeAggregator trade_aggregator;
        SymbolState(const std::string& sym) : book(sym) {}
    };

    void run(std::atomic<bool>& running);
    void process(core::QueueMessage& msg);
    void process_depth(core::DepthUpdate& depth);
    void process_trade(core::Trade& trade);
    SymbolState& get_or_create_state(const std::string& symbol);

    void update_snapshot();

    feed::FeedQueue& queue_;
    output::DiskLogger* recorder_;   // Non-owning, nullable. Records every processed message.
    std::thread thread_;
    std::unordered_map<std::string, std::unique_ptr<SymbolState>> states_;
    mutable std::mutex snapshot_mutex_;
    EngineSnapshot snapshot_;
    SignalDetector signal_detector_;
    std::vector<SignalCondition> signal_conditions_;

    static constexpr size_t MAX_RECENT_SIGNALS = 50;
    std::deque<Signal> recent_signals_;  // Guarded by snapshot_mutex_

    std::atomic<uint64_t> processed_{0};
    std::atomic<uint64_t> trades_processed_{0};
    std::atomic<uint64_t> signals_fired_{0};
    std::atomic<int64_t> last_parse_us_{0};
    std::atomic<int64_t> last_queue_us_{0};
    std::atomic<int64_t> last_total_us_{0};
};

} // namespace mde::engine
