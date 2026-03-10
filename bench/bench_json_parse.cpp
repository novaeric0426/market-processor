// Benchmark: simdjson vs nlohmann/json depth update parsing
//
// Measures JSON parsing throughput for Binance depth update messages.
// Compares simdjson (primary parser) against nlohmann/json (fallback).

#include "feed/depth_parser.h"
#include "core/types.h"

#include <benchmark/benchmark.h>
#include <nlohmann/json.hpp>
#include <simdjson.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// Realistic Binance depth update JSON (20 levels per side)
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

// Combined stream format (multi-symbol subscription)
const std::string COMBINED_JSON = R"({"stream":"btcusdt@depth@100ms","data":)" + SAMPLE_JSON + "}";

// ---- simdjson benchmarks ----

void BM_SimdjsonParse(benchmark::State& state) {
    mde::feed::DepthParser parser;
    mde::core::DepthUpdate update;

    for (auto _ : state) {
        benchmark::DoNotOptimize(parser.parse(SAMPLE_JSON, update));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * SAMPLE_JSON.size());
}
BENCHMARK(BM_SimdjsonParse);

void BM_SimdjsonParseCombined(benchmark::State& state) {
    mde::feed::DepthParser parser;
    mde::core::DepthUpdate update;

    for (auto _ : state) {
        benchmark::DoNotOptimize(parser.parse(COMBINED_JSON, update));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * COMBINED_JSON.size());
}
BENCHMARK(BM_SimdjsonParseCombined);

// ---- nlohmann/json benchmark ----

namespace {

double fast_atof(std::string_view sv) {
    char buf[32];
    auto len = std::min(sv.size(), sizeof(buf) - 1);
    std::memcpy(buf, sv.data(), len);
    buf[len] = '\0';
    return std::strtod(buf, nullptr);
}

bool nlohmann_parse(const std::string& json, mde::core::DepthUpdate& out) {
    auto doc = nlohmann::json::parse(json, nullptr, false);
    if (doc.is_discarded()) return false;

    // Handle combined stream format
    if (doc.contains("data")) {
        doc = doc["data"];
    }

    if (!doc.contains("e") || doc["e"] != "depthUpdate") return false;

    out.event_time_ms = doc["E"].get<uint64_t>();
    out.first_update_id = doc["U"].get<uint64_t>();
    out.last_update_id = doc["u"].get<uint64_t>();
    out.symbol = doc["s"].get<std::string>();

    out.bid_count = 0;
    for (auto& level : doc["b"]) {
        if (out.bid_count >= mde::core::DepthUpdate::MAX_LEVELS) break;
        out.bids[out.bid_count].price = fast_atof(level[0].get<std::string>());
        out.bids[out.bid_count].quantity = fast_atof(level[1].get<std::string>());
        ++out.bid_count;
    }

    out.ask_count = 0;
    for (auto& level : doc["a"]) {
        if (out.ask_count >= mde::core::DepthUpdate::MAX_LEVELS) break;
        out.asks[out.ask_count].price = fast_atof(level[0].get<std::string>());
        out.asks[out.ask_count].quantity = fast_atof(level[1].get<std::string>());
        ++out.ask_count;
    }

    return true;
}

} // namespace

void BM_NlohmannParse(benchmark::State& state) {
    mde::core::DepthUpdate update;

    for (auto _ : state) {
        benchmark::DoNotOptimize(nlohmann_parse(SAMPLE_JSON, update));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * SAMPLE_JSON.size());
}
BENCHMARK(BM_NlohmannParse);

void BM_NlohmannParseCombined(benchmark::State& state) {
    mde::core::DepthUpdate update;

    for (auto _ : state) {
        benchmark::DoNotOptimize(nlohmann_parse(COMBINED_JSON, update));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * COMBINED_JSON.size());
}
BENCHMARK(BM_NlohmannParseCombined);

} // namespace

BENCHMARK_MAIN();
