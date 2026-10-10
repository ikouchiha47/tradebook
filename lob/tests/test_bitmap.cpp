// tests/test_bitmap.cpp — occupancy bitmap unit tests (set/clear, highest/lowest,
// prev/next_occupied). Pure, no book.
#include <gtest/gtest.h>

#include "bitmap.h"

TEST(Bitmap, HighestLowest) {
  Occupancy occ(256);
  occ.set(3);
  occ.set(5);
  occ.set(40);
  occ.set(100);
  occ.set(200);
  EXPECT_EQ(occ.highest(), 200);
  EXPECT_EQ(occ.lowest(), 3);
}

TEST(Bitmap, PrevNextOccupied) {
  Occupancy occ(256);
  for (int s : {3, 5, 40, 100, 200}) occ.set(static_cast<size_t>(s));

  EXPECT_EQ(occ.prev_occupied(100), 40);
  EXPECT_EQ(occ.prev_occupied(40), 5);
  EXPECT_EQ(occ.prev_occupied(5), 3);
  EXPECT_EQ(occ.prev_occupied(3), -1);   // nothing below the lowest
  EXPECT_EQ(occ.prev_occupied(201), 200);
  EXPECT_EQ(occ.prev_occupied(0), -1);

  EXPECT_EQ(occ.next_occupied(100), 200);
  EXPECT_EQ(occ.next_occupied(40), 100);
  EXPECT_EQ(occ.next_occupied(5), 40);
  EXPECT_EQ(occ.next_occupied(200), -1);  // nothing above the highest
  EXPECT_EQ(occ.next_occupied(-1), 3);    // "find the lowest"
}

TEST(Bitmap, ClearUpdates) {
  Occupancy occ(256);
  occ.set(10);
  occ.set(20);
  occ.clear(20);
  EXPECT_EQ(occ.highest(), 10);
  EXPECT_EQ(occ.next_occupied(10), -1);
  occ.clear(10);
  EXPECT_EQ(occ.highest(), -1);
  EXPECT_EQ(occ.lowest(), -1);
}
