// tests/test_book.cpp — book unit tests on REAL sequences from
// 07302019.NASDAQ_ITCH50.gz (values decoded from the raw file).
#include <gtest/gtest.h>

#include "book.h"
#include "itch.h"

// Build an AddOrder the way parse_A/F would.
static AddOrder mk(uint64_t ref, char side, uint32_t shares, uint64_t price_ticks) {
  AddOrder a{};
  a.ref = ref;
  a.side = side;
  a.shares = shares;
  a.price_ticks = price_ticks;
  return a;
}

// Real: A ref=539 side=S shares=11100 price=44100 ($4.41). Ask side.
TEST(Book, AddPutsOrderOnBook) {
  NasdaqBookParser b;
  EXPECT_TRUE(b.add(mk(539, 'S', 11100, 44100)));
  TopOfBook t = b.best();
  EXPECT_EQ(t.ask_px, 44100);
  EXPECT_EQ(t.ask_sz, 11100u);
  EXPECT_EQ(t.bid_px, 0);  // no bids yet
}

// Real: ref=539 gets E(200), E(300), then D.
TEST(Book, ReducePartialThenRemove) {
  NasdaqBookParser b;
  b.add(mk(539, 'S', 11100, 44100));

  EXPECT_TRUE(b.reduce(539, 200));  // E #1
  EXPECT_EQ(b.best().ask_sz, 10900u);

  EXPECT_TRUE(b.reduce(539, 300));  // E #2
  EXPECT_EQ(b.best().ask_sz, 10600u);

  EXPECT_TRUE(b.remove(539));  // D
  EXPECT_EQ(b.best().ask_px, 0);
  EXPECT_EQ(b.best().ask_sz, 0u);
}

// Real: A ref=467 side=B shares=200 price=488400 ($48.84), X(100), then D.
TEST(Book, CancelThenDelete) {
  NasdaqBookParser b;
  b.add(mk(467, 'B', 200, 488400));
  EXPECT_EQ(b.best().bid_px, 488400);
  EXPECT_EQ(b.best().bid_sz, 200u);

  EXPECT_TRUE(b.reduce(467, 100));  // X
  EXPECT_EQ(b.best().bid_sz, 100u);

  EXPECT_TRUE(b.remove(467));  // D
  EXPECT_EQ(b.best().bid_px, 0);
}

// Real U: old=951 (A: S,300,641500) -> new=8407 shares=300 price=641600.
TEST(Book, ReplaceKillsOldFilesNew) {
  NasdaqBookParser b;
  b.add(mk(951, 'S', 300, 641500));

  Replace r{};
  r.old_ref = 951;
  r.new_ref = 8407;
  r.shares = 300;
  r.price = 641600;

  EXPECT_TRUE(b.replace(r));
  TopOfBook t = b.best();
  EXPECT_EQ(t.ask_px, 641600);
  EXPECT_EQ(t.ask_sz, 300u);

  EXPECT_FALSE(b.remove(951));  // old ref no longer present
}

// Unknown refs are counted, not crashed on.
TEST(Book, UnknownRefCounted) {
  NasdaqBookParser b;
  EXPECT_FALSE(b.remove(999999));
  EXPECT_FALSE(b.reduce(999998, 10));
  EXPECT_EQ(b.counters().unknown_ref, 2u);
}

// Reducing more than resting clamps (never wraps uint32).
TEST(Book, OverReduceClamped) {
  NasdaqBookParser b;
  b.add(mk(1001, 'B', 100, 500000));
  EXPECT_TRUE(b.reduce(1001, 150));  // over-take
  EXPECT_EQ(b.counters().clamped, 1u);
  EXPECT_EQ(b.best().bid_px, 0);  // whole order removed
}

// A cross (bid >= ask) is counted, not halted.
TEST(Book, CrossedCounted) {
  NasdaqBookParser b;
  b.add(mk(1, 'B', 100, 500000));  // bid 500000
  b.add(mk(2, 'S', 100, 499000));  // ask 499000 -> crossed
  EXPECT_EQ(b.counters().crossed, 1u);
}

// depth() returns rows best-first on both sides.
TEST(Book, DepthBestFirst) {
  NasdaqBookParser b;
  b.add(mk(1, 'B', 100, 500000));
  b.add(mk(2, 'B', 50, 499000));
  b.add(mk(3, 'S', 70, 501000));
  b.add(mk(4, 'S', 30, 502000));

  Depth d = b.depth(2);
  ASSERT_EQ(d.bids.size(), 2u);
  EXPECT_EQ(d.bids[0].price, 500000);  // highest first
  EXPECT_EQ(d.bids[1].price, 499000);

  ASSERT_EQ(d.asks.size(), 2u);
  EXPECT_EQ(d.asks[0].price, 501000);  // lowest first
  EXPECT_EQ(d.asks[1].price, 502000);
}
