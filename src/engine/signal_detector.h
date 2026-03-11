#pragma once

#include "engine/aggregator.h"
#include "engine/order_book.h"
#include "engine/trade_aggregator.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace mde::engine {

// Types of signals that can be detected.
enum class SignalType : uint8_t {
    SPREAD_WIDE,            // Spread exceeds threshold
    SPREAD_NARROW,          // Spread falls below threshold
    IMBALANCE_BID,          // Bid-heavy imbalance exceeds threshold
    IMBALANCE_ASK,          // Ask-heavy imbalance exceeds threshold (negative)
    PRICE_DEVIATION,        // Mid price deviates from SMA by threshold
    TRADE_IMBALANCE_BUY,    // Buy-aggressor trade imbalance exceeds threshold
    TRADE_IMBALANCE_SELL,   // Sell-aggressor trade imbalance exceeds threshold
    VOLUME_SPIKE,           // Current window volume > threshold × SMA
    BOOK_PRESSURE_BID,      // Imbalance delta > threshold (bid pressure increasing)
    BOOK_PRESSURE_ASK,      // Imbalance delta < -threshold (ask pressure increasing)
};

struct Signal {
    SignalType type;
    std::string symbol;
    double value;        // The metric value that triggered the signal
    double threshold;    // The configured threshold
    uint64_t timestamp_us; // Wall-clock microseconds
};

// Configuration for a single signal condition.
struct SignalCondition {
    SignalType type;
    double threshold;
    uint64_t cooldown_us = 1'000'000;  // Suppress re-fire for this duration (default 1s)
};

// Callback invoked when a signal fires.
using SignalCallback = std::function<void(const Signal&)>;

// Evaluates conditions against aggregator state and fires signals.
class SignalDetector {
public:
    explicit SignalDetector(SignalCallback callback);

    // Add a detection condition.
    void add_condition(SignalCondition condition);

    // Evaluate all conditions against current state. Fires callback for each match.
    // trade_agg is optional — trade-based signals are skipped if null.
    void evaluate(const std::string& symbol,
                  const OrderBook& book,
                  const AggregatorSnapshot& agg,
                  const TradeAggregatorSnapshot* trade_agg = nullptr);

    uint64_t signals_fired() const { return signals_fired_; }
    uint64_t signals_suppressed() const { return suppressed_; }

private:
    // Key for cooldown tracking: signal type + symbol
    struct CooldownKey {
        SignalType type;
        std::string symbol;
        bool operator==(const CooldownKey& o) const {
            return type == o.type && symbol == o.symbol;
        }
    };
    struct CooldownKeyHash {
        size_t operator()(const CooldownKey& k) const {
            return std::hash<uint8_t>()(static_cast<uint8_t>(k.type))
                 ^ (std::hash<std::string>()(k.symbol) << 8);
        }
    };

    SignalCallback callback_;
    std::vector<SignalCondition> conditions_;
    std::unordered_map<CooldownKey, uint64_t, CooldownKeyHash> last_fired_;
    uint64_t signals_fired_ = 0;
    uint64_t suppressed_ = 0;
};

// Human-readable signal type name.
const char* signal_type_name(SignalType type);

} // namespace mde::engine
