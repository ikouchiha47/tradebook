# Book — function contracts (NasdaqBookParser, book.h / book.cpp)

State lives in one `NasdaqBook` (field tables: models.md):
`orders` (ref→Order), `bids`/`asks` (price→Level), `cached` TopOfBook, `stats` Counters.

Rules for every mutator:
- Update the order map AND the level map on the same call — never one alone.
- After touching a price, restamp `cached` (best bid = top of `bids`, best ask = top of `asks`).
- Never throw, never abort the fold. Bad input → bump a `Counters` field and return false.

## Containers and ordering (why these types)
- `bids` / `asks` are `std::map<int64_t, Level>` — a red-black tree kept sorted by price
  key automatically on every insert/erase. **Never call `sort()`; the map is always ordered.**
  Sorted is required because "best" is an ordering query: best bid = highest price,
  best ask = lowest price. `rbegin()` = highest bid, `begin()` = lowest ask, both O(1) to read.
- `orders` is `std::unordered_map<uint64_t, Order>` — only ever looked up by `ref`
  ("give me order X"), never iterated in order, so hashing is the right fit.
- Using an unordered map for levels would force a full scan of every level on each
  `best()` call to find the max/min — O(levels) per query instead of a tree-edge peek.
- Cost of `std::map`: nodes are heap-allocated and pointer-chased, so inserts are not
  cache-friendly (O(log n) each). Fine for thousands of levels per symbol. If `perf`
  flags it, the alternative is a tick-indexed array ladder (index = price, sorted by
  construction, O(1) everything) — deferred to the P1.5 bench, needs a bounded price range.

## Counters — who bumps what (and who must NOT)
Ownership is exclusive. A counter describes a *problem*, so only the path that
detects that problem touches it.

| counter | bumped by | must NOT be bumped by |
|---------|-----------|-----------------------|
| `unknown_ref` | `reduce`/`remove`/`replace` when the ref is absent | `add`, `best`, `depth` |
| `clamped` | `reduce`/`remove` when requested qty > resting qty | `add` |
| `crossed` | any mutator, after `refresh()`, if `bid_px >= ask_px` and both > 0 | reads |

A clean `add` bumps **none** of them. Any helper that increments counters outside
these paths is wrong (e.g. an `onAdd()` that bumps all three inflates the telemetry
into lies). Keep counter bumps inline at the exact detection site.

## bool add(AddOrder& o) — file one birth (A/F)
Returns true if the order was filed; false if `o.side` is not `'B'` or `'S'`.

1. If `o.side != 'B' && o.side != 'S'` → `return false;` (do not touch either map).
2. If `orders` already contains `o.ref` → unwind the old entry first:
   `map[old.price].total -= old.qty; --map[old.price].count;` erase the level if count hits 0.
3. Build `Order{ ref = o.ref, price = o.price_ticks, qty = o.shares, side = o.side }`.
4. `orders[o.ref] = order;`
5. Pick the map: `'B'`→`bids`, `'S'`→`asks`. Then:
   `map[order.price].total += order.qty;`
   `map[order.price].count += 1;`   ← must not be skipped
6. Restamp `cached` if `order.price` is now the best bid (highest) or best ask (lowest).
7. `return true;`

## bool reduce(uint64_t ref, uint32_t qty) — subtract from order + level (E/X)
Returns true if the reduction was applied; false if `ref` is unknown.

1. `auto it = orders.find(ref);` if `it == orders.end()` → `stats.unknown_ref++; return false;`
   (E carries no price, so an unknown ref has no level to touch.)
2. `uint32_t take = std::min(qty, it->second.qty);`
   If `qty > it->second.qty` → `stats.clamped++;` (never let uint32 wrap below zero).
3. `it->second.qty -= take;` and `map[price].total -= take;`
4. If `it->second.qty == 0`: erase the order, `--map[price].count;`
   if `map[price].count == 0`: erase the level, then restamp `cached`.
5. `return true;`

## bool remove(uint64_t ref) — full delete (D)
Returns true if the order existed; false otherwise.
Implement as `reduce(ref, UINT32_MAX)` then delete the order entry — one code path, not two.
Unknown ref → same `unknown_ref++` and false.

## bool replace(Replace& rep) — atomic remove-old + add-new (U)
Returns true if `old_ref` existed; false if it was missing (anomaly counted, new still filed).

1. Look up `old_ref` in `orders`; remember its `side` (the new order keeps the old side).
   If missing → `stats.unknown_ref++;` (continue anyway).
2. `remove(old_ref)` — level total/count drop for the old price.
3. `add({ ref=new_ref, side=old side, price=rep.price, qty=rep.shares })` — new id, loses queue spot.
4. `return old_existed;`

## TopOfBook best() const — O(1) read
Returns `cached` by value: `{bid_px, bid_sz, ask_px, ask_sz}`; zeros mean "no quote on that side".
Never touches the maps.

## Depth depth(size_t n) const — O(n) read
Returns `{bids, asks}`, each a vector of `LevelRow{price, total, count}`, best-first
(highest bid, lowest ask), at most `n` rows per side. Built by walking the maps from the edge.

## Invariants (assert in tests)
- `bid_px < ask_px` while both are nonzero.
- No level survives with `count == 0` or `total == 0`.
- Per side: sum of `Level.total` == sum of `Order.qty`.
- `orders.size()` == number of live orders; every `orders` entry has a matching level contribution.
