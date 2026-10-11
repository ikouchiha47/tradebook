# `std::map` vs price ladder — the difference

The two books behind `NasdaqBookParser<Book>`, side by side. Same API
(`add_order`/`sub_share`/`remove_order`/`find_level`/`best_*`/`top`), same cached-best
discipline, different storage — and therefore different scaling. Numbers: Apple M4,
Release; full tables in `benchmarks/tick-ladder-vs-map.md` and
`benchmarks/tick-ladder-sweep.md`.

## Storage

```
std::map<int64,Level> bids, asks     one RB node per live level, heap-allocated,
                                     ~48 B + allocator metadata, linked by pointers.

PriceLadder bids, asks               hot_: Level[slots] contiguous (8 B/slot),
                                     occ_: 1 bit/slot + summary + root,
                                     cold_: std::map for off-band/off-grid prices.
```

The map stores exactly the live levels and nothing else. The ladder preallocates the
band (narrow = cheap; wide+sparse = wasteful) and keeps a cold tail so any price is
representable. Footprint: map ≈ N × 48 B; ladder ≈ slots × 8 B + bitmap.

## Operation by operation

```
op            map                                  ladder
lookup        tree walk, ~log2 N derefs            index math: (px-base)/inc + one load
insert        node malloc + rebalance + chase      slot write + maybe one bit set
erase         node free + rebalance                slot clear + maybe one bit clear
best          tree edge pointer (cached scalar)    cached scalar; rescan = bitmap descent
depth(n)      walk n tree nodes                    walk n occupied slots (jump)
```

The map pays **allocation + pointer chasing** per structural op. The ladder pays **index
math + a bit flip**, and touches only occupied words.

## Cache behavior (M4: 128 B lines, 4 MiB L2)

A 200-node map ≈ 10 KB — L1-resident — so its ~8 hops are all L1 hits. That is why O(log
n) beat O(1) on the dense micro bench: the ladder's fixed constant (index + bitmap +
descent) exceeded a fully-cached chase. Past L1/L2 the map's hops start missing and its
`malloc`/`free` churns; the ladder's touched set stays ~3 bitmap words + one level, so it
holds flat. Measured (dense, N = 1k → 1M): map add/remove 45 → 258 ns, ladder 30 → 46 ns.

Touch density beats footprint: a sparse 8 MB ladder still wins because it never visits
dead range, while the map visits ~log N missing nodes per op.

## When each wins

```
dense + small (≤ L1)     map wins or ties on constants (add/reduce)
large N (past cache)     ladder wins, gap widens (add/remove 1.4× → 2.2× → 5×+)
sparse depth             ladder wins after the occupied→occupied jump (was map 4×)
end-to-end replay        tie (gzip owns 85%); ladder ships for shape, not msgs/s
```

The map is the right default for a tiny dense book. The ladder is the right default for
anything that grows or thins — which is why it is now ours (`--book ladder`, band
$0–$2000 @ 1¢, `cold_` for the rest).

See: `benchmarks/tick-ladder-vs-map.md` (dense A/B + why-map-won), `benchmarks/*sweep*`
(N×gap, scale-to-1M), `best-price-rescan.md` (shared caching), `algorithm-design-guide.md`
§3 (predicting the crossover from working set vs cache).
