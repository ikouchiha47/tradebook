# Robustness — disorder, refs, and testing them

## Why records arrive out of order
1. UDP multicast drops/reorders/duplicates; gap-fill retransmits land late,
   after newer messages already printed.
2. Independent channels (A/B feeds, symbol groups) sequence separately;
   the file merges them by arrival, scrambling cross-channel order.
3. Timestamps are per-channel with finite resolution — ties and inversions happen.
At 268M records "mostly ordered" still means thousands of surprises.

## Guards (all in book.md contracts, counted in Counters)
- Unknown ref (`E/X/D/U` for an id never filed — hidden flow, filtered births,
  late `A`): `unknown_ref++`, skip. Never subtract blindly: `E` carries no price,
  so there is no valid level to touch, and uint32 would wrap past zero.
- Over-take (`qty > resting` — double-delivered reduce, skewed window):
  `take = min(...)`, `clamped++`. Floor at zero, count the event.
- Crossed book (bid >= ask from stale prints): `crossed++`, continue.
  Transient by nature; never panic, never halt the fold.

## Ref semantics (why ids are the whole game)
- Birth (`A/F`) mints the ref; every later letter borrows side/price/locate
  from `orders[ref]`. No ref, no meaning — hence unknown-ref skips.
- `U` is the only two-id letter (kill old, file new, lose queue spot).
- `match` numbers group one execution's records for the tape; the book ignores them.

## Testing disorder (do this, not just honest data)
- Reordered VecFeed: `A(100) → E(30)` vs `E(30) → A(100)` — first folds clean,
  second must count unknown_ref and still end correct after the late `A`.
- Truncated tail: 10 of 39 `R` bytes → parse false, no partial struct escapes.
- Unknown id: `X`/`D` for ref never born → counters move, maps untouched.
- Double reduce: same `E` twice → second clamps, totals never wrap.
Feed-concept's `VecFeed` + `replay()` already run without the file — these cases
drop straight into `lob_test` beside the parser cases.

## Splitting the file (P3, parked — design locked, no code yet)
- Shard by locate range (1–1000, 1001–2000…), never by byte offset: offsets
  split one symbol's story across shards and corrupt its fold.
- One sequential fold per shard (causality within a symbol is untouchable),
  parallel only across symbols; merge stats at the end.
- `FeedReader` unchanged; a `ShardFeed` satisfies the same Feed checklist.
  Single-threaded correctness first (ADR rule); sharding is the victory lap.
