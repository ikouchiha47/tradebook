// tests/test_alloc.cpp — proves the read/reduce path does not allocate.
// The global operator new/delete overrides MUST live in exactly ONE translation
// unit, linked into lob_test only (never into the library or lob-replay).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include <gtest/gtest.h>

#include "book.h"

static std::atomic<uint64_t> g_allocs{0};

void* operator new(std::size_t n) {
  g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
  g_allocs.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
AddOrder mk(uint64_t ref, char side, uint32_t shares, int64_t px) {
  AddOrder a{};
  a.ref = ref;
  a.side = side;
  a.shares = shares;
  a.price_ticks = static_cast<uint64_t>(px);
  return a;
}
}  // namespace

// reduce() and best() on existing state must not allocate.
TEST(Alloc, ReduceAndBestAreAllocFree) {
  NasdaqBookParser<NasdqBook> book(NasdqBook{});
  constexpr uint64_t kN = 1000;

  // Warm-up: build orders across 50 levels. This DOES allocate (before measuring).
  for (uint64_t i = 0; i < kN; ++i) {
    book.add(mk(i + 1, 'B', 100, 5000000 - static_cast<int64_t>(i % 50) * 100));
  }

  const uint64_t before = g_allocs.load(std::memory_order_relaxed);

  volatile int64_t sink = 0;  // keeps the loop from being optimized away
  uint64_t applied = 0;
  for (uint64_t i = 0; i < kN; ++i) {
    sink = book.best().bid_px;                    // read: no alloc
    applied += book.reduce(i + 1, 10) ? 1u : 0u;  // partial reduce: no alloc
  }

  const uint64_t after = g_allocs.load(std::memory_order_relaxed);

  EXPECT_EQ(after - before, 0u);  // zero allocations on the read/reduce path
  EXPECT_EQ(applied, kN);
  EXPECT_GT(sink, 0);
}

// Documented caveat: adding a NEW price level allocates a map node.
TEST(Alloc, AddNewLevelAllocates) {
  NasdaqBookParser<NasdqBook> book(NasdqBook{});
  book.add(mk(1, 'B', 100, 5000000));  // first level allocates

  const uint64_t before = g_allocs.load(std::memory_order_relaxed);
  book.add(mk(2, 'B', 100, 4999900));  // new ref + new level
  const uint64_t after = g_allocs.load(std::memory_order_relaxed);

  EXPECT_GT(after - before, 0u);  // node-based maps allocate on insert
}
