#!/bin/zsh
# scripts/profile.sh — sample-based profile of a live bounded replay.
# Env: LIMIT (default 20M msgs). Saves the native `sample` output (macOS has no perf).
set -euo pipefail
cd "$(dirname "$0")/.."
LIMIT="${LIMIT:-20000000}"
OUT="scripts/results/sample-$(date +%Y%m%d-%H%M%S).txt"
mkdir -p scripts/results
cmake --build build/build/Release -j >/dev/null
./build/build/Release/lob-replay --limit "$LIMIT" --file ../07302019.NASDAQ_ITCH50.gz >/dev/null 2>&1 &
PID=$!
sleep 1
sample "$PID" 4 -file "$OUT"
wait "$PID" || true
echo "saved to $OUT"
