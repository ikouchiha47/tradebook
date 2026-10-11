# How to know what to optimize — big-O and beyond

A reusable guide for a systems/quant engineer, grounded in one concrete case: our
tick-ladder order book (contiguous array indexed by price + a hierarchical occupancy
bitmap) versus `std::map<price,Level>`. We measured both. The result surprises anyone who
reasons only in big-O, so the case *is* the lesson.

> Correction to earlier notes: this M4 reports `hw.cachelinesize=128` (128-byte lines) and
> `hw.pagesize=16384` (16 KiB pages), `hw.l2cachesize=4194304` (4 MiB L2). Generic x86
> "64 B lines / 4 KiB pages" figures in older drafts are wrong *here*. A `Level` (8 B)
> packs **16 per M4 line**, and a 2 MiB ladder is only 128 pages. All numbers below use
> the verified M4 facts.

## 1. Asymptotics price the slope, not the intercept

Big-O describes how cost *grows* with N. It says nothing about the *fixed* cost. Our two
structures:

- `std::map`: O(log n) — a chain of ~log2(n) pointer chases through red-black nodes.
- ladder: "O(1)" — `idx = (price - base) / tick`, a bit set/clear, a 3-level bitmap descent.

At ~200 levels the **O(log n) map beat the O(1) ladder** (add 14.9 vs 17.1 ns). Why: 200 RB
nodes × ~48 B ≈ 10 KB — entirely L1-resident — so ~8 pointer hops are all L1 hits (~4
cycles each). The ladder's "O(1)" hid a large constant: index math, a read-modify-write
bitmap bit, bounds checks, and a root→summary→leaf descent. **At small N the intercept
dominates; at large N the slope does.** Big-O is a necessary sanity check, not a decision.

## 2. A cost model (M4 magnitudes)

| Access | ~Cycles |
|---|---|
| L1 hit (128 B line) | ~4 |
| L2 hit (4 MiB) | ~12–20 |
| DRAM | ~100–300 |
| `malloc`/`free` (hot) | tens |
| Predicted branch | ~1 |
| Mispredicted branch | ~15–20 |

What actually decides map-vs-ladder:

- **Array index beats pointer chase.** `base + idx*stride` is address math plus one load —
  prefetchable, predictable. A pointer chase is a *dependent* load chain: each address is
  known only after the previous load resolves, so the hardware serializes it.
- **We touch lines, not bytes (128 B here).** A contiguous `Level` array packs 16 levels
  per line; an RB node scatters ~48 B (plus allocator metadata) across its own line(s).
  Contiguity wins — but only when the touched region is actually hot.
- **Prefetchers reward regularity.** Streaming/adjacent array touches prefetch well;
  irregular tree walks defeat them.
- **Allocation is a real cost.** The map `malloc`s/`free`s per level birth/death; the
  ladder preallocates. But preallocation *reserves the whole range* — cheap when narrow,
  wasteful when wide and sparse (mitigated by our `cold_` tail).
- **Slope vs intercept.** Model per-op cost as `intercept + slope·f(N)`. The map has a low
  intercept (cheap cached hops) and a moderate slope (depth + misses). The ladder has a
  higher intercept (index + bitmap + descent) and a near-zero slope — until its *touched*
  set leaves cache.

## 3. When O(1) loses, and how to predict the flip

The map wins while **both** hold: its working set fits L1/L2 (our 200 nodes ≈ 10 KB), and
the ladder's fixed constant exceeds cached-chase cost. The ladder wins when the tree's
`log2(N)` hops start missing (N past L1/L2) **or** the book is sparse enough that the
bitmap's skip-empties does real work.

**Crossover rule:** estimate *working set*, not N in the abstract.
`map ≈ occupancy × ~48 B/node`; `ladder ≈ range/tick × 8 B/slot`. Whichever crosses L1
(then L2) first starts paying 10–100× per touch. Our dense-scale run is the rule in
action: map add/remove 45 → 258 ns as N went 1k → 1M (tree + allocs left cache), ladder
30 → 46 ns (touches only occupied words). Depth moved less on both — few lines touched.

Subtlety the sweep taught: **touch density beats footprint.** A sparse 8 MB ladder
footprint still wins, because each op touches ~3 bitmap words + one level — while the map
touches ~log N missing nodes. Footprint is what you allocate; touches are what you pay.

## 4. Measurement first, in this order

1. **Split the pipeline; find the dominant share.** Ours: read (gzip) 85% / route 15%.
   Nothing inside the 15% matters until the 85% is addressed or accepted.
2. **Profile inside the winner.** `sample` showed ~75% in zlib `inflate_fast`. Location,
   not mechanism.
3. **Micro-benchmark to compare structures.** Same workload, both books. Shows per-op cost
   and the crossover — but *lies* by over-fitting to warm cache, fixed N, and synthetic
   patterns. Never promote a micro-win without step 4.
4. **End-to-end replay to decide.** Ours: map and ladder printed identical books inside
   the noise. The ladder ships because its worst case stopped degrading, not because
   msgs/s moved.

## 5. What to measure beyond time

Time says *that*. These say *why* (Linux `perf` names; macOS needs Xcode `xctrace` CPU
Counters or an `mperf`-style kperf wrapper — unavailable on our CLT-only box, so we
measured the cache story structurally via the scale sweep instead):

- **LLC-load-misses/op** — the crossover detector (tree leaves cache).
- **dTLB-load-misses/op** — the ladder's hidden tax on wide sparse bands.
- **branch-misses** — data-dependent tree compares vs fixed-latency `clz`/mask arithmetic.
- **cycles/op, IPC** — IPC collapse signals latency-bound stalling.
- **allocations** — the map churns per level; the ladder doesn't.

See `measurement-methodology.md` for the exact commands and the macOS blockers.

## 6. Checklists

**Should I change this data structure?**
- Dominant stage identified, and this structure is inside it?
- Profiler shows real time here, not just suspicious-looking code?
- Micro-benchmark wins under *both* warm and cold cache?
- Working-set analysis explains the gap, not just the timer?
- End-to-end replay shows the win survives?
- Complexity budget justified by a measured number?

**Did it work?**
- Same workload, machine, flags?
- p50 *and* p99 (or full sweep), not a single mean?
- Counters moved the way the theory predicted (or the structural sweep did)?
- Re-ran end-to-end, not just the micro-bench?
- Reverted cleanly if it didn't help?

## 7. The thought process, honestly (how it evolved here)

It did not start with this model. It started with "`std::map` feels expensive; an array
must be faster," then a dense bench that said the opposite, then confusion, then the
stage split that said *neither* mattered end-to-end. The model above was built backwards
from those collisions:

- **Pseudocode → bit ops felt unnatural** because the mapping wasn't explicit: a set of
  small integers *is* a word of bits; "below me" *is* a low mask; "the top survivor" *is*
  `clz`. Once each English phrase had its bit form (`bit-manipulation.md`), the hierarchy
  was just the same three moves one level up. Unnatural was *unnamed*, not hard.
- **`clz`/`ctz` intuition:** `clz` counts zeros *above* the top 1, so `(W-1) - clz` is that
  1's position — i.e. `floor(log2(x))`. `ctz` is the mirror (bottom 1). One instruction
  each (`LZCNT`/`TZCNT`); the "magic" is naming the position.
- **How to know what to optimize:** find the dominant share first (stage split), then ask
  *which regime* each structure wins by comparing working set to cache — not by comparing
  big-O letters. Change the structure only when the sweep shows a slope you can own, and
  confirm end-to-end. If the macro number can't move, say so and ship the better shape.

## 8. Document wrong assumptions honestly

"O(1) ladder beats O(log n) map" was written down, measured, and falsified at small N —
and that note is worth more than the fix, because it records *the boundary*. Keep a ledger
(`optimization-journey.md` § Wrong assumptions): the assumption, the measurement, the N /
density / cache condition where it broke. Future-you otherwise re-derives the same wrong
intuition. State it, measure it, bound it.
