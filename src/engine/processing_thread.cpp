#include "engine/processing_thread.h"

#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

namespace mde::engine {

ProcessingThread::ProcessingThread(feed::FeedQueue& queue)
    : queue_(queue) {}

ProcessingThread::~ProcessingThread() {
    stop();
}

void ProcessingThread::start(std::atomic<bool>& running) {
    thread_ = std::thread([this, &running]() {
        spdlog::info("Processing thread started");
        run(running);
        spdlog::info("Processing thread exited");
    });
}

void ProcessingThread::stop() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ProcessingThread::run(std::atomic<bool>& running) {
    core::QueueMessage msg;

    while (running.load(std::memory_order_relaxed)) {
        if (queue_.try_pop(msg)) {
            msg.depth.ts_dequeued = core::Clock::now();
            process(msg);
        } else {
            // Busy-wait with minimal pause to keep latency low
            // A more sophisticated approach would use adaptive spinning
            std::this_thread::yield();
        }
    }

    // Drain remaining messages
    while (queue_.try_pop(msg)) {
        msg.depth.ts_dequeued = core::Clock::now();
        process(msg);
    }
}

void ProcessingThread::process(core::QueueMessage& msg) {
    auto count = processed_.fetch_add(1, std::memory_order_relaxed) + 1;

    // Compute per-stage latencies
    auto parse_us = core::Clock::elapsed_us(msg.depth.ts_received, msg.depth.ts_parsed);
    auto queue_us = core::Clock::elapsed_us(msg.depth.ts_parsed, msg.depth.ts_dequeued);
    auto total_us = core::Clock::elapsed_us(msg.depth.ts_received, msg.depth.ts_dequeued);

    last_parse_us_.store(parse_us, std::memory_order_relaxed);
    last_queue_us_.store(queue_us, std::memory_order_relaxed);
    last_total_us_.store(total_us, std::memory_order_relaxed);

    // Log first few and then periodically
    if (count <= 3 || count % 100 == 0) {
        spdlog::debug("[processed #{}] {} bids={} asks={} | parse={}us queue={}us total={}us",
            count, msg.depth.symbol,
            msg.depth.bid_count, msg.depth.ask_count,
            parse_us, queue_us, total_us);
    }
}

} // namespace mde::engine
