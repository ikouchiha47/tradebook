# Optimization journey — map vs tick-ladder, step by step with results

The full flow that produced our conclusions: what we built, what we measured, what broke,
what we changed, and what it cost. Every step carries its numbers. Machine: Apple M4,
Release (`-O3 -DNDEBUG`), Google Benchmark, unless noted.

## Step 0 — the starting point: one book, a `std::map`

`NasdqBook` stores levels in `std::map<price,Level>` plus an `unordered_map<ref,Order>`.
Healthy micro numbers on a dense 200-level bench book (`BM_Book*`):

```
add 14.9  reduce 3.85  remove 18.4  replace 37.0  best 0.321  depth(5) 43.6   (ns/op)
```

The compute hotspot was map-node insert/erase + pointer chasing. So ADR-0001 said: try a
tick ladder (array indexed by price + a bit per slot). Assumption: **O(1) beats O(log n)**.
(That assumption gets humbled in Step 1.)

## Step 1 — build the ladder; the dense A/B says the map wins

Same template, `NasdqBookWithLadder` (band base/inc/slots + `cold_` map for the tail).
Dense 200-level book, both books:

```
op        map      ladder
add       14.9     17.1      ← map
reduce     3.85     5.74     ← map
remove    18.4     20.0      ~tie
replace   37.0     26.6      ← ladder
best       0.321    0.317    tie
depth      43.6     31.9     ← ladder (was 105 before the top-from-best fix)
```

**Why the map won.** Three reasons, biggest first:
1. **The bench was the map's best case.** 200 nodes × ~48 B ≈ 10 KB — entirely L1-resident.
   Its `log2(200) ≈ 8` pointer hops are all L1 hits (~4 cycles each). The ladder's "O(1)"
   hid a bigger constant: index math + an occupancy-bit write + a root→summary→leaf descent.
2. **`refresh()` descended per message — twice.** Every add/reduce/remove recomputed best;
   the map's `bids.rbegin()->first` is a tree-edge pointer (~free), the ladder's
   `occ_.highest()` is a full 3-level `clz` descent. That's the add (15→17) and reduce
   (3.9→5.7) gap.
3. **Bitmap maintenance** (`in_hot` math + `occ_.set`) the map doesn't pay.

**Wrong assumption #1 (documented):** "O(1) ladder beats O(log n) map." Wrong at small N,
because big-O prices the *slope* and ignores the *intercept*. At ~200 levels the intercept
dominates. See `algorithm-design-guide.md`.

What the ladder already won (and why): **replace** (26.6 vs 37.0) — the map's tree erase
(node free + rebalance) is expensive; the ladder's erase is a bit-clear. **depth** (31.9 vs
43.6) — after starting the walk at the best occupied slot instead of scanning the band
(was 105 ns).

## Step 2 — cache `best_bid_` / `best_ask_`; rescan only when the best level clears

Two coupled changes, because Step 1 showed the per-message descent was the tax:
1. **Compare-and-update on add.** `if (px > best_bid_) best_bid_ = px;` (and the ask mirror).
   No descent on insert — a couple of integer compares. Both books got this.
2. **Rescan (descent) only when the best level empties.** On `remove_order`, if the emptied
   level *was* the best, rescan (`bids.highest()` / tree edge); otherwise best is untouched.
   Partial `reduce` never moves best by construction (the level still rests).

Result (dense micro, ladder):

```
reduce  5.74 → 4.01      (no more per-message descent)
add     17.1 → 15.6      (no more per-message descent)
```

And the map's `best` stays 0.32 ns (a cached struct copy). `reduce` is now ~3–4 ns on both
books — two container touches and no allocation.

**Why rescan-only-on-clear is correct.** The best can only *leave* when its level is gone.
Any other event (add below the best, reduce of a resting order, removal of a worse level)
cannot move the touch. So the invariant "cached best == true best" is preserved by
checking exactly one condition: *did the best level empty?* See `best-price-rescan.md`.

## Step 3 — widen the sweep: dense vs sparse, small vs large

The dense A/B only proved the ladder "loses its worst case." Parameterize: levels-per-side
N = 100 / 1k / 10k, tick gap 100 (dense) / 1k / 5k (sparse). `scripts/sweep.sh` →
`BM_Sweep*`.

**Add/remove a NEW level (create + destroy):**

```
N=100    map 43.7   ladder 30.9
N=1000   map 54.9   ladder 32.2
N=10000  map 71.4   ladder 32.5
```

**The ladder is flat in N; the map grows** — O(log N) tree depth + a `malloc`/`free` per
birth and death + cache-missing hops. This is the *scale-invariant win*, and it widens
(1.4× → 2.2×). Same shape at every gap: create/destroy cost is N-driven, not gap-driven.

**depth(5) — the gap decides (before the jump):**

```
gap 100   map 46.7   ladder 38.9    ← ladder
gap 1000  map 45.8   ladder 57.4    ← map
gap 5000  map 46.2   ladder 205     ← map, 4×
```

**The map is flat in the gap** (tree walk is N-driven). **The ladder grows with the gap**
because `top()` started at the best and then *walked empty slots* to collect five levels.
Dense → adjacent → wins. 50-slot gaps → 205 ns. This single benchmark reading is what sent
us to Step 4.

## Step 4 — the occupied→occupied jump (`prev_occupied` / `next_occupied`)

The sweep said the ladder's depth was a *slot walk*. The fix: never step on an empty slot.
Emit the best level, then **jump straight to the next occupied slot** via the hierarchy:

```
leaf word below the anchor → empty?
  → climb to the summary word → nearest non-empty leaf word below
    → descend → its top slot
```

Same mask + `clz`/`ctz` idiom at two tiers (`Occupancy::prev_occupied`/`next_occupied`).
See `prev-next-occupied.md` (what invokes it, pseudocode, cost) and the worked trace in
`bitmap-walkthrough.md`.

Result — the 205 ns is gone:

```
gap 100   39 → 43–53     gap 1000  57 → 41–45     gap 5000  205 → 44–47
```

**Depth is now flat in both N and gap** (~40–47 ns everywhere); the map still climbs with N
(44 → 58). The former density crossover is gone — both ladder ops win or tie.

## Step 5 — push past cache: dense scale to N = 1M

`scripts/sweep.sh` → `BM_Scale*` (dense, N = 1k / 10k / 100k / 1M). This is the cache cliff,
measured without hardware counters:

```
depth:          map 48.1 → 55.7 → 58.1 → 60.2      ladder 43.0 → 42.5 → 42.2 → 44.1
add/remove:     map 45.4 → 50.2 → 84→182 → 255→258  ladder 29.4 → 31.3 → 31.5 → 39→46
```

**The map's structural ops explode once the tree leaves cache** (×5–6 by N=1M: node allocs
plus DRAM-bound pointer chases). **The ladder stays flat** (index + bit flip; it touches
only occupied words, never the dead range). The depth gap is smaller because both touch
few lines; add/remove is where the tree's allocations bite.

Note run-to-run wobble at large N on the map side (100k read 84 ns one run, 182 the next).
That instability *is* part of the result: once the working set exceeds cache, cost depends
on allocator/cache state instead of the op. The ladder's numbers are stable because its
touched set is deterministic. We present ranges, not single digits, at large N.

Working-set math that predicts it: map ≈ N × ~48 B (10k nodes ≈ 0.5 MB, 1M ≈ 48 MB);
ladder ≈ 2N slots × 8 B (1M levels ≈ 16 MB hot_) — but the ladder only *touches* occupied
lines + ~3 bitmap words per op, so *touch density* beats *footprint*. See
`algorithm-design-guide.md` (slope vs intercept) and `measurement-methodology.md`.

## Step 6 — real-feed A/B validates (identical output)

`lob-replay --book ladder|map` on the same 10M-message ARGX slice:

```
book=map      9.43M msgs/s   route 0.155s   best 141.0200 x 200 / 141.3900 x 200
book=ladder   9.70M msgs/s   route 0.150s   best 141.0200 x 200 / 141.3900 x 200
counters: unknown_ref=0 clamped=0 crossed=0 parse_errs=0   (both)
```

**Identical book, identical counters.** gzip is 85% of the run, so the book sits in the
noise end-to-end — exactly as the Step-0 profiler predicted. The ladder's win is
structural (no alloc, bounded worst case, scale-invariant), not a macro speedup. We ship
it as the default because it costs nothing and its worst case stopped degrading.

## Wrong assumptions — the honest ledger

1. "O(1) ladder beats O(log n) map." → Wrong at small N (intercept dominates). Right once
   the tree leaves cache or books go sparse (slope dominates).
2. "`top()` from best is enough for depth." → Wrong; it gap-walks. Fixed by the
   occupied→occupied jump.
3. "A dense 200-level bench decides the book." → Wrong; it was the map's best case. The
   N×gap sweep + the 1M scale run were needed to find where each structure wins.
4. "`px` in `Occupancy` is a price." → Wrong; it's a **slot**. Renamed (`px` → `slot`).
5. "Hardware cache/branch counters are available on this box." → Wrong: no `perf`/
   `valgrind` on darwin, and `/usr/bin/xctrace` refuses without full Xcode. Measured the
   cache story empirically instead (scale sweep); the Linux `perf stat` path is in
   `scripts/counters.sh` for CI.
6. "Decompress-once will reveal the book win." → Partly wrong: gunzip owns 85%, and the
   post-decompression win would still be small in msgs/s. Deferred; the A/B already shows
   the ladder costs nothing to adopt.

## Reproduce

```
./scripts/sweep.sh       # micro + N×gap sweep + dense scale to 1M  → scripts/results/
LIMIT=10000000 ./scripts/replay-ab.sh   # hyperfine map-vs-ladder end-to-end
./scripts/profile.sh     # sample-based profile of a bounded replay
./scripts/counters.sh    # PMU blocker proof + Linux path + empirical cache reading
```

Full numbers and method notes: `benchmarks/PERFORMANCE.md`, `benchmarks/tick-ladder-vs-map.md`,
`benchmarks/tick-ladder-sweep.md`. Primitives: `bit-manipulation.md`.
Structure: `hierarchical-bitmap.md`. Traces: `bitmap-walkthrough.md`, `ladder-casebook.md`.
Reasoning: `best-price-rescan.md`, `prev-next-occupied.md`, `algorithm-design-guide.md`,
`measurement-methodology.md`.
