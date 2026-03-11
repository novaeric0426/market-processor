#include "engine/aggregator.h"

#include <numeric>

namespace mde::engine {

Aggregator::Aggregator(size_t vwap_levels, size_t sma_window)
    : vwap_levels_(vwap_levels)
    , sma_window_(sma_window) {}

void Aggregator::update(const OrderBook& book) {
    snapshot_.spread = book.spread();
    snapshot_.mid_price = book.mid_price();
    snapshot_.bid_vwap = book.bid_vwap(vwap_levels_);
    snapshot_.ask_vwap = book.ask_vwap(vwap_levels_);

    // Bid-ask imbalance + pressure (rate of change)
    double total_bid = book.total_bid_qty();
    double total_ask = book.total_ask_qty();
    double total = total_bid + total_ask;
    double prev_imbalance = snapshot_.bid_ask_imbalance;
    snapshot_.bid_ask_imbalance = (total > 0.0) ? (total_bid - total_ask) / total : 0.0;
    snapshot_.imbalance_delta = snapshot_.bid_ask_imbalance - prev_imbalance;

    // Rolling SMA
    if (snapshot_.mid_price > 0.0) {
        mid_prices_.push_back(snapshot_.mid_price);
        if (mid_prices_.size() > sma_window_) {
            mid_prices_.pop_front();
        }
        snapshot_.mid_price_sma = std::accumulate(
            mid_prices_.begin(), mid_prices_.end(), 0.0) / mid_prices_.size();
    }

    if (snapshot_.spread >= 0.0) {
        spreads_.push_back(snapshot_.spread);
        if (spreads_.size() > sma_window_) {
            spreads_.pop_front();
        }
        snapshot_.spread_sma = std::accumulate(
            spreads_.begin(), spreads_.end(), 0.0) / spreads_.size();
    }

    ++snapshot_.sample_count;
}

void Aggregator::reset() {
    mid_prices_.clear();
    spreads_.clear();
    snapshot_ = {};
}

} // namespace mde::engine
