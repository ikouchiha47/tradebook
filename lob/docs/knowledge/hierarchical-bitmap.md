# The hierarchical occupancy bitmap (a.k.a. "the best-price bitmap")

How a tick-ladder book finds best bid/ask in O(1) without a tree — and how the
`set`/`clear`/`highest` operations relate.

> See also: `bit-manipulation.md` (the bit primitives + their names/lineage) ·
> `bitmap-walkthrough.md` (bit-is-a-slot primer + worked `prev_occupied`).

## Why it exists
A ladder stores price levels in a contiguous array (fast, cache-friendly), but an array
can't answer "highest occupied price" for free. A *flat* bitmap (one bit per slot) lets
you find the top bit per *word*, but not *which word* holds the top bit — so a sparse
book forces scanning up to `slots/64` words. The **hierarchy** fixes that: each level
summarizes the one below, so finding best is a bounded descent (~3 loads), independent
of gaps.

## The arrays (four layers)
```cpp
std::vector<Level>    hot_;     // the DATA: {total, count} per slot   <- bottom
std::vector<uint64_t> leaf_;    // 1 bit per slot
std::vector<uint64_t> summary_; // 1 bit per leaf WORD
std::vector<uint64_t> root_;    // 1 bit per summary WORD              <- top
```
Each level's **bit i** means "word i of the level below is non-empty." It's a bitmap of a
bitmap.

## Words vs bits
Two different coordinates, easy to conflate:

- A **word** = one entry of the vector, a whole `uint64_t`. `leaf_[1]` is the *second*
  word — an **array index**.
- A **bit** = one position *inside* a word, `0..63`. `leaf_[1]` bit 3 — a **bit position**.

A slot maps to both:
```
leaf WORD = slot / 8      (which entry in leaf_  → array index)
leaf BIT  = slot % 8      (which bit inside it   → bit position 0..7)
(word, bit) → slot = word * 8 + bit
```

**Every level has words and bits** — they're all the same `vector<uint64_t>`, just indexing
different things:

| level | a "word" is | a "bit i" means |
|---|---|---|
| `root_` | an entry of `root_` | summary WORD i is non-empty |
| `summary_` | an entry of `summary_` | leaf WORD i is non-empty |
| `leaf_` | an entry of `leaf_` | slot (word*N + i) is occupied |

The recursion: a level's **bit i** refers to the level-below's **word i**. That's what makes
it fractal — each layer summarizes the words of the one beneath it, so every layer carries
both a word and a bit coordinate.

## Layering (diagram)
*Diagrams use 8-bit words for readability (`>>3`, `&7`, `*8`). Real code uses 64-bit words
(`>>6`, `&63`, `*64`). Same structure.*

16 slots, occupied at 2, 3, 5, 11:
```
root_      [ 0b00000001 ]                     bit 0 → "summary word 0 non-empty"
                 │
                 ▼
summary_   [ 0b00000011 ]                     bit 0 → "leaf word 0 non-empty"
             │        │                       bit 1 → "leaf word 1 non-empty"
             ▼        ▼
leaf_      [ 0b00101100 ]  [ 0b00001000 ]     bit k → "slot (word*8 + k) occupied"
             │ │ │               │
             │ │ └ bit2 → slot 0*8+2 = 2
             │ └── bit3 → slot 0*8+3 = 3
             └──── bit5 → slot 0*8+5 = 5
                                 └ bit3 → slot 1*8+3 = 11
             │
             ▼
hot_       [.][.][L][L][.][L][.][.] [.][.][.][L][.][.][.][.]
           s0 s1 s2 s3 s4 s5 s6 s7  s8 s9 s10 s11 s12 s13 s14 s15
```
So "set slot 11" = "set **word 1, bit 3**" → `leaf_[1] |= (1 << 3)` → `0b00001000`.

## Index arithmetic (a slot maps to each layer)
```
leaf word    = slot       / 64     bit = slot        % 64
summary word = leaf word  / 64     bit = leaf word   % 64
root word    = summary word / 64   bit = summary word % 64
```
(the cheap forms: `>>6` and `&63`)

A slot index is therefore a **base-64 number**: each base-64 digit is one level, and the
digit's value is the **bit position** inside that level's word. With 8-bit words it's
base-8 (3 bits/digit); with 64-bit words it's base-64 (6 bits/digit). The word indices are
just the higher digits combined.

## SET a slot (walk DOWN: slot → words)
Given slot `s`:
```cpp
size_t lw = s >> 6,  lb = s & 63;   // leaf WORD index + bit
size_t sw = lw >> 6, sb = lw & 63;  // summary WORD index + bit
```
Then set the leaf bit, and **only if the leaf word was empty**, set its summary bit; and
only if *that* summary word was empty, set the root bit:
```cpp
if (leaf_[lw] == 0) {
  if (summary_[sw] == 0) root_[sw >> 6] |= 1ull << (sw & 63);
  summary_[sw] |= 1ull << sb;
}
leaf_[lw] |= 1ull << lb;
```
**CLEAR** is the mirror: clear the leaf bit; if the leaf word dropped to `0`, clear its
summary bit; if that summary word dropped to `0`, clear the root bit.
```cpp
leaf_[lw] &= ~(1ull << lb);
if (leaf_[lw] == 0) {
  summary_[sw] &= ~(1ull << sb);
  if (summary_[sw] == 0) root_[sw >> 6] &= ~(1ull << (sw & 63));
}
```
Only *word-level transitions* (empty↔non-empty) touch a higher level → O(1), ≤ 3 levels.
A partial reduce that leaves `count > 0` touches **no bits**.

### `|=` is a bit-write, not an append
`leaf_[lw] |= 1ull << lb` does **not** append. The word is a fixed 64-bit value that already
exists; you set one bit in it:

```
1ull << lb   →  one-hot mask (only bit lb set):  lb = 3 → 0b...00001000
leaf_[lw]    →  the existing word
|=           →  force bit lb ON, leave every other bit unchanged
```
Worked (slot 11 = leaf word 1, leaf bit 3):
```
leaf_[1] before : 0b00000000
mask (1 << 3)   : 0b00001000
OR              : 0b00001000   ← bit 3 now set
```
If the word already had other bits set, they survive — repeated sets at different slots
accumulate into the same word at different bit positions. That *feels* like appending, but
it's a specific bit-write. `1ull` is a 64-bit literal (`unsigned long long`) so shifts up to
bit 63 are well-defined.

`>>` is the inverse: `slot >> N` peels off N low bits (one level's bit position), leaving the
word index. `11 >> 3 = 1` — the `011` leaf bit falls off, leaving leaf word `1`.

## FIND highest / lowest (walk UP: root → slot)
```cpp
int highest() const {                     // best BID
  for (size_t r = root_.size(); r-- > 0; )
    if (root_[r]) {
      size_t sw   = r*64    + (63 - __builtin_clzll(root_[r]));     // summary word
      size_t lw   = sw*64   + (63 - __builtin_clzll(summary_[sw])); // leaf word
      return static_cast<int>(lw*64 + (63 - __builtin_clzll(leaf_[lw]))); // slot
    }
  return -1;                              // empty side
}
// lowest(): same with __builtin_ctzll, scanning up.
```

## The two directions are inverses (the reconciliation)
Both compute the **same `lw` (leaf word index)** — SET from a slot, FIND from the root:

```
SET  (down):  lw = s >> 6                    = 200 >> 6    = 3
FIND (up):    lw = sw*64 + sb                = 0*64 + 3    = 3     ← same
```
because `sw`/`sb` are the high/low halves of `lw`:
```
lw = 3 (0b11):   sw = lw >> 6 = 0     sb = lw & 63 = 3
rearrange:       lw = (sw << 6) | sb  =  sw*64 + sb
```
Same number, reached from opposite ends — that's why the variable name repeats but the
formula looks different. Worked (16-slot, 8-bit example, slot 11) — note **word** (array
index) vs **bit** (position inside it):
```
SET  slot 11
  → leaf    WORD 1, BIT 3     (11/8 = 1, 11%8 = 3)
  → summary WORD 0, BIT 1     ( 1/8 = 0,  1%8 = 1)
  → root    WORD 0, BIT 0     ( 0/8 = 0,  0%8 = 0)

FIND highest
  root    WORD 0 bit 0  → summary WORD 0
  summary WORD 0 bit 1  → leaf    WORD 1
  leaf    WORD 1 bit 3  → slot = 1*8 + 3 = 11
```

## Slot → value
The descent returns a **slot**. From it:
```cpp
price = base + slot * inc;      // price is math off the position
Level& lvl = hot_[slot];        // {total, count} — the size
```
The bitmap holds no price or size; the slot gives both (price by arithmetic, size by the
`hot_` lookup).

## Bit-scan builtins
- `__builtin_clzll(x)` = count leading zeros in a 64-bit word. Highest set bit = `63 - clz(x)`.
- `__builtin_ctzll(x)` = count trailing zeros = lowest set bit position.
- **UB on `x == 0`** — always guard (`if (root_[r])`, `if (summary_[sw])`).
- Portable C++20: `<bit>` → `std::countl_zero` / `std::countr_zero` (defined on 0).

## Sizes
```
                 leaf_         summary_      root_
2,820 slots  →  45 words       1 word         1 word   (2 effective levels)
65,536 slots →  1024 words     16 words       1 word   (3 levels)
```
Each level shrinks ~64×. Memory is `slots/8` bytes for the leaf — tiny **because the band
is bounded**. Covering the full `u32` tick space would be ~512 MB for the bitmap alone.

## Caveats
- **Only covers `hot_`.** Combine with `cold_` (the overflow map): true best =
  `max(hot_best, largest cold key)` for bids, `min(...)` for asks. Off-grid/in-band prices
  can live in `cold_` too.
- **Bit must track `count == 0` exactly.** Set on 0→1, clear on 1→0. Best done by letting
  the **ladder own `add_order`/`remove_order`** so bits can't drift from counts.
- **`sw = highest bit in root` assumes root_ is one word.** General form:
  `sw = r*64 + (63 - clz(root_[r]))`.
- **Direction stays in the Book.** The ladder/bitmap is side-agnostic; bids call
  `highest()`, asks call `lowest()`.
