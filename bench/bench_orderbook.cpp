// Benchmark: Order book update performance
//
// Measures order book apply_update, aggregator, and signal detection throughput.

#include "engine/order_book.h"
#include "engine/aggregator.h"
#include "engine/signal_detector.h"
#include "core/types.h"

#include <benchmark/benchmark.h>

#include <random>
#include <cmath>

namespace {

// Generate a realistic depth update with N levels per side
mde::core::DepthUpdate make_depth_update(size_t levels, uint64_t seq,
                                          std::mt19937& rng) {
    mde::core::DepthUpdate update;
    update.symbol = "BTCUSDT";
    update.event_time_ms = 1672531200000 + seq;
    update.first_update_id = seq;
    update.last_update_id = seq;

    std::uniform_real_distribution<double> price_dist(97000.0, 98000.0);
    std::uniform_real_distribution<double> qty_dist(0.001, 5.0);
    // 10% chance of zero qty (removal)
    std::uniform_real_distribution<double> removal_dist(0.0, 1.0);

    auto count = static_cast<uint16_t>(
        std::min(levels, static_cast<size_t>(mde::core::DepthUpdate::MAX_LEVELS)));

    update.bid_count = count;
    update.ask_count = count;

    double mid = price_dist(rng);
    for (uint16_t i = 0; i < count; ++i) {
        double offset = (i + 1) * 0.10;
        update.bids[i].price = mid - offset;
        update.bids[i].quantity = (removal_dist(rng) < 0.1) ? 0.0 : qty_dist(rng);
        update.asks[i].price = mid + offset;
        update.asks[i].quantity = (removal_dist(rng) < 0.1) ? 0.0 : qty_dist(rng);
    }

    return update;
}

// ---- Order book apply_update ----

void BM_OrderBookUpdate(benchmark::State& state) {
    const auto levels = static_cast<size_t>(state.range(0));
    std::mt19937 rng(42);
    mde::engine::OrderBook book("BTCUSDT");

    uint64_t seq = 1;
    for (auto _ : state) {
        auto update = make_depth_update(levels, seq++, rng);
        book.apply_update(update);
        benchmark::DoNotOptimize(book.best_bid_price());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderBookUpdate)->Arg(5)->Arg(10)->Arg(20)->Arg(50);

// ---- Order book queries after build-up ----

void BM_OrderBookQueries(benchmark::State& state) {
    std::mt19937 rng(42);
    mde::engine::OrderBook book("BTCUSDT");

    // Build up a realistic book with 200 updates
    for (uint64_t i = 1; i <= 200; ++i) {
        book.apply_update(make_depth_update(20, i, rng));
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(book.best_bid_price());
        benchmark::DoNotOptimize(book.best_ask_price());
        benchmark::DoNotOptimize(book.spread());
        benchmark::DoNotOptimize(book.mid_price());
        benchmark::DoNotOptimize(book.bid_vwap(5));
        benchmark::DoNotOptimize(book.ask_vwap(5));
        benchmark::DoNotOptimize(book.total_bid_qty());
        benchmark::DoNotOptimize(book.total_ask_qty());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderBookQueries);

// ---- Aggregator update ----

void BM_AggregatorUpdate(benchmark::State& state) {
    std::mt19937 rng(42);
    mde::engine::OrderBook book("BTCUSDT");
    mde::engine::Aggregator agg(5, 20);

    // Build up a realistic book
    for (uint64_t i = 1; i <= 100; ++i) {
        book.apply_update(make_depth_update(20, i, rng));
    }

    uint64_t seq = 101;
    for (auto _ : state) {
        book.apply_update(make_depth_update(20, seq++, rng));
        agg.update(book);
        benchmark::DoNotOptimize(agg.snapshot());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AggregatorUpdate);

// ---- Signal detection ----

void BM_SignalDetection(benchmark::State& state) {
    std::mt19937 rng(42);
    mde::engine::OrderBook book("BTCUSDT");
    mde::engine::Aggregator agg(5, 20);

    uint64_t signal_count = 0;
    mde::engine::SignalDetector detector([&](const mde::engine::Signal&) {
        ++signal_count;
    });
    detector.add_condition({mde::engine::SignalType::SPREAD_WIDE, 50.0});
    detector.add_condition({mde::engine::SignalType::IMBALANCE_BID, 0.7});
    detector.add_condition({mde::engine::SignalType::IMBALANCE_ASK, 0.7});
    detector.add_condition({mde::engine::SignalType::PRICE_DEVIATION, 0.001});

    // Build up a realistic book
    for (uint64_t i = 1; i <= 100; ++i) {
        book.apply_update(make_depth_update(20, i, rng));
        agg.update(book);
    }

    uint64_t seq = 101;
    for (auto _ : state) {
        book.apply_update(make_depth_update(20, seq++, rng));
        agg.update(book);
        detector.evaluate("BTCUSDT", book, agg.snapshot());
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["signals_fired"] = signal_count;
}
BENCHMARK(BM_SignalDetection);

} // namespace

BENCHMARK_MAIN();
