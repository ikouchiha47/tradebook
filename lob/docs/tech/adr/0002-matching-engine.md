# ADR-0002 — Matching engine (P2): price-time priority on the tick-ladder book

Status: proposed.

## Context

P1 is the market-data side: `lob-replay` decodes ITCH 5.0 into a book that is price-indexed
(`PriceLadder`: a slot array keyed by tick, a hierarchical occupancy bitmap for O(1) best,
and a `cold_` map for the out-of-band tail). It holds *aggregate size per level* — that is
all a market-data book needs.

Matching needs one more thing: **order identity and time**. Within a price, the resting
orders are a FIFO queue (price-time priority); cancels and replaces must find an order by
id in O(1). P1's level (`{total, count}`) has neither.

## Decision

Build the engine as a **new layer that reuses the level indexing, adds per-level FIFO
queues and an order registry**.

1. **Prices**: reuse the tick-ladder level structure (`hot_` slot array + occupancy bitmap
   + `cold_`). Best-first price iteration is already O(1)/O(occupied).
2. **Time**: each level owns a **FIFO queue** of resting orders. An intrusive doubly-linked
   list (order carries `prev`/`next` index) keeps the queue allocation-free and lets cancel
   unlink in O(1).
3. **Identity**: an **order registry** `id → Order{side, price_slot, qty, prev, next}`.
   `id` is either the ITCH-style u64 ref or a monotonic sequence; the registry is an array
   or open-addressed map, grown as orders arrive.
4. **Match loop**: an aggressor walks the opposite side from the best slot; at each level it
   consumes the queue **FIFO**, emitting `Trade{price, qty, maker_id, taker_id}` until the
   aggressor is filled or the level exhausts (price-time priority), then advances to the
   next occupied slot via the occupancy bitmap.
5. **Cancel / replace**: cancel = unlink from its level queue + remove from the registry,
   O(1). Replace = cancel + re-add (a new time priority at the new price/size).

## Alternatives considered

- **`std::map<price, Level>` levels** (the P1 reference book) — correct and simple; the
  ladder wins the large/sparse regimes measured in P1's benchmarks, so reuse the ladder.
- **Heap / priority queue for the book** — gives O(1) best but no arbitrary-key cancel;
  cancel/replace would need lazy deletion with stale-node buildup. ITCH-style flow is
  cancel-heavy, so this is the wrong shape (the earlier PQ analysis).
- **Reuse the market-data book verbatim** — impossible without per-order time priority and
  an id→order lookup; the aggregate level is insufficient.

## Consequences

- The engine shares the price indexing with P1 (same slot math, same occupancy), so the two
  stay consistent and the ladder's measured properties (scale-invariant writes, flat sparse
  depth) carry over.
- New invariants to test: FIFO order within a level, no crossed book after a match, order
  registry ⇄ queue consistency on cancel/replace, size conservation (submitted = rested +
  filled + cancelled).
- Memory: one registry entry + two link indices per resting order; the ladder's level array
  is unchanged in shape.
- Trade output is a sink (callback/collector), keeping I/O and matching separate — same
  seam as P1's `itch.*` (wire) vs `book.*` (state).
