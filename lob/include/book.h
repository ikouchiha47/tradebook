#pragma once
#include "feed.h"
#include "itch.h"
#include "bitmap.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

// The Book turns your parsed structs into answers. Five jobs, nothing else:
// 1. add(AddOrder) — file order by ref, add qty to its price level. (A/F land here.)
// 2. reduce(ref, qty) — subtract from order + level, erase level at zero. (E/X land here; unknown ref → count, no-op.)
// 3. remove(ref) — full delete. (D lands here.)
// 4. replace(Replace) — atomic remove-old + add-new. (U lands here.)
// 5. best() / depth(n) — read-only: top bid/ask pair, top-n rows per side. Everything downstream (replay stats, sim fills, strategy) reads through these two.
// That plus the two maps is the component: events in via 1–4, state out via 5.
//

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

struct Depth{
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


class PriceLadder {
public:
    explicit PriceLadder() = default;
    explicit PriceLadder(int64_t base, int64_t inc, size_t slots);
    Level& at(int64_t px);
    Level* find(int64_t px);

    void add_order(int64_t px, uint32_t qty);
    void erase(int64_t px);
    int64_t highest() const;
    int64_t lowest() const;

private:
  int64_t base_, inc_;
  size_t slots_;

  std::vector<Level> hot_;
  std::map<int64_t, Level> cold_;

  Occupancy occ_;

  bool in_band(int64_t px) {
    return px >= base_ && index(px) < slots_;
  }
  size_t index(int64_t px) const {
    return (px - base_) / inc_;
  };
};


struct NasdqBook {
  std::unordered_map<uint64_t, Order> orders;  // ref → live order (side/price/qty for cancels)
  std::map<int64_t, Level> bids;               // buy-side rows, price → {total, count}
  std::map<int64_t, Level> asks;               // sell-side rows, same
  TopOfBook cached{};                            // best bid/ask, re-stamped per apply (best() is O(1))
  Counters stats{};                              // unknown_ref, clamped, crossed — the REQ-P1-040..042 armor

  NasdqBook() = default;
};

struct NasdqBookWithLadder {
  std::unordered_map<uint64_t, Order> orders;

  PriceLadder bids;
  PriceLadder asks;
  TopOfBook cached{};
  Counters stats{};

  NasdqBookWithLadder() = default;
};

class NasdaqBookParser {
  public:
    NasdaqBookParser();
    ~NasdaqBookParser();

    bool add(const AddOrder &new_order);
    bool reduce(uint64_t ref, uint32_t quantity);
    bool remove(uint64_t ref);
    bool replace(const Replace& rep);

    Depth depth(size_t n) const;
    TopOfBook best() const { return book_.cached; }
    const Counters& counters() const { return book_.stats; }

  private:
    NasdqBook book_;
    void refresh();
    void erase_order(const Order& o);

    void onNoRef() { book_.stats.unknown_ref++; }
    void onClamp() { book_.stats.clamped++; }
    void onCross() { book_.stats.crossed++; }

};

// There is a choice regarding how to maintain the
// highest bid and lowest ask
// 
// Two ways:
// - cursor
// - hierarchical bitmap (behaving like trie)
//
// the cursor maintains 2 flags, best_ask and best_bid
// at every B-id and S-ell
// rescan's on delete or exits
// need to walk the array to figure out the right place
//
// the bitmap 64bits, 2^6 ~ 6bits
