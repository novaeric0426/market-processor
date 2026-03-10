#include "engine/order_book.h"

#include <spdlog/spdlog.h>

namespace mde::engine {

OrderBook::OrderBook(const std::string& symbol)
    : symbol_(symbol) {}

void OrderBook::apply_update(const core::DepthUpdate& update) {
    // Gap detection: warn if update IDs are not contiguous
    if (last_update_id_ > 0 && update.first_update_id > last_update_id_ + 1) {
        spdlog::warn("[{}] Sequence gap: last_u={} next_U={} (gap={})",
            symbol_, last_update_id_, update.first_update_id,
            update.first_update_id - last_update_id_ - 1);
    }

    // Apply bid deltas
    for (uint16_t i = 0; i < update.bid_count; ++i) {
        double price = update.bids[i].price;
        double qty = update.bids[i].quantity;
        if (qty == 0.0) {
            bids_.erase(price);
        } else {
            bids_[price] = qty;
        }
    }

    // Apply ask deltas
    for (uint16_t i = 0; i < update.ask_count; ++i) {
        double price = update.asks[i].price;
        double qty = update.asks[i].quantity;
        if (qty == 0.0) {
            asks_.erase(price);
        } else {
            asks_[price] = qty;
        }
    }

    last_update_id_ = update.last_update_id;
    ++update_count_;
}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
    last_update_id_ = 0;
    update_count_ = 0;
}

double OrderBook::best_bid_price() const {
    return bids_.empty() ? 0.0 : bids_.begin()->first;
}

double OrderBook::best_ask_price() const {
    return asks_.empty() ? 0.0 : asks_.begin()->first;
}

double OrderBook::best_bid_qty() const {
    return bids_.empty() ? 0.0 : bids_.begin()->second;
}

double OrderBook::best_ask_qty() const {
    return asks_.empty() ? 0.0 : asks_.begin()->second;
}

double OrderBook::spread() const {
    if (bids_.empty() || asks_.empty()) return 0.0;
    return best_ask_price() - best_bid_price();
}

double OrderBook::mid_price() const {
    if (bids_.empty() || asks_.empty()) return 0.0;
    return (best_bid_price() + best_ask_price()) / 2.0;
}

double OrderBook::total_bid_qty() const {
    double total = 0.0;
    for (const auto& [price, qty] : bids_) {
        total += qty;
    }
    return total;
}

double OrderBook::total_ask_qty() const {
    double total = 0.0;
    for (const auto& [price, qty] : asks_) {
        total += qty;
    }
    return total;
}

double OrderBook::bid_vwap(size_t levels) const {
    double sum_pq = 0.0;
    double sum_q = 0.0;
    size_t count = 0;
    for (const auto& [price, qty] : bids_) {
        if (count >= levels) break;
        sum_pq += price * qty;
        sum_q += qty;
        ++count;
    }
    return sum_q > 0.0 ? sum_pq / sum_q : 0.0;
}

double OrderBook::ask_vwap(size_t levels) const {
    double sum_pq = 0.0;
    double sum_q = 0.0;
    size_t count = 0;
    for (const auto& [price, qty] : asks_) {
        if (count >= levels) break;
        sum_pq += price * qty;
        sum_q += qty;
        ++count;
    }
    return sum_q > 0.0 ? sum_pq / sum_q : 0.0;
}

} // namespace mde::engine
