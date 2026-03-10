// Benchmark: End-to-end pipeline latency
//
// Measures the full path: JSON parse → queue push → queue pop → order book update
// → aggregation → signal detection. This simulates the complete hot path.

#include "feed/depth_parser.h"
#include "core/spsc_queue.h"
#include "core/types.h"
#include "core/clock.h"
#include "engine/order_book.h"
#include "engine/aggregator.h"
#include "engine/signal_detector.h"

#include <benchmark/benchmark.h>

#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <numeric>
#include <memory>

namespace {

const std::string SAMPLE_JSON = R"({
    "e": "depthUpdate",
    "E": 1672531200000,
    "s": "BTCUSDT",
    "U": 100001,
    "u": 100020,
    "b": [
        ["97850.10", "1.234"], ["97849.50", "0.567"], ["97848.00", "2.100"],
        ["97847.30", "0.890"], ["97846.00", "1.500"], ["97845.20", "0.340"],
        ["97844.10", "2.780"], ["97843.50", "1.120"], ["97842.00", "0.450"],
        ["97841.30", "3.200"], ["97840.00", "0.670"], ["97839.50", "1.890"],
        ["97838.00", "0.230"], ["97837.30", "2.450"], ["97836.00", "1.100"],
        ["97835.20", "0.780"], ["97834.10", "3.400"], ["97833.50", "0.560"],
        ["97832.00", "1.900"], ["97831.30", "0.340"]
    ],
    "a": [
        ["97851.00", "0.890"], ["97852.50", "1.230"], ["97853.00", "2.340"],
        ["97854.30", "0.670"], ["97855.00", "1.100"], ["97856.20", "0.450"],
        ["97857.10", "2.890"], ["97858.50", "1.340"], ["97859.00", "0.780"],
        ["97860.30", "3.100"], ["97861.00", "0.560"], ["97862.50", "1.670"],
        ["97863.00", "0.120"], ["97864.30", "2.340"], ["97865.00", "1.450"],
        ["97866.20", "0.890"], ["97867.10", "3.200"], ["97868.50", "0.670"],
        ["97869.00", "1.780"], ["97870.30", "0.450"]
    ]
})";

// ---- Single-threaded E2E (no queue, measure processing cost) ----

void BM_E2E_SingleThread(benchmark::State& state) {
    mde::feed::DepthParser parser;
    mde::engine::OrderBook book("BTCUSDT");
    mde::engine::Aggregator agg(5, 20);
    uint64_t signal_count = 0;
    mde::engine::SignalDetector detector([&](const mde::engine::Signal&) {
        ++signal_count;
    });
    detector.add_condition({mde::engine::SignalType::SPREAD_WIDE, 50.0});
    detector.add_condition({mde::engine::SignalType::IMBALANCE_BID, 0.7});

    mde::core::DepthUpdate update;

    for (auto _ : state) {
        auto t0 = mde::core::Clock::now();

        // Parse
        parser.parse(SAMPLE_JSON, update);
        auto t1 = mde::core::Clock::now();

        // Order book update
        book.apply_update(update);
        auto t2 = mde::core::Clock::now();

        // Aggregation
        agg.update(book);
        auto t3 = mde::core::Clock::now();

        // Signal detection
        detector.evaluate("BTCUSDT", book, agg.snapshot());
        auto t4 = mde::core::Clock::now();

        auto snap = agg.snapshot();
        benchmark::DoNotOptimize(snap);

        // Report stage timings via counters (last iteration wins, but gives a sense)
        state.counters["parse_ns"] = mde::core::Clock::elapsed_ns(t0, t1);
        state.counters["orderbook_ns"] = mde::core::Clock::elapsed_ns(t1, t2);
        state.counters["aggregator_ns"] = mde::core::Clock::elapsed_ns(t2, t3);
        state.counters["signal_ns"] = mde::core::Clock::elapsed_ns(t3, t4);
        state.counters["total_ns"] = mde::core::Clock::elapsed_ns(t0, t4);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_E2E_SingleThread);

// ---- Two-thread E2E (producer parses+enqueues, consumer processes) ----

using E2EQueue = mde::core::SPSCQueue<mde::core::QueueMessage, 1024>;

void BM_E2E_TwoThread(benchmark::State& state) {
    auto queue = std::make_unique<E2EQueue>();
    std::atomic<bool> done{false};
    std::vector<int64_t> latencies;
    latencies.reserve(1000000);

    // Consumer: dequeue → order book → aggregation → signal
    std::thread consumer([&] {
        mde::engine::OrderBook book("BTCUSDT");
        mde::engine::Aggregator agg(5, 20);
        uint64_t signal_count = 0;
        mde::engine::SignalDetector detector([&](const mde::engine::Signal&) {
            ++signal_count;
        });
        detector.add_condition({mde::engine::SignalType::SPREAD_WIDE, 50.0});
        detector.add_condition({mde::engine::SignalType::IMBALANCE_BID, 0.7});

        mde::core::QueueMessage msg;
        while (!done.load(std::memory_order_relaxed)) {
            if (queue->try_pop(msg)) {
                msg.depth.ts_dequeued = mde::core::Clock::now();
                book.apply_update(msg.depth);
                agg.update(book);
                detector.evaluate("BTCUSDT", book, agg.snapshot());

                auto total_ns = mde::core::Clock::elapsed_ns(
                    msg.depth.ts_received, mde::core::Clock::now());
                latencies.push_back(total_ns);
            }
        }
        // Drain remaining
        while (queue->try_pop(msg)) {
            msg.depth.ts_dequeued = mde::core::Clock::now();
            book.apply_update(msg.depth);
            agg.update(book);
            detector.evaluate("BTCUSDT", book, agg.snapshot());
            auto total_ns = mde::core::Clock::elapsed_ns(
                msg.depth.ts_received, mde::core::Clock::now());
            latencies.push_back(total_ns);
        }
    });

    // Producer: parse → enqueue
    mde::feed::DepthParser parser;
    for (auto _ : state) {
        auto ts_received = mde::core::Clock::now();

        mde::core::QueueMessage msg;
        msg.type = mde::core::MessageType::DEPTH_UPDATE;
        parser.parse(SAMPLE_JSON, msg.depth);
        msg.depth.ts_received = ts_received;
        msg.depth.ts_parsed = mde::core::Clock::now();

        // Spin until enqueue succeeds (queue might be full briefly)
        while (!queue->try_push(std::move(msg))) {}
    }

    done.store(true, std::memory_order_relaxed);
    consumer.join();

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        state.counters["p50_ns"] = latencies[latencies.size() * 50 / 100];
        state.counters["p99_ns"] = latencies[latencies.size() * 99 / 100];
        state.counters["p999_ns"] = latencies[latencies.size() * 999 / 1000];
        state.counters["max_ns"] = latencies.back();

        auto sum = std::accumulate(latencies.begin(), latencies.end(), 0LL);
        state.counters["avg_ns"] = static_cast<double>(sum) / latencies.size();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_E2E_TwoThread)->UseRealTime();

} // namespace

BENCHMARK_MAIN();
