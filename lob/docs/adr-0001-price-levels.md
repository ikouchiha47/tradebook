# ADR-0001 — Price-level storage: tree → tick ladder

Status: proposed (targets P1.5). Supersedes nothing; extends ADR-0002 of the original
plan (which said "BTreeMap first, bench a ladder second" — this is that bench/decision).

## 1. Context — what we have today

The book (`include/book.h`, `src/book.cpp`) keeps two structures:

```cpp
struct Order { uint64_t ref; int64_t price; uint32_t qty; char side; };
struct Level { uint32_t total = 0; uint32_t count = 0; };

NasdqBook {
  std::unordered_map<uint64_t, Order> orders;  // ref -> live order
  std::map<int64_t, Level> bids, asks;         // price -> aggregate
  TopOfBook cached;                            // best bid/ask snapshot
  Counters stats;
};
```

- `orders` is a **hash** (ref → order): only ever looked up by id, never ordered.
- `bids`/`asks` are a **red-black tree** (`std::map`): keyed by price, ordered so
  "best" = `rbegin()` (bid) / `begin()` (ask).
- Single-symbol per replay (the book has no `locate` dimension; the driver filters).

## 2. Vocabulary (the terms that keep tripping us up)

- **price (value):** `int64` in ITCH raw units = dollars × 10,000. `$141.00 = 1410000`.
  It is a *value* with dollar meaning.
- **tick / increment:** the smallest price step. For equities ≥ $1 the market moves in
  **pennies** = 100 raw units; sub-$1 stocks use 1 unit ($0.0001). The ladder indexes by
  the *real increment*, not the raw unit (else 99 empty slots per penny).
- **base:** the lowest price the ladder covers (window bottom). Chosen, not derived.
- **index:** `(price - base) / increment` — the array slot for a price.
- **band / window:** `[base, base + slots × increment)` — the covered price range.
- **Level {total, count}:** one row per *price*: summed shares + number of orders there.
  Orders are NOT stored here; they live in `orders` by ref.
- **best price:** highest occupied bid / lowest occupied ask.
- **dense vs sparse:** dense = prices packed (every penny near the touch occupied);
  sparse = big empty gaps between populated levels. Real books are dense at the touch,
  sparse deeper.

## 3. Measured evidence (real replay + micro benchmarks)

From `PERFORMANCE.md` (M4, Release, real `07302019.NASDAQ_ITCH50.gz`, one symbol):

| op | ns | note |
|---|---|---|
| `best()` | 0.32 | cached snapshot — free |
| `reduce` (partial) | 3.33 | two lookups, no node alloc |
| `add` | 14.2 | tree + hash node insert + refresh |
| `remove` | 17.8 | node erase + refresh |
| `replace` | 35.9 | remove + add |
| `depth(5)` | 42.1 | two vector allocations per call |

Macro: 11.16M msgs/s, but **~75% is zlib inflate**; parse+book is ~13–15%.

**Conclusion:** the **read/reduce path is near-free**; the **writes are the compute
hotspot** — `std::map`/`unordered_map` **node allocations + pointer chasing**. The
system bottleneck is gzip, but the book's own cost is dominated by node writes.

## 4. Problem

`std::map` pays, per new price: one node `malloc`, O(log n) comparisons, and a
pointer-chased descent — all cache-hostile. `unordered_map` for levels would remove the
ordering needed for "best". We want O(1), allocation-free price access while keeping
"best" cheap.

## 5. Decision

Adopt a **tick ladder**: a direct-indexed array keyed by price index, plus a cold
**overflow `std::map`** for out-of-band prices.

```cpp
struct Level { uint32_t total = 0, count = 0; };

class PriceLadder {              // one side (bids or asks)
  int64_t base_, inc_; size_t slots_; bool is_bid_;
  std::vector<Level> hot_;       // index = (px - base_)/inc_  (the band)
  std::map<int64_t, Level> cold_;   // out-of-band overflow (rare)
  int64_t best_ = 0;             // cursor
  bool in_band(int64_t px) const {
    return px >= base_ && static_cast<size_t>((px - base_) / inc_) < slots_;
  }
 public:
  Level& at(int64_t px) {
    if (in_band(px)) return hot_[(px - base_) / inc_];  // O(1), no alloc
    return cold_[px];                                   // O(log n), rare
  }
  int64_t best() const { return best_; }
};
```

`orders` **stays a hash** — refs are sparse 64-bit ids, not indexable.

## 6. Alternatives considered

| option | in-band | best | alloc/op | verdict |
|---|---|---|---|---|
| `std::map` (status quo) | O(log n) | O(1) edge | node per price | correct, slow writes |
| `unordered_map` levels | O(1) | O(levels) scan | node | no ordering → rejected |
| B-tree / skip list | O(log n) | edge | node | fewer misses than RB, still nodes |
| **band ladder + overflow** | **O(1)** | cursor/rescan | **none in-band** | **chosen** |
| paged ladder | O(1) + indirection | cursor/rescan | lazy page | if band hard to bound |
| array + hierarchical bitmap | O(1) | O(1) bit-scan | none | `hsdxpro`; overkill for us now |

Prior art: `KareemJandali/itchbook` (dense ladder `vector<Level>` + `overflow_` map +
intrusive lists + open-addressed refmap), `hsdxpro/matching-engine` (u16 ladder +
hierarchical bitmap), `AsthaMishra/matching-engine` (`Vec<Option<PriceLevel>>` + u64
bitboard + ITCH replay).

## 7. Consequences

- **Speed:** in-band `add`/`remove` become index math → no node alloc, cache-friendly;
  expect the 14/18 ns write cost to drop.
- **Memory:** `slots × sizeof(Level) × 2 sides`. ±10 % of $141 at pennies ≈ 2,820 slots
  ≈ 22 KB per symbol — trivial.
- **Band must be chosen:** from LULD bands, reference ± X %, or the day's range. Width
  sets the index type (`uint16_t` if ≤ 65,536 slots, else `uint32_t` or paged).
- **Out-of-band policy:** overflow `std::map` (cold). A 25 % crash lands there until a
  rebase; `itchbook` does exactly this.
- **Off-grid prices:** a price not a multiple of `increment` (sub-penny) must go to the
  overflow map, or use a per-symbol increment.
- **Best-price maintenance:** a cursor is not enough when the best level empties — need a
  bounded rescan (or a bitmap, `hsdxpro`-style). This is the one genuinely new piece.
- **Extra arithmetic:** one `(px - base)/inc` per access (a multiply-by-constant, or a
  shift if `increment` is a power of two). `hsdxpro` avoids it by making price = index.

## 8. What we have vs what we're doing

| | today | target (P1.5) |
|---|---|---|
| levels | `std::map<int64_t, Level>` | `vector<Level>` band + `map` overflow |
| level access | O(log n), node alloc | O(1) index, no alloc in-band |
| best | tree edge (`rbegin`) | cursor + rescan (or bitmap) |
| orders | `unordered_map<ref, Order>` | unchanged (hash) |
| API | `add/reduce/remove/replace/best/depth` | **unchanged** (drop-in) |
| price | `int64` value (dollars×10000) | unchanged; index derived on access |

## 9. Migration plan

1. Implement `PriceLadder` behind `NasdaqBookParser` keeping the same public API.
2. Add a compile/run flag to select `map` vs `ladder` for A/B comparison.
3. Benchmark head-to-head on the real replay (macro msgs/s + micro add/reduce/best).
4. Tests: lifecycle (A→E→X→D), out-of-band (25 % drop), alloc-free in-band add.
5. Flip the default only if the ladder wins; keep the map path for the comparison story.
