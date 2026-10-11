# AWS perf counters — the mechanism, measured (Linux)

macOS blocked hardware counters (`measurement-methodology.md` §2). We ran the probe on
AWS Linux to get them. Two architectures, both spot-cheap, both terminated on exit.
The absolute ns differ from the M4 (different cores, 2 vCPU); the **shape and the counter
mechanism corroborate** the local result.

## Setup

Self-contained probe (no data file, no zlib, no Google Benchmark):
`tools/scale_probe.cpp` — builds N levels/side (dense, 1¢ apart), loops
`{add a new ask; remove it}` and `{depth(5)}`, prints ns/op. Driven by
`scripts/aws/ec2-probe.sh <arch> [type]` (launch → install `gcc-c++`/`perf` → build → run
→ fetch → terminate), Linux side `scripts/aws/run-probe.sh`.

```
./scripts/aws/ec2-probe.sh arm64  c7g.large    # Graviton3, 2 vCPU
./scripts/aws/ec2-probe.sh x86_64 c7i.large    # Xeon Platinum 8488C (SPR), 2 vCPU
```

Hosts: c7g — L1d 64 KiB/core, L2 1 MiB/core, L3 32 MiB, 64 B line.
c7i — L1d 48 KiB, L2 2 MiB, L3 105 MiB. (Neither equals the M4: 128 B line, 4 MiB L2.)

## Shape (ns/op) — map grows, ladder flat, both arches

```
add/remove (op = add+remove pair)     map            ladder
ARM64 N=1k / 100k / 1M            139 / 438 / 608   63 / 66 / 67
x86   N=1k / 100k / 1M            127 / 410 / 551   62 / 60 / 59
depth(5)  (map and ladder both ~flat; few lines touched)
```

Same conclusion as M4: the map's structural ops scale badly with N; the ladder is
scale-invariant.

## Counters (`perf stat`, N=1M, loop isolated = build+loop − build-only; 5e6 reps = 1e7 ops)

```
ARM64 Graviton3 (c7g)          map      ladder     ratio
cycles/op                      931      223        4.2×
IPC                            1.61     3.77
L1-dcache-load-misses/op       27.2     0.006     ~4000×
dTLB-load-misses/op            59       0.006     ~9000×
branch-misses/op               0.72     0.001
ns/op (shape)                  608      67

x86_64 Sapphire Rapids (c7i)   map      ladder     ratio
cycles/op                      961      230        4.2×
IPC                            1.48     3.68
L1-dcache-load-misses/op       33.8     0.005     ~7000×
dTLB-load-misses/op            0.46     0.0002
branch-misses/op               0.0024   0.0005
ns/op (shape)                  551      59
```

Raw: `scripts/aws/results-arm64-*.txt`, `results-x86_64-*.txt`.

## What it proves

The map's ~4× cycles/op at N=1M is **memory, not arithmetic** — ~30 L1-dcache misses per
op (tree nodes + allocator metadata no longer cache-resident) and, on ARM, ~60 dTLB
misses/op, with IPC sagging to ~1.5 (latency-bound). The ladder does the same logical work
at **~0.005 misses/op** and IPC ~3.7 (well-retired). This is the exact mechanism the
dense-200 bench hid and the M4 micro numbers only implied: *the tree's cost is cache and
TLB misses per level hop; the ladder touches ~3 bitmap words + one level.*

## Caveats (what the counters also revealed)

- **LLC-load-misses is `<not supported>`** on both instances — the PMU event whitelist
  here doesn't expose it (matches `lib-2`'s note). L1d + dTLB + IPC carry the story.
- **dTLB behavior is arch-specific:** ARM map ≈ 60 dTLB-miss/op, x86 map ≈ 0.46 — x86
  TLB/THP handling hides what ARM pays. The *direction* (map ≫ ladder) holds on both.
- **branch-misses split:** ARM map ≈ 0.72/op (its predictor fights the tree) vs x86
  ≈ 0.002 — tree branch behavior is not portable; don't over-read it.
- **2 vCPU, shared/noisy neighbours, no pinning** → treat ns as ±; the shape and
  misses/op are the durable signals.
- **No M4-equivalent on AWS.** These confirm the mechanism on a *different* machine; the
  M4 numbers in `benchmarks/` remain the primary record.

## CPU profile (perf flamegraph) + allocation profile

Counters say *how much*; a flamegraph says *where*, and an allocation counter says *which
alloc*. Both were run on the same box. Artifacts: `scripts/aws/fg-arm64-{map,ladder}.svg`.

**Allocations per op** (add + remove a new level), N=1M (`-DPROBE_COUNT_ALLOCS`):
```
              allocs/op   bytes/op   what
map           2.000       88 B       order-ref map node + std::map level node
ladder        1.000       40 B       order-ref map node only (level = preallocated slot)
depth5        2.000      160 B       both — caller-side vector for bids+asks
```
Two reads: the map's *extra* per-op allocation is exactly **one tree node** (the ladder
skips it), and allocations are **constant in N** (2 vs 1 at N=1k *and* N=1M). So the
cost that grows with N is **not** allocation — it's the misses `perf stat` showed. The two
profiles interlock.

**CPU time** (`perf record -g --call-graph dwarf`, N=1M, self %):
```
map      NasdaqBookParser<NasdqBook>::add       32.6%
         NasdaqBookParser<NasdqBook>::refresh   26.7%
ladder   NasdaqBookParser<...Ladder>::depth      24.3%
         NasdaqBookParser<...Ladder>::refresh     9.1%
```
The map's CPU is *its own* tree-insert path (`add`) plus `refresh`; the ladder's is the
snapshot (`depth`). **New finding the flamegraph surfaced:** `refresh()` — the per-message
top-of-book *size* lookup (`find_level(best_px, side)`) — is ~27% of the map's CPU and ~9%
of the ladder's. Our cached-best change cached the *price*; `refresh` still resolves the
*size* every message, and that's a tree walk for the map vs index math for the ladder.
**Done** — cached the best level's size alongside its price (`where-to-look.md` §6):
N=1M add/remove map 223 → 163 ns (−27%), ladder 30 → 26 ns (−15%); real-feed output
unchanged end-to-end (the win sits inside the 15% route stage).

## Reproduce

```
./scripts/aws/ec2-probe.sh arm64
./scripts/aws/ec2-probe.sh x86_64
# local shape check (any OS):
clang++ -O3 -std=c++20 -Iinclude tools/scale_probe.cpp src/book.cpp src/bitmap.cpp -o /tmp/sp && /tmp/sp map 1000000 2000000
```
