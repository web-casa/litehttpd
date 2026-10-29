#!/usr/bin/env bash
# litehttpd benchmark — runs on VM373 (fixed machine for consistent numbers)
# Usage: ./scripts/bench.sh [duration] [concurrency]
set -euo pipefail

DURATION=${1:-30}
CONCURRENCY=${2:-50}
URL=${BENCH_URL:-"http://127.0.0.1:8088/"}

command -v wrk >/dev/null || { echo "wrk not installed"; exit 1; }

echo "=== litehttpd benchmark ==="
echo "URL: $URL"
echo "Duration: ${DURATION}s, Concurrency: $CONCURRENCY"
echo ""

# Warmup
wrk -t2 -c10 -d5s "$URL" > /dev/null 2>&1 || true

# Run 5 times, report median
RESULTS=()
for i in 1 2 3 4 5; do
    OUT=$(wrk -t4 -c"$CONCURRENCY" -d"${DURATION}s" "$URL" 2>/dev/null | grep "Requests/sec" | awk '{print $2}')
    RESULTS+=("$OUT")
    echo "Run $i: $OUT req/s"
done

# Median
SORTED=($(printf '%s\n' "${RESULTS[@]}" | sort -n))
MEDIAN=${SORTED[2]}
echo ""
echo "=== Median: $MEDIAN req/s ==="

# Compare with baseline if exists
BASELINE_FILE="scripts/.bench-baseline"
if [ -f "$BASELINE_FILE" ]; then
    BASELINE=$(cat "$BASELINE_FILE")
    PERC=$(echo "scale=1; $MEDIAN * 100 / $BASELINE" | bc)
    echo "Baseline: $BASELINE req/s"
    echo "Change: ${PERC}%"
    # Fail if >5% regression
    if (( $(echo "$PERC < 95" | bc -l) )); then
        echo "FAIL: performance regression > 5%"
        exit 1
    fi
else
    echo "$MEDIAN" > "$BASELINE_FILE"
    echo "Baseline saved: $MEDIAN req/s"
fi
