# Ladder casebook — add and remove, bit by bit (8-bit words)

The encoding is the whole design: **a slot is a price index, and a bit is a slot.**
`encode`: slot → `(word, bit)` by `>>3` / `&7`. `decode`: slot → price by
`base + slot*inc`, and slot → size by `hot_[slot]`. Here is every `set` and `clear` for
one concrete sequence, with the full arrays. 8-bit words for readability; the code uses
64-bit (`>>6`/`&63`) — identical shape.

Setup: bids, `base = $100.00`, `inc = $0.01`, band 128 slots → 16 leaf words, **2 summary
words**, 1 root word. All start at 0.

```
slot → price:  2 → $100.02    3 → $100.03    5 → $100.05    4 → $100.04    11 → $100.11
slot → word:   lw = slot >> 3,  lb = slot & 7
               sw = lw >> 3,    sb = lw & 7
```

## ADD — slots 2, 3, 5, 4, 11

### `add 2` — first bit anywhere (touches all 3 levels)
```
decode(2):  lw=0 lb=2   sw=0 sb=0
leaf_[0]==0? yes → summary_[0]==0? yes → root_[0] |= 1<<0
                                              summary_[0] |= 1<<0
                                              leaf_[0]    |= 1<<2
leaf_[0]    = 0b00000100
summary_[0] = 0b00000001    summary_[1] = 0b00000000
root_[0]    = 0b00000001
```

### `add 3` — same word, no higher touch
```
decode(3):  lw=0 lb=3
leaf_[0] |= 1<<3          (word already non-empty → summary/root untouched)
leaf_[0] = 0b00001100
```

### `add 5`
```
decode(5):  lw=0 lb=5
leaf_[0] |= 1<<5
leaf_[0] = 0b00101100
```

### `add 4`
```
decode(4):  lw=0 lb=4
leaf_[0] |= 1<<4
leaf_[0] = 0b00111100     → slots {2,3,4,5}
```

### `add 11` — new leaf word (touches summary, not root)
```
decode(11): lw=1 lb=3   sw=0 sb=1      (11 = 8 + 3)
leaf_[1]==0? yes → summary_[0] |= 1<<1     (summary already non-empty → root untouched)
leaf_[1] |= 1<<3
leaf_[1]    = 0b00001000
summary_[0] = 0b00000011    summary_[1] = 0b00000000
```

Final state after the five adds:

```
leaf_[0]    = 0b00111100   → slots {2,3,4,5}
leaf_[1]    = 0b00001000   → slot  {11}
leaf_[2..15]= 0b00000000
summary_[0] = 0b00000011   → leaf words {0,1} non-empty
summary_[1] = 0b00000000   → (empty: no occupied slot ≥ 64)
root_[0]    = 0b00000001   → summary word {0} non-empty
```

## REMOVE — slots 4, then 11

### `remove 4` — word stays non-empty (no higher touch)
```
decode(4):  lw=0 lb=4
leaf_[0] &= ~(1<<4)  →  0b00101100     → slots {2,3,5}
leaf_[0] != 0 → summary/root untouched. Level emptied (count 1→0); 4 wasn't the
best, so the cached best is untouched.
```

### `remove 11` — leaf word empties (clears one summary bit)
```
decode(11): lw=1 lb=3
leaf_[1] &= ~(1<<3)  →  0b00000000     → empty
leaf_[1]==0 → clear summary_[0] bit 1  →  summary_[0] = 0b00000001 → {0}
summary_[0] != 0 → root untouched.
Level emptied; 11 *was* the best → rescan: highest() descends
root bit 0 → summary word 0 → highest set bit = leaf word 0 → its top bit = 5.
New best bid = slot 5 = $100.05.
```

## The rule these cases prove

A higher level is touched **only on a word-level transition** (empty↔non-empty):
```
set:    leaf word was empty    → set its summary bit (and root, if that was empty)
clear:  leaf word went empty   → clear its summary bit (and root, if that went empty)
```
Adds 3/5/4 and remove 4 touched **one word only**. Adds 2/11 and remove 11 touched two
levels. A partial reduce (level still rests) touches **no bits at all** — the occupancy
tracks `count == 0` transitions, not shares.

See: `hierarchical-bitmap.md` (structure), `bitmap-walkthrough.md` (bit-is-a-slot primer
+ the `prev_occupied` climb), `best-price-rescan.md` (why the cached best only moves on
level-clear).
