#!/bin/zsh
# scripts/sweep.sh — reusable micro + parameterized benchmark capture.
# Builds, then runs the dense micro benches (BM_Book), the N×gap sweep (BM_Sweep),
# and the dense scale sweep past L1/L2 (BM_Scale). Saves a timestamped raw log.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="scripts/results/sweep-$(date +%Y%m%d-%H%M%S).txt"
mkdir -p scripts/results
cmake --build build/build/Release -j >/dev/null
./build/build/Release/lob_bench --benchmark_min_time=0.1 \
  --benchmark_filter='BM_Book|BM_Sweep|BM_Scale' 2>&1 | tee "$OUT"
echo "saved to $OUT"
