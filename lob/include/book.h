#pragma once
#include "bitmap.h"
#include "itch.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------
struct Order {
  uint64_t ref = 0;
  int64_t price = 0;
  uint32_t qty = 0;
  char side = 0;
};

struct Level {
  uint32_t total = 0;
  uint32_t count = 0;
};

struct LevelRow {
  int64_t price = 0;
  uint32_t total = 0;
  uint32_t count = 0;
};

struct Depth {
  std::vector<LevelRow> bids, asks;
};

struct TopOfBook {
  int64_t bid_px = 0, ask_px = 0;
  uint32_t bid_sz = 0, ask_sz = 0;
};

struct Counters {
  uint64_t unknown_ref = 0;
  uint64_t clamped = 0;
  uint64_t crossed = 0;
};

// ---------------------------------------------------------------------------
// Tick ladder for one side. Owns occupancy so highest()/lowest() work.
//   add_order    : a new order arrived  -> count++, total += qty
//   sub_share    : partial reduce       -> total -= qty (order still rests)
//   remove_order : an order left        -> count--, total -= qty
// ---------------------------------------------------------------------------
class PriceLadder {
 public:
  PriceLadder() = default;
  PriceLadder(int64_t base, int64_t inc, size_t slots)
      : base_(base), inc_(inc), slots_(slots), hot_(slots), occ_(slots) {}

  void add_order(int64_t px, uint32_t qty);
  void sub_share(int64_t px, uint32_t qty);
  bool remove_order(int64_t px, uint32_t qty);  // true if the level emptied

  Level* find(int64_t px);
  int64_t highest() const;    // best bid price, 0 if none
  int64_t lowest() const;     // best ask price, 0 if none
  void top(bool is_bid, size_t n, std::vector<LevelRow>& out) const;

 private:
  int64_t base_ = 0, inc_ = 0;
  size_t slots_ = 0;
  std::vector<Level> hot_;
  std::map<int64_t, Level> cold_;
  Occupancy occ_;

  bool in_hot(int64_t px) const {
    if (px < base_ || inc_ == 0) return false;
    return (px - base_) % inc_ == 0 && index(px) < slots_;
  }
  size_t index(int64_t px) const { return static_cast<size_t>((px - base_) / inc_); }
  Level& at(int64_t px) { return in_hot(px) ? hot_[index(px)] : cold_[px]; }
};

// ---------------------------------------------------------------------------
// Book A — std::map levels (reference implementation)
// ---------------------------------------------------------------------------
struct NasdqBook {
  std::unordered_map<uint64_t, Order> orders;
  std::map<int64_t, Level> bids, asks;
  TopOfBook cached{};
  Counters stats{};
  int64_t best_bid_ = 0, best_ask_ = 0;  // cached; rescan only when the best empties
  uint32_t best_bid_sz_ = 0, best_ask_sz_ = 0;  // cached size of the best level

  void add_order(int64_t px, char side, uint32_t qty) {
    Level& l = (side == 'B') ? bids[px] : asks[px];
    l.total += qty;
    l.count += 1;
    if (side == 'B') {
      if (px > best_bid_)      { best_bid_ = px; best_bid_sz_ = qty; }
      else if (px == best_bid_) best_bid_sz_ = l.total;
    } else {
      if (best_ask_ == 0 || px < best_ask_) { best_ask_ = px; best_ask_sz_ = qty; }
      else if (px == best_ask_)             best_ask_sz_ = l.total;
    }
  }
  void sub_share(int64_t px, char side, uint32_t qty) {
    auto& m = (side == 'B') ? bids : asks;
    auto it = m.find(px);
    if (it == m.end()) return;
    it->second.total -= qty;                          // level still rests: best unchanged
    if (side == 'B' && px == best_bid_) best_bid_sz_ = it->second.total;
    if (side == 'S' && px == best_ask_) best_ask_sz_ = it->second.total;
  }
  void remove_order(int64_t px, char side, uint32_t qty) {
    auto& m = (side == 'B') ? bids : asks;
    auto it = m.find(px);
    if (it == m.end()) return;
    it->second.total -= qty;
    if (--it->second.count == 0) {
      m.erase(it);
      if (side == 'B' && px == best_bid_) {
        best_bid_ = bids.empty() ? 0 : bids.rbegin()->first;
        best_bid_sz_ = bids.empty() ? 0 : bids.rbegin()->second.total;
      }
      if (side == 'S' && px == best_ask_) {
        best_ask_ = asks.empty() ? 0 : asks.begin()->first;
        best_ask_sz_ = asks.empty() ? 0 : asks.begin()->second.total;
      }
    } else {
      if (side == 'B' && px == best_bid_) best_bid_sz_ = it->second.total;
      if (side == 'S' && px == best_ask_) best_ask_sz_ = it->second.total;
    }
  }
  Level* find_level(int64_t px, char side) {
    auto& m = (side == 'B') ? bids : asks;
    auto it = m.find(px);
    return it == m.end() ? nullptr : &it->second;
  }
  int64_t best_bid() const { return best_bid_; }
  int64_t best_ask() const { return best_ask_; }
  uint32_t best_bid_sz() const { return best_bid_sz_; }
  uint32_t best_ask_sz() const { return best_ask_sz_; }
  void top(char side, size_t n, std::vector<LevelRow>& out) const {
    if (side == 'B') {
      for (auto it = bids.rbegin(); it != bids.rend() && out.size() < n; ++it)
        out.push_back({it->first, it->second.total, it->second.count});
    } else {
      for (auto it = asks.begin(); it != asks.end() && out.size() < n; ++it)
        out.push_back({it->first, it->second.total, it->second.count});
    }
  }
};

// ---------------------------------------------------------------------------
// Book B — tick ladder levels (needs a band: base, inc, slots)
// ---------------------------------------------------------------------------
struct NasdqBookWithLadder {
  std::unordered_map<uint64_t, Order> orders;
  PriceLadder bids, asks;
  TopOfBook cached{};
  Counters stats{};
  int64_t best_bid_ = 0, best_ask_ = 0;  // cached; rescan only when the best empties
  uint32_t best_bid_sz_ = 0, best_ask_sz_ = 0;  // cached size of the best level

  NasdqBookWithLadder(int64_t base, int64_t inc, size_t slots)
      : bids(base, inc, slots), asks(base, inc, slots) {}

  void add_order(int64_t px, char side, uint32_t qty) {
    ladder(side).add_order(px, qty);
    if (side == 'B') {
      if (px > best_bid_)      { best_bid_ = px; best_bid_sz_ = qty; }
      else if (px == best_bid_) best_bid_sz_ += qty;
    } else {
      if (best_ask_ == 0 || px < best_ask_) { best_ask_ = px; best_ask_sz_ = qty; }
      else if (px == best_ask_)             best_ask_sz_ += qty;
    }
  }
  void sub_share(int64_t px, char side, uint32_t qty) {
    ladder(side).sub_share(px, qty);
    if (side == 'B' && px == best_bid_) best_bid_sz_ -= qty;
    if (side == 'S' && px == best_ask_) best_ask_sz_ -= qty;
  }
  void remove_order(int64_t px, char side, uint32_t qty) {
    if (!ladder(side).remove_order(px, qty)) {  // level still rests: best unchanged
      if (side == 'B' && px == best_bid_) best_bid_sz_ -= qty;
      if (side == 'S' && px == best_ask_) best_ask_sz_ -= qty;
      return;
    }
    if (side == 'B' && px == best_bid_) {        // best emptied: rescan px + size
      best_bid_ = bids.highest();
      Level* l = best_bid_ ? bids.find(best_bid_) : nullptr;
      best_bid_sz_ = l ? l->total : 0;
    }
    if (side == 'S' && px == best_ask_) {
      best_ask_ = asks.lowest();
      Level* l = best_ask_ ? asks.find(best_ask_) : nullptr;
      best_ask_sz_ = l ? l->total : 0;
    }
  }
  Level* find_level(int64_t px, char side) { return ladder(side).find(px); }
  int64_t best_bid() const { return best_bid_; }
  int64_t best_ask() const { return best_ask_; }
  uint32_t best_bid_sz() const { return best_bid_sz_; }
  uint32_t best_ask_sz() const { return best_ask_sz_; }
  void top(char side, size_t n, std::vector<LevelRow>& out) const {
    if (side == 'B') bids.top(true, n, out);
    else             asks.top(false, n, out);
  }

 private:
  PriceLadder& ladder(char side) { return (side == 'B') ? bids : asks; }
};

// ---------------------------------------------------------------------------
// The driver — one body, two books (static polymorphism via template, no concepts)
// ---------------------------------------------------------------------------
template <class Book>
class NasdaqBookParser {
 public:
  explicit NasdaqBookParser(Book b) : book_(std::move(b)) {}

  bool add(const AddOrder& new_order);
  bool reduce(uint64_t ref, uint32_t quantity);
  bool remove(uint64_t ref);
  bool replace(const Replace& rep);

  Depth depth(size_t n) const;
  TopOfBook best() const { return book_.cached; }
  const Counters& counters() const { return book_.stats; }

 private:
  Book book_;
  void refresh();
  void erase_order(const Order& o);
  void onNoRef() { ++book_.stats.unknown_ref; }
  void onClamp() { ++book_.stats.clamped; }
  void onCross() { ++book_.stats.crossed; }
};
