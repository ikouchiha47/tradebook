# Jane Street — "Building an Exchange" (JX) — system design notes

Speaker: Brian Nigito, Jane Street. Talk: https://www.janestreet.com/tech-talks/building-an-exchange/
Framed as a system design doc. Source: talk transcript.

## 1. Problem & goal
A venue where many participants meet to trade. Constraints: high transaction rate, low
deterministic latency, fairness, reliability, durability, robustness to bad clients —
with the simplest possible core.

## 2. Domain model
- Limit order book: bids **descending** (sell to highest), asks **ascending** (buy from
  lowest); the "touch" is where they meet. (Ladder view exists but is patent-encumbered.)
- Messages: new order, cancel, cancel/replace, executions, plus noise.
  Mix ≈ **adds 50 %, cancels 30–40 %, executions 1–2 %**.
- Matching: price-time priority; marketable order atomically generates executions;
  price improvement goes to the **aggressive** order (incentive design).

## 3. Non-functional requirements (stated in the talk)
- Scale: **~3M msgs/s peak**, thousands of participants/connections, **several million
  live orders**, ~10k symbols.
- **Fairness**: information must reach participants *simultaneously* (naive sequential
  fan-out creates first-in-list races — the Iland TCP story).
- **Reliability + durability**: a reported trade is a commitment (no take-backs).
- **Robustness to bad clients**: one participant must not degrade others.

## 4. Component architecture
- **Matching Engine** — the single core; book held **in memory**, on **one commodity x86
  box**. All logic that can be factored out is.
- **Client Ports** — one process per client, **TCP to client** (FIX or leaner); provides
  an ordered, sequenced stream; normalizes, validates, flow-controls, holds the locate.
- **Drop Ports** — feed clearing/guaranteeing firms their participants' activity.
- **Trade Reporter** — reports trades to the trade reporting facility, on parity.
- **Market Data** — anonymized/filtered copy of the transaction log (clients rebuild).
- **Cancel Fairy** — externalizes scheduled cancels; multiple copies race; keeps the
  engine simple.
- **Auctions** — run *outside* the engine (aggregate, optimize max shares, send result).

## 5. Core data structure
- In-memory book; orders **preallocated** into slots; ack returns a **locate** (array
  index, not a pointer) that ports hold and return with cancels — "a cheat code".
- Data arrives **copy-free**; read values, mutate pooled structures; **allocate ~nothing
  in the critical path**.

## 6. Transport & delivery
- **Multicast** (switch-replicated logical address) → same packet to every subscriber
  near-simultaneously (engine, ports, market data, drop ports, reporter). Without it the
  architecture doesn't work.
- It's **UDP — unreliable, no acks**. Mitigation: **retransmitters** (servers recording
  messages + sequence numbers); on a gap they ask peers, then the Matching Engine (which
  keeps an in-memory copy of its output).

## 7. Sequencing & locking — the "topic" model
- Every message has a **unique topic** = an **independent lock** with its **own sequence
  number** (distinct from the global sequence).
- A port **proposes the next transaction + seq for its topic**; on a lost race it
  **rolls back, applies the winner, retries** → **one unacknowledged transaction in
  flight** per port (rollback = keep one message).
- The **Matching Engine alone publishes cross-topic messages** (executions): one order →
  messages on both parties' topics, atomic at **UDP-packet granularity**.
- **Collision**: a stale proposal is silently **dropped** (not rejected); the sender
  eventually sees the real seq, applies it, retries → may then get a legit cancel reject.
- Anything the state machine acts on or shows a client **must come from the totally
  ordered stream** (why even cancel-rejects are sequenced).

## 8. Replication & recovery — state machine replication (SMR)
- Every downstream app **deterministically applies the ordered transactions** → kill and
  replay from a retransmitter, rebuild exact state (~30 s target is enough).
- Client reconnect **handshakes on sequence numbers** ("I saw N" / "I have N+2" → send
  missing 2).
- **Passive Matching Engine** replicates by listening to the primary's **output** (not the
  ports — UDP arrival order unguaranteed); identical code/state machine; **regenerates**
  executions, so a mid-transaction crash is harmless.
- **Failover is an explicit operator switch**, not Paxos — consensus adds a round trip;
  "I don't know of any exchanges that use them."
- Trade-off acknowledged: correlated failure risk, "a little brittle"; discipline + heavy
  testing.

## 9. Performance model (why speed matters)
- **Latency determines throughput**: flow control = one unacknowledged transaction in
  flight + a small socket buffer; TCP back-pressure throttles.
- Internal transactions in **low single-digit microseconds** on commodity hardware.
- **~30 % of time on one line**: dereferencing the locate index → **cache miss** — the
  dominant cost.
- Speed buys **simplicity**: quick rebuild (<30 s) removes complex state-transfer code.

## 10. Parallelism
- Obvious axis: **symbols** — but it **breaks cross-symbol atomicity** (ETF risk limits
  enforced atomically in the engine).
- Multiple cores mainly help by keeping **many memory fetches in flight** (prefetch a
  batch of messages).
- Per-symbol replication ⇒ **separate ports / sequence spaces** (else unrecoverable
  state); NASDAQ options used multi-instance + a proxy, later went direct to the "rings".

## 11. Language / runtime
- Done in **C++, Java, now OCaml** — OCaml gives C-level speed with a **simple runtime**
  (simpler GC/threading than Java → determinism).
- **Not standard idioms**: minimal allocation, preallocated/pooled, copy-free hot path.

## 12. Design principles / takeaways
1. Speed isn't the goal — **determinism and simplicity are**; speed enables them.
2. **State machine replication** → testable, replayable, fuzzable ("replay weeks" before
   each rollout).
3. **Determinism → regulatory explainability**: "I know exactly what I knew at every
   point in time."
4. **Factor logic out** of the hot core (cancel fairy, auctions) — keep it brutally simple.
5. The decisions hang together: one engine + multicast + retransmitters + SMR + operator
   failover = a coherent, naturally scaling design.

## 13. Map to this project
| JX concept | ours |
|---|---|
| limit order book | P1 (mirror book) / P2 (matching engine) |
| locate handle (array index) | the slab + `id_to_slot` arena idea |
| ~30 % cost = locate deref cache miss | why the tick ladder + slab matters |
| one-in-flight, topic locks | P3 execution/OMS concurrency model |
| multicast + retransmitters + SMR | P3 live feed (MoldUDP64 dedup + replay) |
| replay-to-rebuild / determinism | our event-sourced book + determinism hash |
| preallocate, no hot-path alloc | our `test_alloc` + slab direction |
