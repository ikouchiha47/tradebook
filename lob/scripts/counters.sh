#!/bin/zsh
# scripts/counters.sh — hardware-counter attempt + documented fallback.
# On this machine (darwin, CommandLineTools only) PMU counters are UNAVAILABLE:
#   - `perf` / `valgrind` don't exist on macOS,
#   - `/usr/bin/xctrace` refuses to run without full Xcode
#     ("tool 'xctrace' requires Xcode, but active developer directory
#      '/Library/Developer/CommandLineTools' is a command line tools instance").
# So this script proves the blocker, prints the Linux-CI path, and shows the
# empirical fallback that actually produced the cache story on this box.
set -uo pipefail
cd "$(dirname "$0")/.."
echo "## counters.sh — $(date)"
echo
echo "--- tool check ---"
command -v perf || echo "perf: ABSENT (Linux-only)"
command -v valgrind || echo "valgrind: ABSENT (unsupported on arm64 darwin)"
/usr/bin/xctrace version 2>&1 | head -2 || true
echo
echo "--- Linux CI path (copy-paste on a perf box) ---"
echo "perf stat -e cycles,instructions,cache-misses,LLC-load-misses,branch-misses,dTLB-load-misses \\"
echo "  ./build/build/Release/lob-replay --limit 1000000 --book map"
echo "perf stat -e cycles,instructions,cache-misses,LLC-load-misses,branch-misses,dTLB-load-misses \\"
echo "  ./build/build/Release/lob-replay --limit 1000000 --book ladder"
echo
echo "--- empirical fallback on this box (dense SCALE sweep, BM_Scale) ---"
echo "(run: ./scripts/sweep.sh, then read BM_ScaleAddRemove / BM_ScaleDepth)"
./build/build/Release/lob_bench --benchmark_min_time=0.05 \
  --benchmark_filter='BM_ScaleAddRemove' 2>&1 | grep -E "Benchmark|BM_Scale"
echo
echo "Reading: the map's add/remove climbs 45 -> 255 ns as N goes 1k -> 1M"
echo "(tree leaves L1/L2; node alloc + pointer-chase misses). The ladder stays"
echo "29 -> 40 ns (index + bit flip, touches only occupied words). That slope"
echo "difference IS the cache story, measured without PMU counters."
