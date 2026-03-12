# Market Data Engine

[한국어](README.ko.md)
![alt text](<스크린샷 2026-03-12 오후 6.19.57.png>)
Low-latency market data processing pipeline built in C++17.

Receives real-time order book data from Binance WebSocket, parses with simdjson, routes through a lock-free SPSC queue, maintains per-symbol order books, computes aggregations (VWAP, spread SMA, bid-ask imbalance), and fires configurable signals — all within **~3μs end-to-end latency**.

## Architecture

```
[Binance WebSocket]
    → Feed Thread (CPU 0)
        → simdjson parse → DepthUpdate struct
        → SPSC Lock-Free Queue
    → Processing Thread (CPU 1)
        → Order Book (std::map, delta updates)
        → Aggregator (VWAP, Spread SMA, Imbalance)
        → Signal Detector (5 condition types)
    → Output
        → Binary Disk Logger (replay support)
        → HTTP /stats endpoint (JSON metrics)
        → WebSocket Server → React Dashboard
```

## Performance

> Apple M4 Air, Release build (-O2), Google Benchmark

| Component | Latency | Throughput |
|-----------|---------|------------|
| JSON Parse (simdjson) | 2.6 μs | 434 MiB/s |
| JSON Parse (nlohmann, baseline) | 8.9 μs | 126 MiB/s |
| SPSC Queue push+pop | 1.9 ns (uint64) / 66 ns (full msg) | 527M / 15M ops/s |
| Order Book Update (20 levels) | 5.5 μs | 181K ops/s |
| **E2E Pipeline (P50)** | **3.3 μs** | **333K msgs/s** |
| E2E Pipeline (P99.9) | 9.3 μs | - |

simdjson is **3.4x faster** than nlohmann/json for direct format parsing.

Full benchmark details: [docs/benchmarks.md](docs/benchmarks.md)

## Quick Start

### Docker (recommended)

```bash
docker compose -f docker/docker-compose.yml up
```

Engine runs on ports 8080 (HTTP) / 8081 (WebSocket), dashboard on port 3000.

### Build from source

```bash
# Dependencies (macOS)
brew install boost spdlog yaml-cpp simdjson nlohmann-json openssl

# Build
cmake --preset release
cmake --build build/release

# Run
./build/release/market-engine config/dev.yaml
```

### Run benchmarks

```bash
cmake --preset bench
cmake --build build/bench
./scripts/run_bench.sh
```

### Replay mode

```bash
# Record (enable in config or use prod.yaml)
./build/release/market-engine config/prod.yaml

# Replay at 5x speed
./build/release/market-engine --replay recordings/<file>.bin --speed 5x config/dev.yaml
```

## Project Structure

```
src/
├── core/          # Types, SPSC queue, clock, thread utilities
├── feed/          # WebSocket client, feed handler, depth parser
├── engine/        # Order book, aggregator, signal detector, processing thread
├── output/        # Disk logger, HTTP metrics, WebSocket server
└── replay/        # Replay engine with speed control

bench/             # Google Benchmark: json parse, queue, order book, e2e
test/              # GTest: 7 test suites, all using recorded data
dashboard/         # React + Vite: order book chart, metrics, replay controls
docs/              # Architecture, benchmark results
```

## Tech Stack

| Area | Choice | Why |
|------|--------|-----|
| Language | C++17 | `std::optional`, structured bindings, `string_view` |
| WebSocket | Boost.Beast | Cross-platform (macOS kqueue / Linux epoll) |
| JSON | simdjson (primary) | SIMD-accelerated, 3.4x faster than nlohmann |
| Queue | Custom SPSC ring buffer | Lock-free, cache-line aligned, acquire/release ordering |
| Logging | spdlog | Async mode, file rotation, level-based |
| Config | yaml-cpp | dev.yaml / prod.yaml separation |
| Dashboard | React + Vite | Lightweight, WebSocket-driven real-time UI |
| Benchmark | Google Benchmark | Per-component microbenchmarks |
| CI | GitHub Actions | Linux build + test + benchmark on every push |

## Design Decisions

- **SPSC over MPMC**: 1-producer 1-consumer pipeline; SPSC gives lowest possible latency with minimal atomic operations
- **simdjson over nlohmann/json**: Benchmarked 3.4x speedup; nlohmann kept as documented fallback
- **Binary recording format**: Zero-overhead replay without JSON re-parsing; schema-versioned header
- **CPU affinity (Linux)**: Feed on CPU 0, processing on CPU 1; reduces cache pollution and scheduling jitter
- **No exceptions on hot path**: `std::optional` return values, error codes; exceptions only at init

## Documentation

- [Architecture](docs/architecture.md) — thread model, data flow, design rationale
- [Benchmarks](docs/benchmarks.md) — detailed results with per-stage breakdown
