#!/usr/bin/env bash
# scripts/aws/run-probe.sh — runs ON the instance (Amazon Linux 2023).
# Compiles the self-contained probe and runs it bare (shape) and under `perf stat`
# (counters). Run via: sudo bash run-probe.sh   (perf needs root / paranoid=-1).
set -uo pipefail
LOB="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$LOB"

echo "== host =="
uname -m
grep -m1 'model name\|Model' /proc/cpuinfo || true
nproc | sed 's/^/cores: /'
echo "== caches =="
lscpu 2>/dev/null | grep -i cache || true
echo

echo "== build =="
g++ -O3 -std=c++20 -Iinclude tools/scale_probe.cpp src/book.cpp src/bitmap.cpp -o /tmp/scale_probe && echo "ok: $(ls -l /tmp/scale_probe | awk '{print $5}') bytes"
echo

echo "== shape (ns, no counters) =="
for n in 1000 100000 1000000; do
  /tmp/scale_probe map    "$n" 2000000
  /tmp/scale_probe ladder "$n" 2000000
done
echo

EVENTS="cycles,instructions,branches,branch-misses,L1-dcache-load-misses,LLC-load-misses,dTLB-load-misses,iTLB-load-misses"
echo "== perf stat (build-only vs build+loop; subtract to get loop counters) =="
for n in 1000 1000000; do
  for bk in map ladder; do
    for reps in 0 5000000; do
      echo "### $bk N=$n reps=$reps"
      perf stat -e "$EVENTS" /tmp/scale_probe "$bk" "$n" "$reps" 2>&1 \
        | grep -E 'ns|cycles|instructions|branches|branch-misses|L1-dcache|LLC-load|dTLB|iTLB|seconds time' || true
    done
  done
done
echo
echo "== alloc profile (-DPROBE_COUNT_ALLOCS) =="
g++ -O3 -std=c++20 -DPROBE_COUNT_ALLOCS -Iinclude tools/scale_probe.cpp src/book.cpp src/bitmap.cpp -o /tmp/sp_alloc
for bk in map ladder; do /tmp/sp_alloc "$bk" 1000000 1000000; done
echo

echo "== flamegraph (perf record -g --call-graph dwarf) =="
if command -v perf >/dev/null 2>&1; then
  if [ ! -x /tmp/FlameGraph/flamegraph.pl ]; then
    rm -rf /tmp/FlameGraph
    git clone -q --depth 1 https://github.com/brendangregg/FlameGraph /tmp/FlameGraph 2>&1 | tail -2 || true
  fi
  echo "flamegraph.pl: $([ -x /tmp/FlameGraph/flamegraph.pl ] && echo present || echo MISSING)"
  for bk in map ladder; do
    echo "--- $bk: top functions ---"
    perf record -q -F 999 -g --call-graph dwarf -o "/tmp/perf-$bk.data" /tmp/scale_probe "$bk" 1000000 3000000 >/dev/null 2>&1 || true
    perf report -i "/tmp/perf-$bk.data" --stdio --sort=symbol --no-children --percent-limit 1 2>/dev/null | grep -vE '^#|^$' | head -15 || true
    if [ -x /tmp/FlameGraph/flamegraph.pl ]; then
      perf script -i "/tmp/perf-$bk.data" > "/tmp/out-$bk.perf" 2>/dev/null || true
      /tmp/FlameGraph/stackcollapse-perf.pl "/tmp/out-$bk.perf" > "/tmp/out-$bk.folded" 2>/dev/null || true
      /tmp/FlameGraph/flamegraph.pl --title "scale_probe $bk N=1M" "/tmp/out-$bk.folded" > "/tmp/fg-$bk.svg" 2>/dev/null || true
      echo "fg-$bk.svg: $(wc -c < "/tmp/fg-$bk.svg" 2>/dev/null | tr -d ' ') bytes"
    fi
  done
else
  echo "(perf unavailable — flamegraph skipped)"
fi
echo

echo "== done =="
