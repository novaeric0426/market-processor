# Benchmark Results

> Environment: Apple M4 Air (10 cores), macOS, AppleClang 17.0, Release build (-O2)
>
> All benchmarks use Google Benchmark framework with sufficient iterations for statistical stability.

## JSON Parser Comparison: simdjson vs nlohmann/json

Parsing a realistic Binance depth update (20 levels per side, ~1.1KB JSON).

| Parser | Format | Time/op | Throughput | Speedup |
|--------|--------|---------|------------|---------|
| **simdjson** | Direct | **2,569 ns** | **434 MiB/s** | **3.4x** |
| simdjson | Combined stream | 2,005 ns | 575 MiB/s | 6.1x |
| nlohmann/json | Direct | 8,854 ns | 126 MiB/s | 1.0x (baseline) |
| nlohmann/json | Combined stream | 12,248 ns | 94 MiB/s | 0.7x |

**결론**: simdjson은 direct format에서 **3.4배**, combined stream에서 **6.1배** 빠르다.
Combined stream의 경우 simdjson은 raw_json() 추출 후 inner만 재파싱하는 반면, nlohmann은 전체를 DOM으로 구성하기 때문에 차이가 더 크다.

## SPSC Lock-Free Queue

### Single-thread Push/Pop

| Payload | Time/op | Throughput |
|---------|---------|------------|
| uint64_t (8B) | **1.9 ns** | **527M ops/s** |
| QueueMessage (~2KB) | **66.4 ns** | **30 GiB/s** |

### Burst (Fill → Drain)

| Burst Size | Time/op (per element) | Throughput |
|------------|----------------------|------------|
| 64 | 1.4 ns | 736M ops/s |
| 256 | 1.7 ns | 585M ops/s |
| 1,024 | 1.8 ns | 555M ops/s |
| 4,095 | 1.8 ns | 548M ops/s |

### Cross-Thread Latency

| Percentile | Latency |
|------------|---------|
| P50 | 44.6 μs |
| P99 | 174.9 μs |
| P99.9 | 319.1 μs |

> Cross-thread latency includes OS scheduling overhead. Linux with CPU affinity는 이 수치를 크게 개선할 수 있다.

## Order Book Performance

| Operation | Levels | Time/op |
|-----------|--------|---------|
| apply_update | 5 | 2,350 ns |
| apply_update | 10 | 3,626 ns |
| apply_update | 20 | 5,536 ns |
| apply_update | 50 | 13,123 ns |
| All queries (8 calls) | - | 22,026 ns |

`std::map` 기반으로 레벨 수에 대해 O(N log N) 스케일링.

## End-to-End Pipeline Latency

전체 hot path: JSON parse → queue → order book → aggregation → signal detection.

### Single-Thread (no queue overhead)

| Stage | Time |
|-------|------|
| JSON Parse | 2,458 ns |
| Order Book Update | 125 ns |
| Aggregation | 125 ns |
| Signal Detection | < 1 ns |
| **Total** | **2,708 ns** |

파싱이 전체의 **91%**를 차지. Order book + aggregation + signal detection은 합쳐서 250ns 수준.

### Two-Thread (producer → consumer, with queue)

| Metric | Value |
|--------|-------|
| P50 | **3,292 ns** |
| P99 | 3,542 ns |
| P99.9 | 9,333 ns |
| Max | 26,667 ns |
| Avg | 3,305 ns |
| Throughput | **333K msgs/s** |

실질적인 E2E 레이턴시는 **~3μs (P50)** 수준이며, 꼬리 레이턴시(P99.9)도 **10μs 이내**로 안정적.

## Summary

| Component | Key Metric |
|-----------|------------|
| JSON Parsing (simdjson) | **2.6μs/msg**, 434 MiB/s |
| SPSC Queue | **1.9ns** push+pop (uint64), **66ns** (full message) |
| Order Book (20 levels) | **5.5μs/update** |
| E2E Pipeline (P50) | **3.3μs** |
| E2E Throughput | **333K msgs/s** |
