#pragma once

#include <chrono>
#include <cstdint>

namespace mde::core {

// Microsecond-resolution timestamps for latency measurement.
// Uses steady_clock to avoid wall-clock adjustments.
struct Clock {
    using clock_type = std::chrono::steady_clock;
    using time_point = clock_type::time_point;

    static time_point now() noexcept {
        return clock_type::now();
    }

    // Wall-clock timestamp in microseconds since epoch (for logging/recording).
    static uint64_t wall_us() noexcept {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
    }

    // Elapsed microseconds between two steady_clock time points.
    static int64_t elapsed_us(time_point start, time_point end) noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }

    // Elapsed nanoseconds between two steady_clock time points.
    static int64_t elapsed_ns(time_point start, time_point end) noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }
};

} // namespace mde::core
