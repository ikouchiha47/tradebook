# ADR-0003 — Order storage: a pool + intrusive FIFO queues

Status: proposed.

## Context

The matching engine (ADR-0002) mints and retires orders constantly — every submit creates one,
every full fill or cancel frees one. P1 measured exactly this cost on the market-data side: a
node-based container (`unordered_map`) pays one heap allocation per order (**1 alloc/op**) and
scatters nodes across memory (**~27 L1d-misses/op**, **~59 dTLB-misses/op** at 1M). A matching
engine is *worse*: it also needs per-price **time priority** (a FIFO queue), which a node
container gives only awkwardly.

## Decision

1. **Orders live in a preallocated pool** — `Order pool[N]`, contiguous, reserved once.
   An order's identity is its **`u32` slot index**, not a pointer.
2. **The per-level FIFO queue is intrusive** — the `Order` carries `prev`/`next` (slot
   indices); a `Level` holds `head`/`tail`. Append and unlink are pointer swaps; the queue
   allocates nothing.
3. **The freelist is a bump + return-stack**, not a prefilled chain:
   ```cpp
   uint32_t take() { if (free_ != kNil) { uint32_t i = free_; free_ = pool_[i].next; return i; }
                     return fresh_++; }              // reuse hot slot, or bump a fresh one
   void     put(uint32_t i) { pool_[i].next = free_; free_ = i; }   // return to stack
   ```
   `take`/`put` are O(1); freed slots are reused LIFO, so they stay cache-warm. No prefix pass
   over the pool, so RSS tracks *used* orders, not the reservation.
4. **Reserve, don't grow** (default): 64 MiB ≈ 2M orders — "never grow" for one book. If the
   book can exceed that, grow in **fixed blocks** that never move (a reallocating `vector`
   would invalidate every slot index).
5. **Cache-line discipline**: `alignas(64)` the pool; keep the freelist head and any shared
   metadata on their own line (only matters across threads); size `Order` so it does not
   straddle lines (~32 B ⇒ 2–4 per 64 B line).

## Alternatives considered

- **`std::list<Order>` / node containers** — one allocation per order, pointer-chasing. The
  exact cost P1 measured. Rejected.
- **`std::deque` per level** — block allocation, and O(1) middle removal needs iterators;
  many containers, awkward. Rejected.
- **Index into a reallocating `std::vector`** — growth moves slots and invalidates every
  `prev`/`next`/registry index. Rejected (use fixed or block-grown).
- **Pointers instead of `u32` indices** — 8 B vs 4 B, not relocatable, worse TLB use. Rejected.

## Consequences

- Hot path (submit / fill / cancel) **allocates nothing**; it touches the pool + a bitmap.
- O(1) take/put, O(1) FIFO append/unlink, O(1) cancel by id (with a preallocated id→slot map).
- New invariants to test: freelist ⇄ pool consistency; a slot is in exactly one list (free,
  or one level queue); `count` in a level equals its queue length; no slot leaks on cancel.
- One pool per engine/book (orders of all levels share it). Multi-symbol ⇒ one pool per book,
  or shard — note the 64 MiB reservation is per pool.
