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
