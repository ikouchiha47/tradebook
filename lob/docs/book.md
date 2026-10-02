# Book — function contracts (book.h / book.cpp)

State: `orders` (ref→Order), `bids`/`asks` (price→Level), `cached` TopOfBook, `stats` Counters.
Rule for all four mutators: update order map AND level map together, refresh `cached`
iff the touched price is the edge. Never throws; corrupt input becomes a counter bump.

## add(const AddOrder&) — file one birth (A/F)
1. `orders[ref] = {ref, side, price, qty}`.
2. Side map: `'B'`→bids, `'S'`→asks; anything else → ignore (corrupt side byte).
3. `levels[price].total += qty; levels[price].count += 1;`
   (`operator[]` births the row when absent.)
4. Refresh `cached` iff price is now the edge (higher bid / lower ask).
5. Duplicate ref (same id posts twice — must not happen): unwind the OLD
   level first (`total -= old.qty`, erase at zero / count--), then file over it.
Only grows. Shrinking is reduce's job.

## reduce(ref, qty) — subtract from order + level (E/X)
1. Look up `orders[ref]`; missing → `stats.unknown_ref++`, return (hidden flow).
2. `take = min(qty, order.qty)`; if `qty > order.qty` → `stats.clamped++`
   (never wrap uint32 past zero).
3. `order.qty -= take; level.total -= take;`
4. If `order.qty == 0`: erase order, `level.count -= 1`;
   if `level.count == 0`: erase the price row, refresh `cached`.

## remove(ref) — full delete (D)
`reduce(ref, INF)`: same path, whole remainder. One function, no second code path.

## replace(Replace&) — atomic remove-old + add-new (U)
1. `remove(old_ref)` (counts as a death: level total/count drop).
2. `add({new_ref, side-of-old, price, shares})` — new id, loses queue spot.
3. Old ref missing → `unknown_ref++`, still file the new side (half-open state beats
   dropped state; counter records the anomaly).

## best() / depth(n) — reads, const, O(1) / O(n)
- `best()` returns `cached` by value (bid_px/sz, ask_px/sz; zeros = no quote).
  Never touches the maps: no locking, no iterator risk.
- `depth(n)` walks n rows per side from the edge for display/strategy.
- Invariants (assert in tests): bid_px < ask_px when both nonzero; no zero-total
  rows; Σ level totals == Σ order qtys per side.
