#!/bin/bash
# Run all benchmarks and output results in both console and JSON format.
# Usage: ./scripts/run_bench.sh [--json]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/bench"
RESULTS_DIR="$PROJECT_DIR/bench/results"

# Build if needed
if [ ! -d "$BUILD_DIR" ]; then
    echo "Building benchmarks..."
    cmake --preset bench -S "$PROJECT_DIR"
    cmake --build "$BUILD_DIR" --parallel
fi

mkdir -p "$RESULTS_DIR"

BENCHMARKS=(
    bench_json_parse
    bench_queue
    bench_orderbook
    bench_e2e
)

OUTPUT_FORMAT="console"
if [ "${1:-}" = "--json" ]; then
    OUTPUT_FORMAT="json"
fi

TIMESTAMP=$(date +%Y%m%d_%H%M%S)

for bench in "${BENCHMARKS[@]}"; do
    BENCH_BIN="$BUILD_DIR/$bench"
    if [ ! -f "$BENCH_BIN" ]; then
        echo "SKIP: $bench (binary not found)"
        continue
    fi

    echo "========================================"
    echo "Running: $bench"
    echo "========================================"

    if [ "$OUTPUT_FORMAT" = "json" ]; then
        "$BENCH_BIN" \
            --benchmark_format=json \
            --benchmark_out="$RESULTS_DIR/${bench}_${TIMESTAMP}.json" \
            --benchmark_out_format=json \
            2>/dev/null
        echo "  → saved to $RESULTS_DIR/${bench}_${TIMESTAMP}.json"
    else
        "$BENCH_BIN" --benchmark_format=console
    fi
    echo ""
done

echo "Done. Results saved to $RESULTS_DIR/"
