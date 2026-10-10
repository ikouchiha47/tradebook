# Bit manipulation techniques (the toolkit behind the occupancy bitmap)

Every trick in our book is **standard** — nothing ad-hoc. This is the reference: the
primitives, the canonical idioms, and where each is known from. The *structure* they build
(the layered bitmap) lives in `hierarchical-bitmap.md`; the worked trace in
`bitmap-walkthrough.md`.

## 1. An integer is a set of small integers
A `uint64_t` is 64 booleans — **bit i = "is element i in the set?"**. That's a **bitset** /
**bitmap**. All set algebra is bit ops:

```cpp
(w >> i) & 1      // member?  (test)
w |=  1ull << i   // insert
w &= ~(1ull << i) // remove
w ^=  1ull << i   // toggle
```

## 2. One-hot and range masks
The bread and butter. Note they come in **strict** and **inclusive** flavours:
```
1ull << n              one-hot: only bit n
(1ull << n) - 1        low n bits: 0 .. n-1        "strictly below n"
~((1ull << n) - 1)     bits n .. 63               "n and above"
~((1ull << (n+1))-1)   bits n+1 .. 63             "strictly above n"
(1ull << (n+1)) - 1    bits 0 .. n                "n and below"
```
`(1<<n)-1` ("n ones") is the first trick in every bit-twiddling reference. The `- 1` after a
shift is what turns "a 1 at n" into "everything below n".

## 3. Highest / lowest set bit — CLZ / CTZ
```
highest set bit position = (W - 1) - countl_zero(x)     // W = word width
lowest  set bit position =           countr_zero(x)
```
`countl_zero` (**CLZ**, count-leading-zeros) counts zeros above the top 1, so `(W-1) - clz`
lands on that 1's position. It is also `std::bit_width(x) - 1` = `floor(log2(x))`. For an
8-bit word that's `7 - clz`; for our 64-bit words, `63 - clz`.

```
8-bit: 7 - clz(0b00001100) = 7 - 4 = 3      # first 1 at bit 3
64-bit:63 - clz(same value) = 63 - 60 = 3   # same position, wider count
```
- **Hardware:** x86 `BSF`/`BSR` (legacy), `TZCNT`/`LZCNT`/`POPCNT` (BMI1/ABM); ARM `CLZ`.
- **C:** `__builtin_clz[l]/ctz[l]`.
- **C++20 `<bit>`:** `std::countl_zero` / `std::countr_zero` / `std::bit_width` (defined on `0`).
- **UB on `x == 0`** for the builtins — always guard (`if (word)`).

## 4. Count set bits — POPCOUNT
`std::popcount(w)` / `__builtin_popcountll(w)` → hardware `POPCNT`. Gives **rank** and
occupancy counts without scanning. (Not used in the book yet; standard in succinct bitmaps.)

## 5. Word/bit coordinates: `>>` is divide, `&` is modulo
For a word of `W = 2^k` bits, an index splits into "which word" and "which bit":
```
word = index >> k          // index / W
bit  = index & (W - 1)     // index % W
```
64-bit words → `>>6` / `&63`. 8-bit toy → `>>3` / `&7`. `x >> k` is integer ÷2ᵏ (floor), so
`3 >> 3 = 0` — it's "how many groups of 8/64".

## 6. The "half mask" idiom (strictly below / above) — the 3-move query
This is the whole of `prev_occupied` / `next_occupied` within one word:
```
1. MASK     (1<<lb)-1          below        |  ~((1<<(lb+1))-1)   above
2. AND      word & mask                     |  word & mask
3. EXTRACT  (W-1) - clz (top)               |  ctz (bottom)
```
Mask off the half you don't want, AND, read the top (or bottom) surviving 1. Empty result →
the answer isn't in this word → climb a level and repeat.

## 7. Making it hierarchical (skip empty words)
A **bitmap of bitmaps**: each level's bit `i` = "word `i` of the level below is non-empty."
- **Write** (set/clear) touches a higher level **only** on an empty↔non-empty transition.
- **Query** (highest/lowest/next) descends root→summary→leaf in ~levels steps, skipping whole
  empty words — the "jump".
- **Cost:** O(levels) ≈ 2–3 loads + one `clz`/`ctz` each, independent of the gap. That's why
  `top()` is flat on dense *and* sparse books.

## 8. Names & lineage (so it reads as engineering, not magic)
```
masks / clz / one-hot        Hacker's Delight (Warren); "Bit Twiddling Hacks" (S. Anderson)
find next/prev set bit       CPU BSF/BSR/LZCNT/TZCNT; libc ffs/ffsl
hierarchical bitmap          = "summary bitmaps" / multi-level bitmap
                             flat cousin of van Emde Boas tree, x-fast / y-fast trie
leaf+summary "find block"    TLSF allocator ("Two-Level Segregated Fit") — same shape, O(1)
same idea in the kernel      Linux buddy allocator; find_next_bit / bitmap_find_next_zero_area
large-set containers         Roaring bitmaps (hierarchical containers)
page tables / radix trees    "each level summarizes the level below"
rank / select on bitvectors  succinct data structures
```

## 9. Where each is used *here*
```
one-hot, range masks   Occupancy::set/clear + prev/next_occupied   src/bitmap.cpp
CLZ / CTZ              Occupancy::highest/lowest/prev/next         src/bitmap.cpp
word/bit coordinates   every `>>6` / `&63` in the ladder + bitmap
hierarchical bitmap    leaf_ / summary_ / root_
```
See also: `hierarchical-bitmap.md` (structure, SET/FIND, sizes) ·
`bitmap-walkthrough.md` (bit-is-a-slot primer + worked `prev_occupied`).

## 10. Gotchas
- `clz`/`ctz` are **UB on 0** — guard, or use the `std::count*_zero` forms.
- Shift with `1ull` (not `1`) so bit 31–63 is well-defined; shifting ≥ width is UB.
- **Strict vs inclusive** differ by one `-1`/`+1` — `(1<<lb)-1` excludes the anchor's own bit;
  `(1<<(lb+1))-1` would include it and wrongly return the anchor itself.
- `|=` is a **bit-write into an existing word**, not an append (repeated inserts accumulate
  at different bit positions).
- `%` == `&` only for power-of-two moduli — fine here (64), not in general.

## Cheat sheet
```
goal                         code
build set {2,3,5}            (1ull<<2)|(1ull<<3)|(1ull<<5)
member i?                    (w >> i) & 1
insert / remove i            w |= 1ull<<i    /    w &= ~(1ull<<i)
low n bits (below n)         (1ull<<n) - 1
strictly above n             ~((1ull<<(n+1)) - 1)
highest set bit              (W-1) - std::countl_zero(w)     [w != 0]
lowest set bit               std::countr_zero(w)            [w != 0]
count set bits               std::popcount(w)
word / bit of index          i >> k    /    i & (W-1)
prev element < x             highest set bit of  w & ((1<<lb)-1)
next element > x             lowest  set bit of  w & ~((1<<(lb+1))-1)
```
