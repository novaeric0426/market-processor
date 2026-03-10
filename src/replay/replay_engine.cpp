#include "replay/replay_engine.h"

#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>

namespace mde::replay {

ReplayEngine::ReplayEngine(feed::FeedQueue& queue)
    : queue_(queue) {}

ReplayEngine::~ReplayEngine() {
    stop();
}

bool ReplayEngine::load(const std::string& path) {
    if (!reader_.open(path)) {
        return false;
    }
    total_records_ = reader_.file_header().record_count;
    spdlog::info("ReplayEngine: loaded '{}', {} records", path, total_records_);
    return true;
}

void ReplayEngine::start(std::atomic<bool>& running) {
    finished_.store(false, std::memory_order_relaxed);
    replayed_.store(0, std::memory_order_relaxed);

    thread_ = std::thread([this, &running]() {
        spdlog::info("ReplayEngine: playback started");
        run(running);
        spdlog::info("ReplayEngine: playback finished ({} records)", replayed_.load());
    });
}

void ReplayEngine::stop() {
    // Unpause so the thread can exit
    paused_.store(false, std::memory_order_relaxed);
    if (thread_.joinable()) {
        thread_.join();
    }
    reader_.close();
}

void ReplayEngine::pause() {
    paused_.store(true, std::memory_order_relaxed);
    spdlog::info("ReplayEngine: paused");
}

void ReplayEngine::resume() {
    paused_.store(false, std::memory_order_relaxed);
    spdlog::info("ReplayEngine: resumed");
}

void ReplayEngine::set_speed(PlaybackSpeed speed) {
    speed_.store(speed, std::memory_order_relaxed);
    spdlog::info("ReplayEngine: speed set to {}x",
        speed == PlaybackSpeed::MAX ? 0 : static_cast<int>(1 << static_cast<int>(speed)));
}

double ReplayEngine::speed_divisor() const {
    switch (speed_.load(std::memory_order_relaxed)) {
        case PlaybackSpeed::REALTIME: return 1.0;
        case PlaybackSpeed::FAST_2X:  return 2.0;
        case PlaybackSpeed::FAST_5X:  return 5.0;
        case PlaybackSpeed::MAX:      return 0.0;  // No delay
    }
    return 1.0;
}

void ReplayEngine::run(std::atomic<bool>& running) {
    output::DepthRecord record;
    output::RecordHeader rhdr;
    uint64_t prev_wall_us = 0;

    while (running.load(std::memory_order_relaxed)) {
        // Handle pause
        while (paused_.load(std::memory_order_relaxed) &&
               running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!running.load(std::memory_order_relaxed)) break;

        // Read next record
        if (!reader_.read_next(record, rhdr)) {
            // EOF or error — replay complete
            finished_.store(true, std::memory_order_relaxed);
            break;
        }

        // Apply inter-message delay based on original timestamps
        double divisor = speed_divisor();
        if (divisor > 0.0 && prev_wall_us > 0 && rhdr.wall_timestamp_us > prev_wall_us) {
            uint64_t delta_us = rhdr.wall_timestamp_us - prev_wall_us;
            auto sleep_us = static_cast<uint64_t>(static_cast<double>(delta_us) / divisor);
            if (sleep_us > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
            }
        }
        prev_wall_us = rhdr.wall_timestamp_us;

        // Convert to runtime type and enqueue
        core::DepthUpdate update = output::from_record(record);

        // Set fresh timestamps for latency measurement in replay mode
        auto now = core::Clock::now();
        update.ts_received = now;
        update.ts_parsed = now;

        core::QueueMessage msg;
        msg.type = core::MessageType::DEPTH_UPDATE;
        msg.depth = std::move(update);
        msg.depth.ts_enqueued = core::Clock::now();

        // Spin-wait if queue is full (backpressure)
        while (!queue_.try_push(std::move(msg))) {
            if (!running.load(std::memory_order_relaxed)) return;
            std::this_thread::yield();
        }

        replayed_.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace mde::replay
