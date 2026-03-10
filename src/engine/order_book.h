#pragma once

#include "core/types.h"

#include <map>
#include <string>
#include <cstdint>
#include <functional>

namespace mde::engine {

// Maintains a price-level order book for a single symbol.
// Bids sorted descending (highest first), asks sorted ascending (lowest first).
class OrderBook {
public:
    explicit OrderBook(const std::string& symbol);

    // Apply a depth update (delta). Levels with quantity=0 are removed.
    void apply_update(const core::DepthUpdate& update);

    // Reset the entire book (e.g., on reconnect or snapshot).
    void clear();

    const std::string& symbol() const { return symbol_; }

    // Best bid/ask. Returns 0.0 if the side is empty.
    double best_bid_price() const;
    double best_ask_price() const;
    double best_bid_qty() const;
    double best_ask_qty() const;

    // Spread = best_ask - best_bid. Returns 0.0 if either side is empty.
    double spread() const;

    // Mid price = (best_bid + best_ask) / 2.
    double mid_price() const;

    // Number of price levels on each side.
    size_t bid_levels() const { return bids_.size(); }
    size_t ask_levels() const { return asks_.size(); }

    // Total quantity across all levels on each side.
    double total_bid_qty() const;
    double total_ask_qty() const;

    // VWAP over top N levels on each side.
    double bid_vwap(size_t levels) const;
    double ask_vwap(size_t levels) const;

    // Access raw book (for iteration/snapshot).
    // Bids: descending by price (std::greater).
    // Asks: ascending by price (std::less).
    const std::map<double, double, std::greater<>>& bids() const { return bids_; }
    const std::map<double, double>& asks() const { return asks_; }

    // Sequence tracking for gap detection.
    uint64_t last_update_id() const { return last_update_id_; }
    uint64_t update_count() const { return update_count_; }

private:
    std::string symbol_;
    std::map<double, double, std::greater<>> bids_; // price → qty, descending
    std::map<double, double> asks_;                  // price → qty, ascending
    uint64_t last_update_id_ = 0;
    uint64_t update_count_ = 0;
};

} // namespace mde::engine
