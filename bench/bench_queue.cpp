// Benchmark: SPSC lock-free ring buffer throughput and latency
//
// Measures single-threaded push/pop throughput and cross-thread latency.

#include "core/spsc_queue.h"
#include "core/types.h"
#include "core/clock.h"

#include <benchmark/benchmark.h>

#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace {

// Small queue for benchmarks (avoid 40MB QueueMessage queue in tight loops)
using SmallQueue = mde::core::SPSCQueue<uint64_t, 4096>;
using MessageQueue = mde::core::SPSCQueue<mde::core::QueueMessage, 1024>;

// ---- Single-threaded throughput ----

void BM_QueuePushPop_uint64(benchmark::State& state) {
    SmallQueue queue;
    uint64_t val = 0;

    for (auto _ : state) {
        queue.try_push(val);
        auto result = queue.try_pop();
        benchmark::DoNotOptimize(result);
        ++val;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_QueuePushPop_uint64);

void BM_QueuePushPop_QueueMessage(benchmark::State& state) {
    auto queue = std::make_unique<MessageQueue>();
    mde::core::QueueMessage msg;
    msg.type = mde::core::MessageType::DEPTH_UPDATE;
    msg.depth.symbol = "BTCUSDT";
    msg.depth.bid_count = 20;
    msg.depth.ask_count = 20;

    for (auto _ : state) {
        queue->try_push(msg);
        mde::core::QueueMessage out;
        queue->try_pop(out);
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * sizeof(mde::core::QueueMessage));
}
BENCHMARK(BM_QueuePushPop_QueueMessage);

// ---- Burst throughput (fill then drain) ----

void BM_QueueBurst(benchmark::State& state) {
    const size_t burst_size = static_cast<size_t>(state.range(0));
    SmallQueue queue;

    for (auto _ : state) {
        // Fill
        for (size_t i = 0; i < burst_size; ++i) {
            queue.try_push(i);
        }
        // Drain
        for (size_t i = 0; i < burst_size; ++i) {
            auto v = queue.try_pop();
            benchmark::DoNotOptimize(v);
        }
    }
    state.SetItemsProcessed(state.iterations() * burst_size * 2);
}
BENCHMARK(BM_QueueBurst)->Arg(64)->Arg(256)->Arg(1024)->Arg(4095);

// ---- Cross-thread latency ----

void BM_QueueCrossThreadLatency(benchmark::State& state) {
    SmallQueue queue;
    std::atomic<bool> done{false};
    std::vector<int64_t> latencies;
    latencies.reserve(1000000);

    // Consumer thread: measure time from push to pop
    std::thread consumer([&] {
        while (!done.load(std::memory_order_relaxed)) {
            auto val = queue.try_pop();
            if (val.has_value()) {
                auto now = mde::core::Clock::now();
                // Value encodes the push timestamp as nanoseconds offset
                auto push_time = mde::core::Clock::time_point(
                    std::chrono::nanoseconds(*val));
                auto latency_ns = mde::core::Clock::elapsed_ns(push_time, now);
                latencies.push_back(latency_ns);
            }
        }
        // Drain remaining
        while (auto val = queue.try_pop()) {
            (void)val;
        }
    });

    for (auto _ : state) {
        auto now = mde::core::Clock::now();
        uint64_t ts = static_cast<uint64_t>(now.time_since_epoch().count());
        queue.try_push(ts);
    }

    done.store(true, std::memory_order_relaxed);
    consumer.join();

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        auto p50 = latencies[latencies.size() * 50 / 100];
        auto p99 = latencies[latencies.size() * 99 / 100];
        auto p999 = latencies[latencies.size() * 999 / 1000];
        state.counters["p50_ns"] = p50;
        state.counters["p99_ns"] = p99;
        state.counters["p999_ns"] = p999;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_QueueCrossThreadLatency)->UseRealTime();

} // namespace

BENCHMARK_MAIN();
