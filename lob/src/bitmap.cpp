#include "bitmap.h"
#include <cstdint>

#define clz __builtin_clzll
#define ctz __builtin_ctzll

constexpr size_t kBitLen = 6;
constexpr uint64_t kMask = (1 << kBitLen) - 1; // 2^6 - 1
/*
 *
 * sw   = highest set bit in root      → summary word index
 * sb   = highest set bit in summary[sw] → leaf word index within the block
 * lw   = sw * 64 + sb
 * lb   = highest set bit in leaf[lw]  → bit within that word
 * slot = lw * 64 + lb                 ← the answer: which slot
 *
 * --------
 *
 * for base-8, bit_len 3

  slot 11 = 0b 0000 1011
                 │ ││
      bits [5:3] ┘ └│── (summary bits)
       bits [2:0] ──┘

  bits [2:0] = 0b011 = 3   → LEAF bit    (bit 3 in the leaf word)
  bits [5:3] = 0b001 = 1   → SUMMARY bit (bit 1 in the summary word)
  bits [6+]  = 0b0   = 0   → ROOT bit

  lw = 11 >> 3 = 1;   // leaf WORD = 1     (0b0000 0001)
  lb = 11 &  7 = 3;   // leaf BIT  = 3     (0b011)
  sw = 1 >> 3 = 0;    // summary WORD = 0   (0b0000 0000)
  sb = 1 &  7 = 1;    // summary BIT  = 1   (0b001)
 *
 * */

std::tuple<size_t, size_t, size_t, size_t> Occupancy::addr(size_t slot) {
  size_t lw = slot >> kBitLen, lb = slot & kMask;
  size_t sw = lw >> kBitLen, sb = lw & kMask;

  return {lw, lb, sw, sb};
}

void Occupancy::set(size_t slot) {
  // size_t lw = slot >> BIT_LEN, lb = slot & 63;
  // size_t sw = lw >> BIT_LEN, sb = lw & 63;

  auto [lw, lb, sw, sb] = addr(slot);

  if(leaf_[lw] == 0) {
    if(summary_[sw] == 0) {
      size_t rw = sw >> kBitLen, rb = (sw & kMask);
      root_[rw] |= 1ull << rb;
    }

    summary_[sw] |= 1ull << sb; // ull unsigned long long -> 64-bit, value = 1
  }

  leaf_[lw] |= 1ull << lb;
}

void Occupancy::clear(size_t slot) {
  auto [lw, lb, sw, sb] = addr(slot);

  // lw sw are the index
  // lb and sb are the bit which are used in slot
  // calculation
  leaf_[lw] &= ~(1ull << lb);
  if(leaf_[lw] == 0) {
    summary_[sw] &= ~(1ull << sb);

    if(summary_[sw] == 0) {
      root_[sw >> kBitLen] &= ~(1ull << (sw & kMask));
    }
  }
}

int Occupancy::highest() const {
  for (int64_t r = static_cast<int64_t>(root_.size()) - 1; r >= 0; --r) {
    if (root_[r]) {
      const size_t sw = static_cast<size_t>(r) * 64 + (kMask - clz(root_[r]));
      const size_t lw = sw * 64 + (kMask - clz(summary_[sw]));
      return static_cast<int>(lw * 64 + (kMask - clz(leaf_[lw])));
    }
  }
  return -1;  // empty
}

int Occupancy::lowest() const {
  for (size_t r = 0; r < root_.size(); ++r) {
    if (root_[r]) {
      const size_t sw = r * 64 + ctz(root_[r]);
      const size_t lw = sw * 64 + ctz(summary_[sw]);
      return static_cast<int>(lw * 64 + ctz(leaf_[lw]));
    }
  }
  return -1;  // empty
}

// highest occupied slot strictly below `slot` (or -1)
int Occupancy::prev_occupied(int slot) const {
  if (slot <= 0) return -1;
  const size_t s = static_cast<size_t>(slot);
  // level 0 — leaf word holding slot, bits below (slot & 63)
  const size_t lw = s >> kBitLen, lb = s & kMask;
  if (lb) {
    const uint64_t hit = leaf_[lw] & ((1ull << lb) - 1);
    if (hit) return static_cast<int>(lw * 64 + (kMask - clz(hit)));
  }
  // level 1 — leaf words strictly below lw, via the summary word
  const size_t sw = lw >> kBitLen, sb = lw & kMask;
  if (sb) {
    const uint64_t hit = summary_[sw] & ((1ull << sb) - 1);
    if (hit) {
      const size_t plw = sw * 64 + (kMask - clz(hit));
      return static_cast<int>(plw * 64 + (kMask - clz(leaf_[plw])));
    }
  }
  // level 2 — summary words strictly below sw, via the root words
  for (size_t r = sw >> kBitLen;; ) {
    uint64_t word = root_[r];
    if (r == (sw >> kBitLen)) {
      const size_t rb = sw & kMask;
      word &= (rb ? ((1ull << rb) - 1) : 0ull);
    }
    if (word) {
      const size_t psw = r * 64 + (kMask - clz(word));
      const size_t plw = psw * 64 + (kMask - clz(summary_[psw]));
      return static_cast<int>(plw * 64 + (kMask - clz(leaf_[plw])));
    }
    if (r == 0) break;
    --r;
  }
  return -1;
}

// lowest occupied slot strictly above `slot` (or -1)
int Occupancy::next_occupied(int slot) const {
  if (slot < 0) slot = -1;
  const size_t s = static_cast<size_t>(slot) + 1;
  if (s >= leaf_.size() * 64) return -1;
  // level 0 — leaf word holding s, bits at/above (s & 63)
  const size_t lw = s >> kBitLen, lb = s & kMask;
  {
    const uint64_t hit = leaf_[lw] & ~((1ull << lb) - 1);
    if (hit) return static_cast<int>(lw * 64 + ctz(hit));
  }
  // level 1 — leaf words strictly above lw, via the summary word
  const size_t sw = lw >> kBitLen, sb = lw & kMask;
  {
    const uint64_t hit = summary_[sw] & ~((1ull << (sb + 1)) - 1);
    if (hit) {
      const size_t nlw = sw * 64 + ctz(hit);
      return static_cast<int>(nlw * 64 + ctz(leaf_[nlw]));
    }
  }
  // level 2 — summary words strictly above sw, via the root words
  for (size_t r = sw >> kBitLen; r < root_.size(); ++r) {
    uint64_t word = root_[r];
    if (r == (sw >> kBitLen)) {
      const size_t rb = sw & kMask;
      word &= ~((1ull << (rb + 1)) - 1);
    }
    if (word) {
      const size_t nsw = r * 64 + ctz(word);
      const size_t nlw = nsw * 64 + ctz(summary_[nsw]);
      return static_cast<int>(nlw * 64 + ctz(leaf_[nlw]));
    }
  }
  return -1;
}
