#pragma once

#include "output/disk_logger.h"
#include "feed/feed_handler.h"
#include "core/types.h"
#include "core/clock.h"

#include <atomic>
#include <thread>
#include <string>
#include <cstdint>

namespace mde::replay {

enum class PlaybackSpeed : uint8_t {
    REALTIME = 0,  // 1x — use original inter-message timing
    FAST_2X  = 1,  // 2x
    FAST_5X  = 2,  // 5x
    MAX      = 3,  // No delay
};

// Replays recorded binary data into the SPSC queue,
// substituting the live feed. The ProcessingThread consumes
// replay data exactly as it would live data.
class ReplayEngine {
public:
    ReplayEngine(feed::FeedQueue& queue);
    ~ReplayEngine();

    ReplayEngine(const ReplayEngine&) = delete;
    ReplayEngine& operator=(const ReplayEngine&) = delete;

    // Load a recording file. Must be called before start().
    bool load(const std::string& path);

    // Start replay on a background thread.
    void start(std::atomic<bool>& running);

    // Stop and join the replay thread.
    void stop();

    // Pause / resume playback.
    void pause();
    void resume();

    // Change playback speed (can be called while playing).
    void set_speed(PlaybackSpeed speed);
    PlaybackSpeed speed() const { return speed_.load(std::memory_order_relaxed); }

    // Metrics
    uint64_t replayed_count() const { return replayed_.load(std::memory_order_relaxed); }
    uint64_t total_records() const { return total_records_; }
    bool is_finished() const { return finished_.load(std::memory_order_relaxed); }
    bool is_paused() const { return paused_.load(std::memory_order_relaxed); }

private:
    void run(std::atomic<bool>& running);

    // Returns the delay divisor for the current speed setting.
    double speed_divisor() const;

    feed::FeedQueue& queue_;
    output::DiskReader reader_;
    std::thread thread_;

    std::atomic<PlaybackSpeed> speed_{PlaybackSpeed::REALTIME};
    std::atomic<bool> paused_{false};
    std::atomic<bool> finished_{false};
    std::atomic<uint64_t> replayed_{0};
    uint64_t total_records_ = 0;
};

} // namespace mde::replay
