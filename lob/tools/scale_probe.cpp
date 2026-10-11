// tools/scale_probe.cpp — map vs ladder, dense scale, for profiling.
// Self-contained: no zlib, no Google Benchmark, no ITCH data file.
//
// Build (plain):   g++ -O3 -std=c++20 -Iinclude tools/scale_probe.cpp src/book.cpp src/bitmap.cpp -o scale_probe
// Build (alloc):   add -DPROBE_COUNT_ALLOCS  → prints allocs/op + bytes/op
// Run:             ./scale_probe <map|ladder> <levels> <reps>
//                  reps=0 → build only (subtract from a reps run to isolate the loop)
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef PROBE_COUNT_ALLOCS
#include <atomic>
#include <new>
static std::atomic<uint64_t> g_nalloc{0}, g_nbytes{0};
static void* raw_alloc(size_t n) {
  g_nalloc.fetch_add(1, std::memory_order_relaxed);
  g_nbytes.fetch_add(n, std::memory_order_relaxed);
  void* p = std::malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  return p;
}
void* operator new(std::size_t n) { return raw_alloc(n); }
void* operator new[](std::size_t n) { return raw_alloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

#include "book.h"

using Clock = std::chrono::steady_clock;
static volatile uint64_t g_sink = 0;

static NasdaqBookParser<NasdqBook> make_map(int n) {
  NasdaqBookParser<NasdqBook> b(NasdqBook{});
  AddOrder a{};
  a.shares = 100;
  a.side = 'B';
  for (int i = 0; i < n; ++i) { a.ref = 100000 + i; a.price_ticks = 5000000 - (int64_t)i * 100; b.add(a); }
  a.side = 'S';
  for (int i = 0; i < n; ++i) { a.ref = 200000 + i; a.price_ticks = 5000100 + (int64_t)i * 100; b.add(a); }
  return b;
}

static NasdaqBookParser<NasdqBookWithLadder> make_ladder(int n) {
  const int64_t lo = 5000000 - (int64_t)(n + 1) * 100;
  const int64_t hi = 5000100 + (int64_t)(n + 1) * 100 + 200;
  const size_t slots = (size_t)((hi - lo) / 100) + 1;
  NasdaqBookParser<NasdqBookWithLadder> b(NasdqBookWithLadder(lo, 100, slots));
  AddOrder a{};
  a.shares = 100;
  a.side = 'B';
  for (int i = 0; i < n; ++i) { a.ref = 100000 + i; a.price_ticks = 5000000 - (int64_t)i * 100; b.add(a); }
  a.side = 'S';
  for (int i = 0; i < n; ++i) { a.ref = 200000 + i; a.price_ticks = 5000100 + (int64_t)i * 100; b.add(a); }
  return b;
}

template <class Book>
static void run(const char* name, int n, uint64_t reps, Book book) {
  AddOrder a{};
  a.side = 'S';
  a.shares = 100;
  a.price_ticks = (uint64_t)(5000100 + (int64_t)(n + 1) * 100 + 100);  // above the top ask

#ifdef PROBE_COUNT_ALLOCS
  const uint64_t a0 = g_nalloc.load(), b0 = g_nbytes.load();
#endif
  auto t0 = Clock::now();
  for (uint64_t i = 0; i < reps; ++i) {
    a.ref = 7;
    g_sink += book.add(a) ? 1u : 0u;
    g_sink += book.remove(7) ? 1u : 0u;
  }
  const double ar = reps ? std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / (double)reps : 0.0;
#ifdef PROBE_COUNT_ALLOCS
  const uint64_t da = g_nalloc.load() - a0, db = g_nbytes.load() - b0;
#endif

  auto t1 = Clock::now();
  for (uint64_t i = 0; i < reps; ++i) {
    Depth d = book.depth(5);
    g_sink += d.bids.size() + d.asks.size();
  }
  const double dp = reps ? std::chrono::duration<double, std::nano>(Clock::now() - t1).count() / (double)reps : 0.0;
#ifdef PROBE_COUNT_ALLOCS
  const uint64_t dda = g_nalloc.load() - a0, ddb = g_nbytes.load() - b0;
#endif

  std::printf("%-6s N=%-8d reps=%-9llu addremove=%7.1f ns  depth5=%7.1f ns\n",
              name, n, (unsigned long long)reps, ar, dp);
#ifdef PROBE_COUNT_ALLOCS
  if (reps) {
    std::printf("        addremove: allocs/op=%.3f  bytes/op=%.1f   |   depth5: allocs/op=%.3f (%.1f B)\n",
                (double)da / (double)reps, (double)db / (double)reps,
                (double)(dda - da) / (double)reps, (double)(ddb - db) / (double)reps);
  }
#endif
}

int main(int argc, char** argv) {
  const std::string book = argc > 1 ? argv[1] : "map";
  const int n = argc > 2 ? std::atoi(argv[2]) : 10000;
  const uint64_t reps = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 1000000;

  if (book == "ladder") run("ladder", n, reps, make_ladder(n));
  else                  run("map", n, reps, make_map(n));

  std::printf("sink=%llu\n", (unsigned long long)g_sink);
  return 0;
}
