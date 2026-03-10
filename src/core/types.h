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
};

// Wrapper for the SPSC queue. Fixed-size, no heap allocation on hot path.
struct QueueMessage {
    MessageType type = MessageType::DEPTH_UPDATE;
    DepthUpdate depth;
};

} // namespace mde::core
