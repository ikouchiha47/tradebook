# Matching basics — levels, priority, the match loop, the lifecycle

The concepts between "what orders exist" (`order-types.md`) and the code. Read this before
designing the engine's data structures.

## What a level is

A **level** (price level) is all the resting interest at **one price** — one row of the book.

```
price      side   orders (FIFO)                  total  count
100.03     ask    [ o7: 500 ]                      500     1
100.02     ask    [ o5: 300 ][ o9: 100 ]           400     2    ← a level
                 ─────────── spread ───────────
100.00     bid    [ o3: 200 ][ o8: 50 ][ o1: 700 ] 950     3    ← a level
 99.99     bid    [ o2: 400 ]                      400     1
```

A level has two faces:
```
key        the price
aggregate  total (sum of resting qty)  and  count (how many orders)
```
The book is a collection of levels ordered by price; `best bid` = highest bid level,
`best ask` = lowest ask level. Several orders share a level whenever they arrive at the same
price — that's why `count` exists.

Two representations, depending on who holds it:
```
market-data book (P1)   level = { total, count }              aggregate only — ITCH gives
                                                              a level, not the orders
matching engine         level = { total, count,               aggregate + the FIFO queue of
                                  head, tail }                individual orders (time priority)
```
Matching needs "how much **and in what order**," so the engine's level owns a queue (intrusive
linked list; first-in at `head`).

In P1 the ladder's `hot_` array holds one `Level{total,count}` per **slot**; a slot's occupancy
bit is set iff its level is non-empty (`count > 0`). *Slot* = the array index (a price, via
`base + slot*inc`); *level* = the data there.

## Price-time priority (the rule)

Two keys, in order:
```
1. price   best price first            (highest bid, lowest ask)
2. time    same price -> oldest first   (FIFO)
```
You get filled ahead of someone else by being **better-priced**, or **earlier** at the same
price. It's the standard for equities because it's fair, deterministic, and easy to state.

## The match loop

Roles:
```
resting order  = already in the book  -> the MAKER (sets the price)
incoming order = the aggressor        -> the TAKER
trade price    = the MAKER's price    (the resting order's price wins)
```
```
while taker has qty left and it still crosses:
    take the best opposite level
    take the OLDEST order at that level      (time priority)
    fill min(taker_left, maker_left)
    emit Trade{price = maker.price, qty}
    fully-filled maker -> unlink it
    level now empty    -> advance to the next occupied level
    taker filled       -> stop
leftover: rest (limit) or drop (IOC/market)
```
Fallout: **partial fills** are normal (split across makers), and a large taker **sweeps**
multiple levels paying successively worse prices.

## Allocation algorithms — how a level is split

Price priority decides **which level** fills first. *Within* a level, who at that price gets
filled is the **allocation algorithm**:

```
FIFO (price-time)       oldest order first                              equities, ITCH
Pro-Rata                proportional to each order's size               futures (CME/Eurex)
Pro-Rata with priority  a "top-order" gets a guaranteed slice, rest pro-rata
Threshold Pro-Rata      FIFO up to a threshold, then pro-rata
Split / configurable    e.g. X% pro-rata, (1-X)% FIFO
Allocation (top-order)  a designated / lead market maker fills first
Size priority           largest order first
```

Worked example — level `A:100 B:300 C:600` (total 1000), taker takes 500:
```
FIFO      A: 100 (full)   B: 300 (full)   C: 100     oldest first
PRO-RATA  A:  50          B: 150          C: 300     take × size/level = 500 × size/1000
```

Pro-rata rewards **quoting size** (that's why futures use it — it encourages big quotes); FIFO
rewards **speed** (equities). The catch with pro-rata: it spreads a taker thin, so **everyone
gets a small partial fill** and the book accumulates **dust**. Venues add "with priority" /
"minimum allocation" to stop everyone getting one lot.

For the engine this is a **policy seam** — the match loop picks the level, then calls an
allocator:
```
allocate(level, taker_qty) -> [ (order, fill), ... ]   FIFO: queue order · Pro-Rata: by weight
```
The level + FIFO queue are shared; FIFO vs pro-rata is one function. Our default is FIFO; naming
the seam now is where pro-rata or MM-priority would slot in.

## Allocation blends & parameters (real venues)

Production engines don't pick FIFO *or* pro-rata — they **blend** with parameters (e.g. Derive):
```
FIFO pass        some % / min-size routed FIFO
pro-rata pass    the rest by size
FIFO cleanup     rounding leftovers routed FIFO
params           pro_rata_fraction · fifo_min_allocation · pro_rata_amount_step
```
So: `80% pro-rata / 20% FIFO`, a minimum allocation that rewards price-improvers, a rounding
step — per-instrument tuning of one algorithm.

## Auction / uncrossing algorithms

Open/close (call) auctions don't match continuously — they **uncross a batch at one price**:
```
1. volume-maximizing price    the price that trades the most volume
2. tie -> minimum imbalance
3. tie -> market pressure / dynamic reference price
4. random uncrossing window    (anti-sniping)
```
Cleaner theory variants: **uniform-price** clearing (all at one price) and the **Ausubel
auction** (multi-unit Vickrey generalization, ascending/descending).

## Timing — continuous vs batch

```
continuous (CLOB)          match on arrival; rewards speed          <- what we built
Frequent Batch Auctions    sealed-bid, discrete-time, uniform price; removes the speed race
  (Budish/Cramton/Shim)    "pro-rata across batches, not within a batch"
speed bumps                IEX (symmetric); TSX Alpha / Aequitas (random asymmetric):
                           deliberate delay on takers to protect makers
```

## Beyond — the crypto / DeFi frontier (actively researched)

```
MEV, encrypted mempools    hide order content until ordering is fixed (Shutter, Ferveo)
CoW Protocol / intents     batch ("combinatorial") auctions; solvers compete; Coincidence
                           of Wants; uniform clearing -> order-independent -> no MEV
privacy-preserving match   MPC / FHE / TEE dark pools; commit-reveal batch auctions
```
Where matching is being *invented* right now. A single-symbol continuous LOB needs none of it,
but it's the design space beyond FIFO.

## Invariants (test these)

```
no crossed book     best_bid < best_ask after every submit
FIFO within a level earlier order fills first, always
price priority      a worse-priced order never jumps ahead
conservation        submitted_qty == resting + filled + cancelled
registry ⇄ queue    id lookup and the level queue stay consistent on cancel/replace
```

## The order lifecycle

The states an order moves through, and the events that drive them:
```
new ──accept──> open (resting) ──fill──> partially_filled ──fill──> filled
   \                 \                        \
    reject            cancel                   cancel
                      v                        v
                   cancelled                cancelled
   (GTC/Day) ── close/expire ──> expired ; (IOC/FOK) ── no fill ──> cancelled
```
- **new → open**: accepted and rests (limit) — or **rejected** (bad price, post-only that
  would cross, unknown instrument).
- **open → partially_filled → filled**: executions against it.
- **open → cancelled**: an explicit cancel (or STP).
- **expired**: Day orders at the close; GTD at their date.
- **IOC/FOK**: never reach `open` — they fill what they can and go to `cancelled`.

The engine reports each of these as an execution report / status. On the wire (ITCH) these are
`A/F` (add) → `E/C/X` (execute/cancel) → `D` (delete) → `U` (replace) — see `lifecycle.md`.

## Where this leads

Once levels + priority + the loop are fixed, the build order is:
```
1. Limit (GTC)      level + FIFO queue + rest
2. Market           same loop, no rest
3. Cancel / Replace order id -> O(1) unlink, then re-add (new time priority)
4. IOC / FOK        remainder handling
5. Post-Only, Iceberg
```
See `order-types.md` (the taxonomy), `lifecycle.md` (wire messages), `hierarchical-bitmap.md`
and `book.md` (the P1 book these reuse).
