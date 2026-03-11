#include "engine/trade_aggregator.h"

#include <numeric>

namespace mde::engine {

TradeAggregator::TradeAggregator(uint64_t window_ms, size_t sma_windows)
    : window_ms_(window_ms)
    , sma_windows_(sma_windows) {}

void TradeAggregator::update(const core::Trade& trade) {
    uint64_t now_ms = trade.event_time_ms;

    // Initialize window start on first trade
    if (window_start_ms_ == 0) {
        window_start_ms_ = now_ms;
    }

    // Check if we've crossed into a new window — archive the old one
    if (now_ms >= window_start_ms_ + window_ms_) {
        // Archive fixed-window volume for SMA
        completed_volumes_.push_back(current_window_volume_);
        if (completed_volumes_.size() > sma_windows_) {
            completed_volumes_.pop_front();
        }
        ++snapshot_.window_count;
        current_window_volume_ = 0.0;

        // Advance window start (skip empty windows)
        window_start_ms_ = now_ms - (now_ms % window_ms_);
    }

    // Expire trades outside the sliding window
    expire_old(now_ms);

    // Add new trade
    bool is_buy = !trade.is_buyer_maker;
    trades_.push_back({now_ms, trade.quantity, is_buy});
    current_window_volume_ += trade.quantity;

    recompute();
}

void TradeAggregator::expire_old(uint64_t now_ms) {
    uint64_t cutoff = (now_ms > window_ms_) ? (now_ms - window_ms_) : 0;
    while (!trades_.empty() && trades_.front().timestamp_ms < cutoff) {
        trades_.pop_front();
    }
}

void TradeAggregator::recompute() {
    double buy_vol = 0.0;
    double sell_vol = 0.0;
    uint64_t count = 0;

    for (const auto& t : trades_) {
        if (t.is_buy) {
            buy_vol += t.quantity;
        } else {
            sell_vol += t.quantity;
        }
        ++count;
    }

    double total = buy_vol + sell_vol;
    snapshot_.buy_volume = buy_vol;
    snapshot_.sell_volume = sell_vol;
    snapshot_.total_volume = total;
    snapshot_.trade_imbalance = (total > 0.0) ? (buy_vol - sell_vol) / total : 0.0;
    snapshot_.trade_count = count;

    // Volume SMA from completed windows
    if (!completed_volumes_.empty()) {
        snapshot_.volume_sma = std::accumulate(
            completed_volumes_.begin(), completed_volumes_.end(), 0.0)
            / static_cast<double>(completed_volumes_.size());
    }
}

void TradeAggregator::reset() {
    trades_.clear();
    completed_volumes_.clear();
    window_start_ms_ = 0;
    snapshot_ = {};
}

} // namespace mde::engine
