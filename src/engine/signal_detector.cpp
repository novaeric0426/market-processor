#include "engine/signal_detector.h"
#include "core/clock.h"

#include <cmath>

namespace mde::engine {

SignalDetector::SignalDetector(SignalCallback callback)
    : callback_(std::move(callback)) {}

void SignalDetector::add_condition(SignalCondition condition) {
    conditions_.push_back(condition);
}

void SignalDetector::evaluate(const std::string& symbol,
                              const OrderBook& book,
                              const AggregatorSnapshot& agg) {
    if (agg.sample_count == 0) return;

    for (const auto& cond : conditions_) {
        bool triggered = false;
        double value = 0.0;

        switch (cond.type) {
            case SignalType::SPREAD_WIDE:
                value = agg.spread;
                triggered = (value > cond.threshold);
                break;

            case SignalType::SPREAD_NARROW:
                value = agg.spread;
                triggered = (value > 0.0 && value < cond.threshold);
                break;

            case SignalType::IMBALANCE_BID:
                value = agg.bid_ask_imbalance;
                triggered = (value > cond.threshold);
                break;

            case SignalType::IMBALANCE_ASK:
                value = agg.bid_ask_imbalance;
                triggered = (value < -cond.threshold);
                break;

            case SignalType::PRICE_DEVIATION:
                if (agg.mid_price_sma > 0.0) {
                    value = std::abs(agg.mid_price - agg.mid_price_sma) / agg.mid_price_sma;
                    triggered = (value > cond.threshold);
                }
                break;
        }

        if (triggered) {
            Signal sig;
            sig.type = cond.type;
            sig.symbol = symbol;
            sig.value = value;
            sig.threshold = cond.threshold;
            sig.timestamp_us = core::Clock::wall_us();

            if (callback_) {
                callback_(sig);
            }
            ++signals_fired_;
        }
    }
}

const char* signal_type_name(SignalType type) {
    switch (type) {
        case SignalType::SPREAD_WIDE:     return "SPREAD_WIDE";
        case SignalType::SPREAD_NARROW:   return "SPREAD_NARROW";
        case SignalType::IMBALANCE_BID:   return "IMBALANCE_BID";
        case SignalType::IMBALANCE_ASK:   return "IMBALANCE_ASK";
        case SignalType::PRICE_DEVIATION: return "PRICE_DEVIATION";
    }
    return "UNKNOWN";
}

} // namespace mde::engine
