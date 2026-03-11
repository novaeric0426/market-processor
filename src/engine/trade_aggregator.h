#pragma once

#include "core/types.h"

#include <deque>
#include <cstdint>

namespace mde::engine {

// Rolling window statistics computed from trade stream.
struct TradeAggregatorSnapshot {
    double buy_volume = 0.0;        // Aggressor-buy volume in current window
    double sell_volume = 0.0;       // Aggressor-sell volume in current window
    double total_volume = 0.0;      // Total volume in current window
    double trade_imbalance = 0.0;   // (buy - sell) / (buy + sell), range [-1, +1]
    double volume_sma = 0.0;        // SMA of completed window volumes
    uint64_t trade_count = 0;       // Trades in current window
    uint64_t window_count = 0;      // Number of completed windows
};

// Accumulates trade data in a sliding time window and computes
// rolling statistics for signal detection.
class TradeAggregator {
public:
    // window_ms: duration of the sliding window in milliseconds.
    // sma_windows: number of completed window volumes for SMA.
    explicit TradeAggregator(uint64_t window_ms = 1000, size_t sma_windows = 20);

    // Add a trade. Uses event_time_ms for windowing.
    void update(const core::Trade& trade);

    const TradeAggregatorSnapshot& snapshot() const { return snapshot_; }

    void reset();

private:
    struct TradeEntry {
        uint64_t timestamp_ms;
        double quantity;
        bool is_buy;  // !is_buyer_maker = buyer is aggressor
    };

    void expire_old(uint64_t now_ms);
    void recompute();

    uint64_t window_ms_;
    size_t sma_windows_;

    std::deque<TradeEntry> trades_;
    std::deque<double> completed_volumes_;  // Historical window volumes for SMA

    uint64_t window_start_ms_ = 0;
    double current_window_volume_ = 0.0;  // Fixed-window accumulator for volume SMA
    TradeAggregatorSnapshot snapshot_;
};

} // namespace mde::engine
