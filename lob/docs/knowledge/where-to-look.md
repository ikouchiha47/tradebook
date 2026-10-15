# Where to look — signal → instrument → with/without → rule

The real question is never "which optimization" — it's "which **signal**." This is the
set of signals we used, what each points at, and for every change: the before/after and
the artifact it came from. No prose, no blog — a lookup.

## The generic loop (do these in order; each has a job and a blind spot)

```
1. STAGE SPLIT     find the dominant share            ANSWERS: what matters at all
                                                       BLIND:   why it's slow
2. PROFILE         locate the hot function inside it   ANSWERS: where the time is
                                                       BLIND:   the mechanism
3. MICRO A/B       two structures, same workload       ANSWERS: per-op cost, head-to-head
                                                       BLIND:   warm-cache, fixed-N lies
4. SWEEP param     N (size) × density (gap)            ANSWERS: the regime / crossover
                                                       BLIND:   absolute truth (synthetic)
5. COUNTERS        cache/TLB/branch/IPC                ANSWERS: the mechanism (why)
                                                       BLIND:   whether it matters
6. END-TO-END      full replay, identical output       ANSWERS: ship or not
```
Rule: never skip 1. Never promote a step-3 win without step 6.

## Signal → meaning → where to look (the reusable cheat-sheet)

| what you see | what it means | next move |
|---|---|---|
| one stage owns most of the time | nothing downstream matters yet | fix/accept **that** stage (step 1) |
| a function dominates the profile | hotspot *location*, not cause | micro-bench that function (step 3) |
| cost **grows with N** | per-op `malloc`/`free` or log-depth cache misses | `L1d/LLC-misses per op` (step 5) |
| cost **grows with sparsity/gap** | you iterate **non-data** (empty slots/regions) | make the op touch only occupied data |
| **flat** across N and density | cache-resident, or genuinely O(1) | good — re-check when it *grows* |
| p99 ≫ p50 while p50 is flat | I/O, allocator, OS scheduling | stage split / `sample` |
| macro number **unmoved by a micro win** | a different stage dominates | re-run step 1 |
| IPC collapses (low insn/cycle) | latency/memory-bound, not compute-bound | cache/TLB counters |
| high `L1d`/`dTLB` misses **per op** | working set left cache / too many pages | shrink the touched set |
| `branch-misses` high & data-dependent | unpredictable compares (tree) | branch-light (mask + `clz`) |

The two highest-value reads: **"grows with N" → *where* does the working set cross cache**,
and **"grows with gap" → you're looping over emptiness, stop touching empty data.**

## Causality DAG — how each look led to the next

Not a timeline: a graph. Each arrow is a *question*; the answer names where to look next.
```
legend:  ▶ experiment   ◇ hypothesis   ■ finding   ★ fix   ↺ re-measure

▶ dense A/B (BM_Book): the "O(1)" ladder LOSES add/reduce to the map, wins replace/depth
   │  "why does the O(1) structure lose?"
   ├─▶ ◇ best recomputed every message
   │      │ read include/book.h + src/book.cpp refresh()/best path
   │      ▼
   │    ■ a root→summary→leaf descent runs twice per message
   │      ★ cache best_bid/ask (compare-on-add); rescan ONLY when the best level empties
   │      ↺ reduce 5.74 → 4.01 · add 17.1 → 15.6
   │
   └─▶ ◇ the bench is the map's best case (small + dense)
          │ sweep N × gap (BM_Sweep)
          ▼
        ■ add/remove: ladder flat, map grows with N
        ■ depth: ladder 39/57/205 — grows with the GAP ; map flat 46/45/46
          │ "cost ∝ gap ⇒ it's iterating emptiness"
          │ read src/book.cpp top()
          ▼
        ■ loop steps slot-by-slot: for (i=s; …; ++i) if (hot_[i].count)
          ★ prev_occupied / next_occupied (jump occupied→occupied)
          ↺ depth flat ~40–47 at every N and gap

▶ stage split (--stages): read 85% / route 15%
   │ "the book lives inside 15%"
   ▼
 profile (sample) ──▶ ■ inflate_fast ~64%   ("gzip dominates, not our code")
   ▼
▶ replay A/B (map vs ladder) ──▶ ■ identical book + counters; throughput a tie
   ▼
 ★ ship the ladder for SHAPE (no alloc, bounded worst case) — state msgs/s did NOT move
   ▼
▶ counters (perf stat, AWS c7g/c7i, N=1M)
   ■ map ~30 L1d-miss/op @ IPC 1.5 ; ladder ~0.005 miss/op @ IPC 3.7
   ▼
 ■ the map's cost is MEMORY (misses), not arithmetic
   └─▶ EXPLAINS the dense-200 result: a cache-resident tree's intercept beat the
       ladder's larger O(1) constant

◇ assumption "O(1) beats O(log n)" ── falsified by the dense A/B ──▶ recorded WITH its
  boundary (small + cache-resident), not deleted
```
Two reusable reads off this graph: **(1)** a signal that scales with a *parameter*
(N, gap, density) points at whatever that parameter multiplies — read the op. **(2)** an
answer that survives every downstream check becomes a rule; one that gets falsified
becomes a *bounded* rule (keep the boundary).

## Per-optimization: symptom → instrument → with/without

### 0. Baseline: the `std::map` book — where we looked
```
signal     micro A/B per op:  add 14.9  reduce 3.85  remove 18.4  replace 37.0 ns
                              best 0.32   depth 43.6
look 1     read the hot ops in include/book.h:
             add_order    bids[px]       → operator[] = tree node malloc + RB rebalance
                          orders[ref]    → unordered_map insert
             remove_order m.erase(it)    → node free + rebalance
             replace      erase + insert → ≈ remove+add (hence 37 ns)
look 2     scale sweep (BM_Scale*): add/remove grows with N  45 → 84–182 → 258 ns
             → not a fixed cost: per-op allocation + log-depth cache misses
look 3     counters (AWS): ~30 L1d-miss/op, IPC 1.5  → memory-bound, not arithmetic
found      (a) heap alloc/free per level, (b) ~log N pointer chases, (c) both grow with N
optimize   drop node alloc + pointer chasing → contiguous array keyed by price +
           a bitmap for best  = the tick ladder
```
The look-path in one line: **signal (cost grows with N) → read `add_order`/`remove_order`
→ see `malloc`/`free` + pointer chase.**

### 1. Cache `best_bid` / `best_ask` (compare-on-add; stop the per-message descent)
```
symptom      ladder's *reduce* slower than map despite no structural change
             (dense A/B: reduce 5.74 vs 3.85; add 17.1 vs 14.9)
instrument   dense A/B (BM_Book*) + profile of the per-message path
without      every add/reduce/remove descends root→summary→leaf to recompute best
             → add 17.1 ns   reduce 5.74 ns
with         compare-on-add (`px > best_bid_ ? …`); descent only when the best empties
             → add 15.6 ns   reduce 4.01 ns
rule         if a read-only op is slow, stop recomputing state it didn't change
reach it     A/B two impls; find the op whose cost is NOT explained by its own work
             (reduce does no index/alloc yet is slower on the ladder) → the cost is on
             the path every op shares → maintain incrementally + a cheap validity check
evidence     benches/bench.cpp BM_Book*; docs/benchmarks/tick-ladder-vs-map.md §"refresh()";
             perf flamegraph: refresh self% 26.7 (map) / 9.1 (ladder)
```

### 2. Rescan **only when the best level clears**
```
symptom      even with cached best, "when do we recompute?" is the next question
instrument   reasoning over the invariants (not a benchmark)
without      rescan on any remove  → wasted descents, and still correct but costly
with         rescan iff the emptied level was the best (`px == best_bid_`)
rule         the touch can move only on (a) a better arrival or (b) a best-empty —
             every other event provably can't move it
evidence     src/book.h `remove_order`; docs/knowledge/best-price-rescan.md
```

### 3. `top()` starts at the best, not the band edge
```
symptom      ladder depth(5) = 105 ns for five levels — absurd for the op
instrument   dense A/B — ladder depth worse than everything else on the board
without      scan from the band edge / cursor
with         start the walk at occ_.highest() / lowest()
             → 105 ns → 31.9 ns
rule         an op should begin at the data, not at the array's origin
evidence     docs/benchmarks/tick-ladder-vs-map.md ("depth was 105 → 31.9")
```

### 4. `prev_occupied` / `next_occupied` — the jump (**you asked for this one explicitly**)
```
symptom      ladder depth GROWS WITH THE GAP, map is flat:
             gap 100/1000/5000  →  ladder 39 / 57 / 205 ns   map 46 / 45 / 46 ns
instrument   the N×gap sweep (BM_SweepDepth*) — this single reading found it
look 1       open src/book.cpp top():  for (i = s; …; ++i) if (hot_[i].count) push;
             → a slot-by-slot walk; depth() calls it
look 2       re-bench after the jump: depth flat ~40–47 at every N and gap
without      top() walks slot by slot, `if (hot_[s].count)` — visits every empty
             slot between levels → depth = O(n·gap)
             → 39 / 57 / 205 ns
with         emit best, then jump occupied→occupied:
             `s = prev_occupied(s)` (leaf mask below → else summary → else root)
             → 43–53 / 41–45 / 44–47 ns  (flat in N and gap)
rule         cost proportional to *density* ⇒ you are iterating emptiness.
             Ask the index for the next *occupied* item, not the next item.
reach it     sweep a parameter; if cost scales with emptiness, read the loop and make it
             touch only data (here: flip `s++` → `prev_occupied(s)`)
why          O(n) per snapshot, independent of gap; same mask+clz idiom, one tier up
evidence     benches/bench.cpp BM_SweepDepth*; docs/benchmarks/tick-ladder-sweep.md
             (before/after both recorded); docs/knowledge/prev-next-occupied.md
```

### 5. Default book = ladder
```
symptom      none — end-to-end it's a tie
instrument   full replay A/B (`--book map|ladder`), identical output required
with/without map 9.43M msgs/s route 0.155s   ladder 9.70M route 0.150s
             identical best + counters (gzip owns 85%)
rule         a micro-structure win need not move the macro number. Ship the better
             *shape* (no alloc, bounded worst case) when it costs nothing end-to-end;
             state honestly that throughput didn't move
evidence     scripts/replay-ab.sh; docs/knowledge/optimization-journey.md Step 6
```

### 6. Cache the best level's **size** (the flamegraph's find)
```
symptom      flamegraph: `refresh` = 26.7% self-CPU (map) / 9.1% (ladder)
instrument   `perf record` → top functions — the profile named the function
what it is   refresh() called find_level(best_px, side) on EVERY message to read the
             best level's *size*; we had cached the best *price*, and the size still
             resolved per message (tree walk on the map, index+cold_ on the ladder)
without      per-message find_level
with         maintain best_bid_sz_/best_ask_sz_ in the Book at the same points the price
             moves (add: new best = qty, else += qty; sub: −= qty; remove: −= qty, and
             rescan the size only when the best level empties)
             → N=1M add/remove:  map 223 → 163 ns (−27%)  ·  ladder 30 → 26 ns (−15%)
             → real feed: identical best + counters; route 0.158/0.156s (inside the 15%)
reach it     a flamegraph names the function; if it's a lookup for state you already
             track the price of, cache that state at the same points
evidence     tools/scale_probe.cpp; tests Book.CachedBestSizeTracksLevel{Map,Ladder};
             scripts/aws/fg-arm64-*.svg
confirmed    after: `refresh` leaves the top functions entirely — map now add 38.1% /
             erase_order 30.7% (was add 32.6% / refresh 26.7%); ladder depth 27.1% /
             erase_order 10.3% (was depth 24.3% / refresh 9.1%).
             ARM N=1M add/remove: map 613 → 336 ns, ladder 68 → 57 ns
```

## Where the difference actually lives (measured, not asserted)

Dense scale (BM_Scale* / tools/scale_probe.cpp), add+remove of a new level:
```
N=1k / 10k / 100k / 1M     map 45 → 50 → 84–182 → 258 ns
                           ladder 29 → 31 → 32 → 46 ns
```
Mechanism, `perf stat`, N=1M, per op (maps open: AWS c7g + c7i, docs/knowledge/aws-perf-counters.md):
```
                       map     ladder
cycles/op              931–961  223–230
IPC                    1.5      3.7
L1d-cache-miss/op      ~30      ~0.005
dTLB-miss/op (ARM)     59       ~0.006
```
Raw: `scripts/aws/results-arm64-*.txt`, `scripts/aws/results-x86_64-*.txt`.
The map's 4× cycles/op is **misses**, not arithmetic — that's the "why" behind every row above.

## From pseudocode to bits — why it was hard, and the crossing

The struggle was real and it had one cause: **nothing said how a set maps to a word.** Once
each English phrase got its bit form, the "bit magic" became bookkeeping:

```
pseudocode phrase                     bit form
"a set of slots"                       one uint64: bit i = slot i present
"is slot s present"                    (w >> s) & 1
"slots below me (strictly)"            word & ((1 << lb) - 1)
"climb to the summary"                 sw = lw >> 6; repeat the same query on summary
"the highest present"                  63 - countl_zero(word)   ... i.e. (W-1) - clz
"the lowest present"                   countr_zero(word)        ... ctz
```
- `clz` = count leading zeros; the top 1 sits `clz` steps down from the top, so its index
  is `(W-1) - clz`. That is *all* "which slot is best" is — one instruction.
- Concretely: `clz(0b00001100) = 4` (four zeros, then bit 3) → `7 - 4 = 3` = the slot.
- The hierarchy is not a new idea: it's the **same three moves** (mask one side, AND, read
  the survivor) asked one word up. `prev_occupied` is that sentence, twice.

Crossing it, as a method: write the English ("highest occupied slot below me"), then write
the **mask that keeps exactly that half**, `AND`, then `clz`/`ctz`. Every query is those
three steps at some tier. See `bit-manipulation.md` for the full primitive set.

## big-O and beyond (the durable model to keep)

- Big-O gives the **slope**; it ignores the **intercept**. At 200 levels the map's
  L1-resident O(log n) beat the ladder's bigger O(1) constant — the intercept won.
- Model per-op cost as `intercept + slope·f(N)`. The map's slope is real (per-op `malloc`
  + log-depth misses); the ladder's is ~0 until its touched set leaves cache.
- Predict the flip by **working set vs cache**, not by N: `map ≈ occupancy × ~48 B`;
  `ladder ≈ range/tick × 8 B` touched. Whichever crosses L1/L2 first pays 10–100×/access.
- **Touch density beats footprint:** a sparse 8 MB ladder still wins because each op
  touches ~3 bitmap words + one level, while the map touches ~log N missing nodes.
- Ship when the *shape* is better even if the macro number is unmoved — and say so.

## Profiling the real replay (gzip removed)

The flamegraphs above were on the synthetic probe. To profile the *actual* replay we took
gzip out of the way: `--decompress` writes a raw framed `.bin` (8.66 GB), `--raw` replays it
with no zlib.

```
replay, 282M msgs        gzip        raw: 2 fread/msg     raw: 1 MiB blocks
throughput               11.16M/s    15.61M/s             44.08M/s
elapsed                  25.3s       18.08s               6.40s
```

`--stages` on the raw run said **read 74% / route 26%**. Whether that "read" was disk or CPU:
```
cat $BIN >/dev/null   →   8.66 GB in 3.3 s   →   pure I/O is ~3 s of ~18 s
⇒ the ~13 s "read" is framing CPU, not disk
```
(The symbolized `sample`, `-g -fno-omit-frame-pointer`, put `feed.next` at ~1% with the time
in the inlined loop — the sampler cannot attribute inlined `fread`, so here the wall-clock
split was the trustworthy signal. When the two disagree, the pure-I/O baseline settles it.)

The raw path did **2 `fread` per message** (~564 M stdio calls); the gz path's `gzread` is
already buffered inside zlib, which is why only the raw path regressed. Fix: a 1 MiB block
reader (`FeedReader::ensure` → ~1 `fread` per MiB).

```
raw FULL   18.08s → 6.40s   (2.8×)
vs gzip    11.16M → 44.08M msgs/s   (~4×)
```

Rule: after removing one dominant cost, re-run the split — the new bottleneck is often the
*framing* that was hidden under it. And a stage's wall time against a `cat` baseline tells
CPU from disk.

## Evidence index
```
numbers      docs/benchmarks/{PERFORMANCE,tick-ladder-vs-map,tick-ladder-sweep}.md
             scripts/aws/results-{arm64,x86_64}-*.txt
scripts      scripts/{sweep,replay-ab,profile,counters}.sh   scripts/aws/{run-probe,ec2-probe}.sh
probe        tools/scale_probe.cpp   (self-contained; no data file)
bench src    benches/bench.cpp  (BM_Book* = dense micro, BM_Sweep* = N×gap, BM_Scale* = to 1M)
```
