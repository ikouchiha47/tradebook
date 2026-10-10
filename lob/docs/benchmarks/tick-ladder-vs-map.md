# Tick ladder vs std::map — measured (and why the map is winning)

A/B of the two books behind the same `NasdaqBookParser<Book>` template, on the same
benchmarks. Machine: Apple M4, Release (`-O3 -DNDEBUG`). `ns/op`, Google Benchmark.

## The numbers (`lob_bench --benchmark_filter=BM_Book`)

| op | map (`NasdqBook`) | ladder (`NasdqBookWithLadder`) | winner |
|---|---|---|---|
| add | 14.9 | 17.1 | map |
| reduce | 3.85 | 5.74 | map |
| remove | 18.4 | 20.0 | ~tie |
| replace | 37.0 | **26.6** | **ladder** |
| best | 0.321 | 0.317 | tie |
| depth(5) | 43.6 | **31.9** | **ladder** |

(depth was 105 ns for the ladder before the `top()`-from-best fix → 31.9.)

## Why the map is winning right now

Three reasons, in order of impact:

**1. The bench book is dense and tiny — the map's best case.**
```
bids: 5,000,000 … 4,990,100  (100 consecutive penny levels)
asks: 5,000,100 … 5,010,000  (100 consecutive penny levels)
band: [4,990,000, 5,010,000], inc 100, 201 slots
occupied slots = 200 / 201 ≈ 99.5%   →  zero empty gaps
tree ≈ 200 nodes × ~48 B ≈ 9.6 KB     →  fits in L1 (64 KB)
```
So the ladder's *only* edges — skip empty slots, avoid cache-missing pointer chases —
have nothing to skip and nothing to miss. A 200-node `std::map` is ~8 levels deep and
**entirely L1-resident**, so it never takes a cache miss.

**2. `refresh()` recomputes best on EVERY message — and the ladder's best is a descent.**
Every `add`/`reduce`/`remove` calls `refresh()` → `best_bid()`/`best_ask()`:
- map: `bids.rbegin()->first` — a pointer to the tree edge, ~free.
- ladder: `occ_.highest()`/`lowest()` — a full root→summary→leaf `clz`/`ctz` descent,
  **run twice per message**. That's the add (17 vs 15) and reduce (5.7 vs 3.9) gap.

This is an implementation choice, not inherent: `hsdxpro` keeps a cached `best` scalar
(`max`/`min` on level create, descent only on best-empty) and `itchbook` keeps a `cursor_`
updated on level transitions. **Neither descends per message.** We do.

**3. The ladder pays bitmap maintenance the map doesn't.**
`add_order` does `in_hot` math + `occ_.set` (branch + bit write); the map does one tree
node insert. On a small dense book that overhead isn't repaid.

## What the ladder won, and why
- **replace** (26.6 vs 37.0): `replace` = remove + add, and the map's tree **erase**
  (node dealloc + rebalance) is expensive, while the ladder's erase is a bit-clear + a
  slot reset.
- **depth(5)** (31.9 vs 43.6): after starting the walk at the best occupied slot
  (`occ_.highest()`) instead of scanning the band (*was* 105 ns). The map's `depth` walks
  5 tree nodes; the ladder now walks ~5 slots from the touch.

## Cache residency — the mechanism
```
map lookup:  ~8 pointer derefs to a node, all within ~10 KB → L1 hits, ~4-cycle latency each
ladder write: index math + one array write + one bitmap bit → also L1, but more ops per event
```
With the tree fully in L1, its nominal O(log n) beats the ladder's O(1) on *constant
factors* — the ladder's O(1) has a bigger constant (index + bitmap + descent) than a
cache-hit tree walk.

## The regime where the ladder should win (not yet measured)
The ladder's premise is a **large, sparse** book:
- thousands→tens-of-thousands of levels, so the map's tree exceeds L1/L2 and actually
  **cache-misses** on each of its ~log n derefs;
- **sparse** (gaps), so the bitmap's "skip empties" does real work.
Our bench is the exact opposite (200 levels, 99.5% dense). So this A/B proves the ladder
*loses its worst case*, not that it loses in general.

## Next (to make the comparison fair and find the crossover)
1. **Cache best in the parser** (both books): compare-and-update on add, descent only when
   the best level empties. Removes the per-message descent → closes add/reduce.
2. **Wide/sparse bench** (5k–50k levels spread out) → the map's tree misses cache; this is
   where the ladder should separate.
3. Record both, then decide which the default config uses.
