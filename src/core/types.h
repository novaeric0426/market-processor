#pragma once

#include "core/clock.h"

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace mde::core {

// A single price-quantity level in the order book.
struct PriceLevel {
    double price = 0.0;
    double quantity = 0.0;
};

// Parsed Binance trade message.
// Corresponds to the "trade" event from the WebSocket stream.
struct Trade {
    uint64_t event_time_ms = 0;       // "E": exchange event timestamp (ms)
    uint64_t trade_id = 0;            // "t": trade ID
    std::string symbol;               // "s": e.g. "BTCUSDT"
    double price = 0.0;               // "p": trade price
    double quantity = 0.0;            // "q": trade quantity
    bool is_buyer_maker = false;      // "m": true = buyer is maker (seller is aggressor)

    // Latency instrumentation timestamps (steady_clock)
    Clock::time_point ts_received;
    Clock::time_point ts_parsed;
    Clock::time_point ts_enqueued;
    Clock::time_point ts_dequeued;
};

// Parsed Binance depth update message.
// Corresponds to the "depthUpdate" event from the WebSocket stream.
struct DepthUpdate {
    // Binance fields
    uint64_t event_time_ms = 0;       // "E": exchange event timestamp (ms)
    uint64_t first_update_id = 0;     // "U": first update ID in event
    uint64_t last_update_id = 0;      // "u": last update ID in event
    std::string symbol;               // "s": e.g. "BTCUSDT"

    // Price levels
    static constexpr size_t MAX_LEVELS = 64;
    std::array<PriceLevel, MAX_LEVELS> bids;
    std::array<PriceLevel, MAX_LEVELS> asks;
    uint16_t bid_count = 0;
    uint16_t ask_count = 0;

    // Latency instrumentation timestamps (steady_clock)
    Clock::time_point ts_received;     // When raw bytes arrived from WebSocket
    Clock::time_point ts_parsed;       // When JSON parsing completed
    Clock::time_point ts_enqueued;     // When pushed into SPSC queue
    Clock::time_point ts_dequeued;     // When popped from SPSC queue
};

// Message type tag for the SPSC queue.
enum class MessageType : uint8_t {
    DEPTH_UPDATE = 1,
    TRADE = 2,
};

// Wrapper for the SPSC queue. Fixed-size, no heap allocation on hot path.
// Tagged union: check `type` to know which field is active.
struct QueueMessage {
    MessageType type = MessageType::DEPTH_UPDATE;
    DepthUpdate depth;
    Trade trade;
};

} // namespace mde::core
