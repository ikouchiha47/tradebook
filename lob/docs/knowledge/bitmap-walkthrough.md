# Worked example — the occupancy bitmap end to end (with prices)

A full trace: four orders added to a book, then the best-price walk. **8-bit words** for
readability (`>>3`, `&7`, `*8`); the real code uses 64-bit words (`>>6`, `&63`, `*64`) —
swap the constants and it's identical.

## Setup
```
base = $100.00   (raw 1,000,000)
inc  = $0.01     (raw 100)          // one penny
price(slot) = $100.00 + slot * $0.01
```
16 slots → `leaf_` = 2 words, `summary_` = 1 word, `root_` = 1 word. All start at 0.

```
slot:   0     1     2     3     4     5   ...   11
price: 100.00 100.01 100.02 100.03 100.04 100.05  100.11
```

`decode(slot)`:
```cpp
lw = slot >> 3;  lb = slot & 7;   // leaf word + bit
sw = lw   >> 3;  sb = lw   & 7;   // summary word + bit
```

## Add four bid orders (four `set` calls)
```
bid @ $100.02 → slot 2
bid @ $100.03 → slot 3
bid @ $100.05 → slot 5
bid @ $100.11 → slot 11
```

### `set(2)` — first bit anywhere (propagates all 3 levels)
```
decode(2): lw=0 lb=2 sw=0 sb=0
leaf_[0]==0? yes → summary_[0]==0? yes → root_[0] |= 1<<0
                                        summary_[0] |= 1<<0
                                        leaf_[0]    |= 1<<2
```
```
root_    [ 00000001 ]
summary_ [ 00000001 ]
leaf_    [ 00000100 ]   [ 00000000 ]
```

### `set(3)` — same leaf word already non-empty → leaf only
```
decode(3): lw=0 lb=3
leaf_[0]==0? NO → skip propagation
leaf_[0] |= 1<<3
```
```
root_    [ 00000001 ]
summary_ [ 00000001 ]
leaf_    [ 00001100 ]   [ 00000000 ]
```

### `set(5)` — same again → leaf only
```
leaf_[0] |= 1<<5
```
```
root_    [ 00000001 ]
summary_ [ 00000001 ]
leaf_    [ 00101100 ]   [ 00000000 ]
```

### `set(11)` — a NEW leaf word appears → leaf + summary (root already set)
```
decode(11): lw=1 lb=3 sw=0 sb=1
leaf_[1]==0? yes → summary_[0]==0? NO → skip root
                    summary_[0] |= 1<<1
                    leaf_[1]    |= 1<<3
```
```
root_    [ 00000001 ]
summary_ [ 00000011 ]        ← bit 1 now set
leaf_    [ 00101100 ]   [ 00001000 ]
```

## Final state, annotated
```
bit index:  7 6 5 4 3 2 1 0
root_[0]    0 0 0 0 0 0 0 1   bit 0  → summary word 0 non-empty
summary_[0] 0 0 0 0 0 0 1 1   bit 0  → leaf word 0 non-empty
                              bit 1  → leaf word 1 non-empty
leaf_[0]    0 0 1 0 1 1 0 0   bits 2,3,5 → slots 2,3,5   ($100.02, .03, .05)
leaf_[1]    0 0 0 0 1 0 0 0   bit 3      → slot 1*8+3 = 11 ($100.11)
```

## `highest()` — find the best bid
```cpp
for (size_t r = root_.size(); r-- > 0; )
  if (root_[r]) {
    sw   = r*8  + (7 - clz(root_[r]));
    lw   = sw*8 + (7 - clz(summary_[sw]));
    slot = lw*8 + (7 - clz(leaf_[lw]));
  }
```
Step by step (r = 0, the only root word):
```
root_[0] = 0b00000001
  clz(0b00000001) = 7   →  7-7 = 0    →  sw = 0*8 + 0 = 0

summary_[0] = 0b00000011
  clz(0b00000011) = 6   →  7-6 = 1    →  lw = 0*8 + 1 = 1

leaf_[1] = 0b00001000
  clz(0b00001000) = 4   →  7-4 = 3    →  slot = 1*8 + 3 = 11
```

Why each step is the *highest*: it takes the top set bit (`7 - clz`) of the chosen word,
and bit position = index. `summary_[0]=0b11` picked bit 1 → leaf word 1 — the word holding
$100.11, which is *above* the $100.02–.05 in leaf word 0.

## Slot → value
```
slot  = 11
price = $100.00 + 11*$0.01 = $100.11       (raw: 1,000,000 + 11*100 = 1,001,100)
Level& lvl = hot_[11];                       // { total, count }
```
**Best bid = $100.11, size = `hot_[11].total`.**

## What this shows
- `set` flips **one leaf bit**, and propagates up **only when a whole word transitions
  empty → non-empty**: `set(2)` touched all 3 levels; `set(3)`/`set(5)` touched only the
  leaf; `set(11)` touched leaf + summary (summary word 0 was already non-empty, so root
  didn't change).
- `highest` did **not** scan prices. It read `root_ → summary_ → leaf_` (3 loads) and
  `clz`'d each, landing straight on slot 11.
- `lowest()` (best ask) is the mirror on a **separate** bitmap for the other side: scan
  root bottom-up and use `ctz` (lowest set bit) at each level.

## Why rescan only when the best level empties (case-by-case)

Caching the best price is *correct* because the cache goes stale at a **detectable** point:
the instant the current best level empties. Every other event leaves best unchanged.

Setup for all cases — occupied slots **{3, 5, 11}**, best = **11**, 8-bit words:
```
leaf_[0] (slots 0..7)  = 0b00101000    bits set: 3, 5
leaf_[1] (slots 8..15) = 0b00001000    bit set: 11
summary_[0]            = 0b00000011    leaf words 0 and 1 non-empty
root_[0]               = 0b00000001    summary word 0 non-empty
best_bid_ = occ_.highest() = 11        (cached)
```

### Case A — add at slot 13 (a *better* price)
```
13 > best_bid_(11)  →  best_bid_ = 13          // ONE COMPARE, no descent
occ_.set(13): 13 → leaf word 1, bit 5 → leaf_[1] |= 1<<5 → 0b00101000
              leaf_[1] was already non-empty → no summary/root change
```
Best updated by comparison. **No rescan.**

### Case B — add at slot 4 (a *worse* price)
```
4 < best_bid_(11)   →  best unchanged
occ_.set(4): leaf_[0] |= 1<<4 → 0b00111000
```
**No rescan.**

### Case C — remove slot 5 (NOT the best)
```
occ_.clear(5): 5 → leaf word 0, bit 5 → leaf_[0] &= ~(1<<5) → 0b00001000
leaf_[0] != 0  → no summary change
the BEST bit (11, in leaf_[1]) was never touched  →  cache still VALID
                                                                 (best stays 11)
```
**No rescan.**

### Case D — remove slot 11 (THE best)
```
occ_.clear(11): 11 → leaf word 1, bit 3 → leaf_[1] &= ~(1<<3) → 0b00000000
leaf_[1] == 0  →  clear summary bit 1: summary_[0] &= ~(1<<1) → 0b00000001
summary_[0] != 0 → root unchanged
the BEST bit is now GONE  →  cache STALE  →  rescan:
  occ_.highest():
    root_[0]    = 0b00000001  → highest set bit 0 → summary word 0
    summary_[0] = 0b00000001  → highest set bit 0 → leaf word 0
    leaf_[0]    = 0b00101000  → highest set bit 5 → slot 5
  best_bid_ = 5
```
**Only here is a rescan needed** — because only here did the bit the cache depended on get
cleared. (Removing 11 left {3, 5}, so the new best is 5.)

## The rule
```
best changes  ⟺  a better level is created  →  compare/update    O(1)
             OR  the current best level empties →  rescan (descent)  rare
anything else →  best is already correct, do nothing
```
Detect the second case with one check: `if (erasing the best price) rescan();`. The bitmap
tells you "the best" in O(1) when you *do* need it, and the cache skips it the rest of the
time. hsdxpro does exactly this (`if best == price { best = last()/first() }`); itchbook
does it via `level_emptied(idx == cursor_)`. Doing a descent on *every* message (our
current `refresh()`) is the wasted work this avoids.

## `prev_occupied(slot)` / `next_occupied(slot)` — neighbour queries

`slot` is the anchor slot. The point to hold onto: **`slot` is the one value that gets split
into the three layers.** The root/summary/leaf indices are just different **bit-slices of
the same `slot`**:

```
slot  =  ... [ root digit ][ summary digit ][ leaf digit ]        (base-64 digits, 6 bits each)

leaf    word = slot >> 6          leaf    bit = slot       & 63
summary word = slot >> 12         summary bit = (slot >> 6)  & 63
root    word = slot >> 18         root    bit = (slot >> 12) & 63
```

So `slot` is not "in" one layer — it *decomposes* into all three at once.

### What they return
```
prev_occupied(slot) = the highest occupied slot STRICTLY BELOW slot   (or -1)
next_occupied(slot) = the lowest  occupied slot STRICTLY ABOVE slot   (or -1)
```

### How — same hierarchy, anchored at `slot`, masked one side
```
prev_occupied(slot):
  1) leaf_[slot>>6] & ((1ull << (slot&63)) - 1)          # bits BELOW slot in its leaf word
        if != 0  → answer = (slot>>6)*64 + (63 - clz(hit))   # the neighbour slot
  2) else summary_[slot>>12] & ((1ull << ((slot>>6)&63)) - 1)   # leaf words BELOW, in its summary word
       if != 0  → highest non-empty leaf word → its top bit
  3) else root_[slot>>18]   & ((1ull << ((slot>>12)&63)) - 1)   # summary words BELOW, in its root word
       → its top bit → that summary word's top bit → that leaf word's top bit
```
`next_occupied` is the mirror: mask bits **above** (`word & ~((1ull << (bit+1)) - 1)`) and use
`ctz`, jumping **up** (leaf → summary → root).

### Worked
Occupied slots **{3, 5, 40, 100, 200}** (64-bit words → one word per 64 slots):
```
leaf_[0]=bits{3,5,40}   leaf_[1]=bit{36}  (=100-64)   leaf_[3]=bit{8} (=200-192)

prev_occupied(100):
  slot=100 : leaf word 1, bit 36
  1) leaf_[1] & ((1<<36)-1) = 0            # nothing below 100 in word 1
  2) summary: highest non-empty leaf word < 1 → word 0
     leaf_[0] top bit = 40  → return 40     ✓

next_occupied(100):
  slot=100 : leaf word 1, bit 36
  1) leaf_[1] & ~((1<<37)-1) = 0           # nothing above 100 in word 1
  2) summary: lowest non-empty leaf word > 1 → word 3
     leaf_[3] low bit = 8 → return 192+8 = 200   ✓
```

### Why this is the same machinery as `highest()` / `lowest()`
```
highest()          : start at the ROOT  → top set bit → descend
prev_occupied(slot)  : start at slot's digits → mask below → descend / ascend to the neighbour
```
`prev_occupied`/`next_occupied` are `highest()`/`lowest()` **anchored at `slot`** — you re-split
the anchor into root/summary/leaf coordinates and walk from there. Cost is O(hierarchy)
(a few word-reads + `clz`/`ctz`), **independent of the gap**.

### Usage
```
top()/depth :  best = occ_.highest();  loop { emit(best); best = prev_occupied(best); }
cursor      :  when the best level empties: best = prev_occupied(old_best)
             (asks: next_occupied / lowest)
```
i.e., `prev/next_occupied` are the occupied→occupied **jump**, used instead of stepping
`i--`/`i++` through empty slots — which is what makes `top()` flat across dense and sparse.


