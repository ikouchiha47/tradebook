# `prev_occupied` / `next_occupied` — the jump that fixed sparse depth

## What invokes it

Exactly one caller: `PriceLadder::top()` — the depth snapshot. To emit 5 levels it must
walk the book from the touch outward:

```cpp
void PriceLadder::top(bool is_bid, size_t n, std::vector<LevelRow>& out) const {
  int s = is_bid ? occ_.highest() : occ_.lowest();   // best occupied slot
  while (s >= 0 && out.size() < n) {
    out.push_back({base_ + s * inc_, hot_[s].total, hot_[s].count});
    s = is_bid ? occ_.prev_occupied(s) : occ_.next_occupied(s);
  }
}
```

Before this existed, the last line was `s--` / `s++` with an `if (hot_[s].count)` guard —
a slot-by-slot walk that stepped on every empty price between levels.

## Which benchmark revealed it

`BM_SweepDepth` (N × gap), ladder column:

```
gap 100   (1 slot apart)    39 ns
gap 1000  (10 slots)        57 ns
gap 5000  (50 slots)       205 ns     ← map holds ~46 at every gap
```

The map's depth was flat in the gap (tree walk is N-driven); the ladder's grew with it.
A cost proportional to *emptiness* can only be a walk over empty slots — that single
reading named both the bug and the fix: stop visiting slots that hold nothing.

## Pseudocode (two tiers, then the root for free)

`prev_occupied(slot)` = highest occupied slot strictly below `slot`.
(`next_occupied` mirrors: above, `ctz`, lowest.)

```
prev_occupied(slot):
  s  = slot
  lw = s >> k ;  lb = s & (W-1)          # my leaf word, my bit in it  (k=6, W=64)

  # tier 1 — my own leaf word, bits below lb
  m = leaf_[lw] & ((1<<lb) - 1)          # stencil: keep 0..lb-1
  if m:  return lw*W + ((W-1) - clz(m))  # highest survivor → done

  # tier 2 — climb to the summary word, leaf words below lw
  sw = lw >> k ;  sb = lw & (W-1)
  m = summary_[sw] & ((1<<sb) - 1)       # stencil: keep words 0..sb-1
  if m:
      plw = sw*W + ((W-1) - clz(m))      # nearest non-empty leaf word below
      return plw*W + ((W-1) - clz(leaf_[plw]))   # descend → its top slot

  # tier 3 — summary also empty below: same stencil on root_, then descend twice
  return -1
```

The three moves are identical at every tier — *mask one side, AND, read the top/bottom
survivor* — which is why the climb is just the same query asked about words instead of
slots. Strictness comes from the `-1`: `(1<<lb)-1` drops the anchor's own bit, so the
answer is always genuinely below (mirror: `~((1<<(lb+1))-1)` drops it going up).

## Why it is an optimization

Before: O(gap) per emitted level → depth(n) costs O(n·gap). Five levels across 50-slot
gaps = ~250 dead slot checks = 205 ns.

After: ~2 ops when the neighbor shares the word (AND + `clz`), ~4 when it climbs (AND +
`clz` on the summary, AND + `clz` on the leaf) → depth(n) costs O(n), **independent of
the gap**. Measured: 39/57/205 → 43–53/41–45/44–47. The 205 is gone; the ladder is flat
in N *and* gap while the map still climbs with N.

## What we optimize for

Depth latency on sparse books — the realistic shape (a quiet symbol's levels sit cents
apart, not ticks). More generally: the ladder's core promise that an op touches **only
occupied data**. A slot walk breaks that promise by visiting emptiness; the jump restores
it, and the bounded worst case (~4 ops/level, no allocation, no search) is what an
exchange-shaped workload actually buys.

See: `bitmap-walkthrough.md` (worked `prev_occupied(11)` climb), `ladder-casebook.md`
(set/clear that maintain the bits), `bit-manipulation.md` §6 (the 3-move idiom),
`optimization-journey.md` Steps 4–5 (the revealing benchmark and the re-measure).
