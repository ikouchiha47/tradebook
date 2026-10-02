#pragma once
#include "feed.h"
#include "itch.h"
#include <cstdint>
#include <map>
#include <unordered_map>

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

struct TopOfBook {
  int64_t bid_px = 0, ask_px = 0;
  uint32_t bid_sz = 0, ask_sz = 0;
};

struct Counters {
  uint64_t uknown_ref = 0;
  uint64_t clamped = 0;
  uint64_t crossed = 0;
};

struct Book {
  std::unordered_map<uint64_t, Order> orders;  // ref → live order (side/price/qty for cancels)
  std::map<int64_t, Level> bids;               // buy-side rows, price → {total, count}
  std::map<int64_t, Level> asks;               // sell-side rows, same
  TopOfBook cached;                            // best bid/ask, re-stamped per apply (best() is O(1))
  Counters stats;                              // unknown_ref, clamped, crossed — the REQ-P1-040..042 armor
};

class NasdaqBookParser {
  public:
    NasdaqBookParser();
    ~NasdaqBookParser();

    bool add(AddOrder &new_order);
    bool reduce(uint64_t ref, uint32_t quantity);
    bool remove(uint64_t ref);
    bool replace(Replace& rep);
    bool best();

  private:
    Book book_;
};
