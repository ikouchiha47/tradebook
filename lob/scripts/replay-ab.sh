#!/bin/zsh
# scripts/replay-ab.sh — end-to-end map-vs-ladder A/B on real data via hyperfine.
# Env: LIMIT (default 10M msgs). Compares wall-clock; the two books must print
# identical best + counters (correctness), so this measures cost, not output.
set -euo pipefail
cd "$(dirname "$0")/.."
LIMIT="${LIMIT:-10000000}"
BIN=./build/build/Release/lob-replay
DATA="../07302019.NASDAQ_ITCH50.gz"
cmake --build build/build/Release -j >/dev/null
hyperfine --warmup 1 --runs 3 \
  "$BIN --limit $LIMIT --book map --file $DATA" \
  "$BIN --limit $LIMIT --book ladder --file $DATA"
