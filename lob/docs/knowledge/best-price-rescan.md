# Cached best and rescan-only-on-clear

How `best_bid` / `best_ask` stay correct while almost never being recomputed — and why
the rescan condition (only when the best level empties) is sufficient.

## The cached scalars

Each book keeps `best_bid_` / `best_ask_` (raw ticks, 0 = none). Two update rules:

1. **On add: compare, never descend.**
   ```cpp
   if (side == 'B') { if (px > best_bid_) best_bid_ = px; }
   else             { if (best_ask_ == 0 || px < best_ask_) best_ask_ = px; }
   ```
   A new order can only *improve* the touch (higher bid, lower ask), so one integer
   compare maintains the invariant. Cost: ~free. This removed the per-message
   root→summary→leaf descent that Step 1's profile caught (ladder add 17.1 → 15.6 ns,
   reduce 5.74 → 4.01 ns).

2. **On remove: rescan only if the emptied level was the best.**
   `remove_order` returns whether the level's `count` hit zero. The book checks exactly
   one condition:
   ```cpp
   if (!ladder(side).remove_order(px, qty)) return;   // level still rests: best unchanged
   if (side == 'B' && px == best_bid_) best_bid_ = bids.highest();
   if (side == 'S' && px == best_ask_) best_ask_ = asks.lowest();
   ```
   A partial `reduce` (shares leave, the order rests) never calls this path — the level
   still rests, so the touch cannot move.

## Why "only on clear" is sufficient

The best price can change in exactly three ways, and the two rules cover all of them:
```
a better price arrives      → rule 1 (compare on add) catches it
the best level empties      → rule 2 (rescan) recomputes it
anything else               → the touch is untouched by construction
```
"Anything else" — adding a worse price, reducing a resting order, removing a non-best
level — provably cannot move the best. So the cached best equals the true best at all
times, and the expensive descent runs only on best-empty (rare) instead of per message.

## `highest()` / `lowest()` fold in `cold_`

Rescan must see *every* level, including off-band/off-grid prices that live in the
`cold_` map. `PriceLadder::highest()` / `lowest()` combine both:

```cpp
int64_t PriceLadder::highest() const {
  int slot = occ_.highest();
  int64_t best = (slot < 0) ? 0 : base_ + slot * inc_;   // best HOT slot, or 0
  if (!cold_.empty())
    best = (best == 0) ? cold_.rbegin()->first : std::max(best, cold_.rbegin()->first);
  return best;                                            // max(hot best, cold max)
}
// lowest(): mirror with occ_.lowest(), cold_.begin(), std::min.
```

So the rescan is `max(hot best, cold max)` for bids (mirror for asks) — correct for any
band, tight or wide. `top()`/depth walks `hot_` alone (a documented minor gap); `best()`
is exact.

## `refresh()` — the per-message read path

Every mutating call ends in `refresh()`, which copies the cached scalars into the
returned `TopOfBook` and re-reads each side's size (cheap: one `find_level`):

```cpp
book_.cached.bid_px = best_bid();   // the scalar, not a descent
Level* l = find_level(bid_px, 'B'); // size lookup
```

`best()` itself is a struct copy — 0.32 ns. The per-message cost of "keeping best" is
therefore two compares on add plus a size lookup on refresh; the descent is amortized to
best-empty events only.

See: `ladder-casebook.md` (remove-11 → rescan walkthrough), `hierarchical-bitmap.md`
(array layout), `optimization-journey.md` Step 2 (measured effect).
