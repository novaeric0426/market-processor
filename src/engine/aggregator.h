#pragma once

#include "engine/order_book.h"

#include <deque>
#include <cstdint>

namespace mde::engine {

// Rolling window statistics computed from order book snapshots.
struct AggregatorSnapshot {
    double spread = 0.0;
    double mid_price = 0.0;
    double bid_vwap = 0.0;        // VWAP over top N bid levels
    double ask_vwap = 0.0;        // VWAP over top N ask levels
    double mid_price_sma = 0.0;   // Simple moving average of mid price
    double spread_sma = 0.0;      // Simple moving average of spread
    double bid_ask_imbalance = 0.0; // (total_bid_qty - total_ask_qty) / (total_bid_qty + total_ask_qty)
    uint64_t sample_count = 0;
};

// Computes rolling aggregations from order book state.
// Maintains a window of recent mid-price and spread values for SMA.
class Aggregator {
public:
    // vwap_levels: number of top levels for VWAP computation.
    // sma_window: number of samples for moving average.
    explicit Aggregator(size_t vwap_levels = 5, size_t sma_window = 20);

    // Update aggregations from current order book state.
    // Call this after each order book update.
    void update(const OrderBook& book);

    const AggregatorSnapshot& snapshot() const { return snapshot_; }

    // Reset all state.
    void reset();

private:
    size_t vwap_levels_;
    size_t sma_window_;

    std::deque<double> mid_prices_;
    std::deque<double> spreads_;
    AggregatorSnapshot snapshot_;
};

} // namespace mde::engine
