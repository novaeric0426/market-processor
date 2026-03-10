#pragma once

#include <cstdint>
#include <algorithm>

namespace mde::feed {

// Exponential backoff reconnect policy.
// Doubles the delay on each failure, up to a configurable maximum.
class ReconnectPolicy {
public:
    ReconnectPolicy(uint32_t initial_delay_ms, uint32_t max_delay_ms, double multiplier)
        : initial_delay_ms_(initial_delay_ms)
        , max_delay_ms_(max_delay_ms)
        , multiplier_(multiplier)
        , current_delay_ms_(initial_delay_ms) {}

    // Returns the current delay in milliseconds, then advances to the next.
    uint32_t next_delay_ms() {
        uint32_t delay = current_delay_ms_;
        current_delay_ms_ = std::min(
            static_cast<uint32_t>(current_delay_ms_ * multiplier_),
            max_delay_ms_
        );
        return delay;
    }

    // Reset delay back to the initial value (call on successful connection).
    void reset() {
        current_delay_ms_ = initial_delay_ms_;
    }

private:
    uint32_t initial_delay_ms_;
    uint32_t max_delay_ms_;
    double multiplier_;
    uint32_t current_delay_ms_;
};

} // namespace mde::feed
