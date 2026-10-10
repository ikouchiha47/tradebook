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
