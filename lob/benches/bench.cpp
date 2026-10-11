// benches/bench.cpp — micro-benchmarks on REAL payload bytes from
// 07302019.NASDAQ_ITCH50.gz.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <vector>

#include "book.h"
#include "itch.h"

namespace {

std::vector<uint8_t> unhex(const char* h) {
  std::vector<uint8_t> out;
  for (; h[0] && h[1]; h += 2) {
    unsigned v = 0;
    std::sscanf(h, "%2x", &v);
    out.push_back(static_cast<uint8_t>(v));
  }
  return out;
}

// Real payloads (see tests / transcript).
const std::vector<uint8_t> kR = unhex(
    "52000100000a392d5f038c41202020202020204e20000000644e415a20504e20314e000000004e");
const std::vector<uint8_t> kA = unhex(
    "4101ee00000d18c2ed9da800000000000021a9420000025841524758202020200015836c");
const std::vector<uint8_t> kF = unhex(
    "46228a00000d18c55ae5cf00000000000019f853000000645a565a5a542020200098e4a04c45484d");
const std::vector<uint8_t> kE = unhex(
    "450aca00020d18c3e203760000000000000356000000640000000000004521");
const std::vector<uint8_t> kC = unhex(
    "43139e00011c2bb49c4da7000000000050ef1300000064000000000000927d4e0001016c");
const std::vector<uint8_t> kX = unhex("58043f00000d18c6e7dad50000000000006175000000c8");
const std::vector<uint8_t> kD = unhex("4421f900000d18c39030b400000000000001b0");
const std::vector<uint8_t> kU = unhex(
    "5519d400000d18c4f0020c00000000000003b700000000000020d70000012c0009ca40");

// Book pre-filled with `levels` bid rows and `levels` ask rows.
NasdaqBookParser<NasdqBook> make_book(int levels) {
  NasdaqBookParser<NasdqBook> b(NasdqBook{});
  AddOrder a{};
  a.side = 'B';
  a.shares = 100;
  for (int i = 0; i < levels; ++i) {
    a.ref = 1000 + i;
    a.price_ticks = 5000000 - i * 100;
    b.add(a);
  }
  a.side = 'S';
  for (int i = 0; i < levels; ++i) {
    a.ref = 2000 + i;
    a.price_ticks = 5000100 + i * 100;
    b.add(a);
  }
  return b;
}

// ---- parsers (input byte varied so the decode can't be hoisted) ----

void BM_ParseR(benchmark::State& s) {
  std::vector<uint8_t> d = kR;
  StockDir out;
  for (auto _ : s) {
    d[19] ^= 1;
    parsers::nasdaq::parse_R(d, out);
    benchmark::DoNotOptimize(out);
  }
}
BENCHMARK(BM_ParseR);

void BM_ParseA(benchmark::State& s) {
  std::vector<uint8_t> d = kA;
  AddOrder out;
  for (auto _ : s) {
    d[19] ^= 1;
    parsers::nasdaq::parse_A(d, out);
    benchmark::DoNotOptimize(out);
  }
}
BENCHMARK(BM_ParseA);

void BM_ParseF(benchmark::State& s) {
  std::vector<uint8_t> d = kF;
  AddOrder out;
  for (auto _ : s) {
    d[19] ^= 1;
    parsers::nasdaq::parse_F(d, out);
    benchmark::DoNotOptimize(out);
  }
}
BENCHMARK(BM_ParseF);

void BM_ParseE(benchmark::State& s) {
  std::vector<uint8_t> d = kE;
  Exec out;
  for (auto _ : s) {
    d[19] ^= 1;
    parsers::nasdaq::parse_E(d, out);
    benchmark::DoNotOptimize(out);
  }
}
BENCHMARK(BM_ParseE);

void BM_ParseC(benchmark::State& s) {
  std::vector<uint8_t> d = kC;
  ExecPx out;
  for (auto _ : s) {
    d[19] ^= 1;
    parsers::nasdaq::parse_C(d, out);
    benchmark::DoNotOptimize(out);
  }
}
BENCHMARK(BM_ParseC);

void BM_ParseX(benchmark::State& s) {
  std::vector<uint8_t> d = kX;
  Cancel out;
  for (auto _ : s) {
    d[19] ^= 1;
    parsers::nasdaq::parse_X(d, out);
    benchmark::DoNotOptimize(out);
  }
}
BENCHMARK(BM_ParseX);

void BM_ParseD(benchmark::State& s) {
  std::vector<uint8_t> d = kD;
  Delete out;
  for (auto _ : s) {
    d[11] ^= 1;
    parsers::nasdaq::parse_D(d, out);
    benchmark::DoNotOptimize(out);
  }
}
BENCHMARK(BM_ParseD);

void BM_ParseU(benchmark::State& s) {
  std::vector<uint8_t> d = kU;
  Replace out;
  for (auto _ : s) {
    d[19] ^= 1;
    parsers::nasdaq::parse_U(d, out);
    benchmark::DoNotOptimize(out);
  }
}
BENCHMARK(BM_ParseU);

// ---- order book ops (state reset inside PauseTiming, so it isn't measured) ----

void BM_BookAdd(benchmark::State& s) {
  std::optional<NasdaqBookParser<NasdqBook>> book;
  AddOrder a{};
  a.side = 'B';
  a.shares = 100;
  a.price_ticks = 5000000;
  constexpr uint64_t kBatch = 4096;
  uint64_t ref = 0;
  for (auto _ : s) {
    if ((ref % kBatch) == 0) {
      s.PauseTiming();
      book.emplace(NasdqBook{});
      s.ResumeTiming();
    }
    a.ref = ++ref;
    benchmark::DoNotOptimize(book->add(a));
  }
}
BENCHMARK(BM_BookAdd);

void BM_BookReduce(benchmark::State& s) {
  NasdaqBookParser<NasdqBook> book(NasdqBook{});
  AddOrder a{};
  a.ref = 1;
  a.side = 'B';
  a.shares = 0xFFFFFFFFu;  // huge, never exhausts during a short run
  a.price_ticks = 5000000;
  book.add(a);
  for (auto _ : s) benchmark::DoNotOptimize(book.reduce(1, 1));
}
BENCHMARK(BM_BookReduce);

void BM_BookRemove(benchmark::State& s) {
  std::optional<NasdaqBookParser<NasdqBook>> book;
  AddOrder a{};
  a.side = 'B';
  a.shares = 100;
  a.price_ticks = 5000000;
  constexpr uint64_t kBatch = 4096;
  uint64_t base = 0;
  auto refill = [&] {
    book.emplace(NasdqBook{});
    for (uint64_t i = 0; i < kBatch; ++i) {
      a.ref = base + i;
      book->add(a);
    }
  };
  refill();
  uint64_t k = 0;
  for (auto _ : s) {
    if (k == kBatch) {
      s.PauseTiming();
      base += kBatch;
      refill();
      s.ResumeTiming();
      k = 0;
    }
    benchmark::DoNotOptimize(book->remove(base + k));
    ++k;
  }
}
BENCHMARK(BM_BookRemove);

void BM_BookReplace(benchmark::State& s) {
  NasdaqBookParser<NasdqBook> book(NasdqBook{});
  AddOrder a{};
  a.ref = 1;
  a.side = 'B';
  a.shares = 100;
  a.price_ticks = 5000000;
  book.add(a);
  Replace r{};
  r.shares = 100;
  r.price = 5000000;
  uint64_t cur = 1;
  for (auto _ : s) {
    r.old_ref = cur;
    r.new_ref = cur + 1;
    benchmark::DoNotOptimize(book.replace(r));
    ++cur;
  }
}
BENCHMARK(BM_BookReplace);

void BM_BookBest(benchmark::State& s) {
  auto book = make_book(100);
  for (auto _ : s) benchmark::DoNotOptimize(book.best());
}
BENCHMARK(BM_BookBest);

void BM_BookDepth5(benchmark::State& s) {
  auto book = make_book(100);
  for (auto _ : s) benchmark::DoNotOptimize(book.depth(5));
}
BENCHMARK(BM_BookDepth5);

// ---- pipeline: parse one real A + file it (compute only, no gzip) ----

void BM_ParseThenApplyA(benchmark::State& s) {
  std::optional<NasdaqBookParser<NasdqBook>> book;
  std::vector<uint8_t> d = kA;
  AddOrder a;
  constexpr uint64_t kBatch = 4096;
  uint64_t ref = 0;
  for (auto _ : s) {
    if ((ref % kBatch) == 0) {
      s.PauseTiming();
      book.emplace(NasdqBook{});
      s.ResumeTiming();
    }
    d[19] ^= 1;
    parsers::nasdaq::parse_A(d, a);
    a.ref = ++ref;
    benchmark::DoNotOptimize(book->add(a));
  }
}
BENCHMARK(BM_ParseThenApplyA);

// ===========================================================================
// Book B — tick ladder (same workloads, ladder levels). Band covers the
// bench price range: bids 5,000,000-..4,990,100, asks ..5,010,000, penny step.
// ===========================================================================
constexpr int64_t kLadderBase = 4990000;
constexpr int64_t kLadderInc = 100;
constexpr size_t kLadderSlots = 201;

NasdaqBookParser<NasdqBookWithLadder> make_book_ladder(int levels) {
  NasdaqBookParser<NasdqBookWithLadder> b(
      NasdqBookWithLadder(kLadderBase, kLadderInc, kLadderSlots));
  AddOrder a{};
  a.side = 'B';
  a.shares = 100;
  for (int i = 0; i < levels; ++i) {
    a.ref = 1000 + i;
    a.price_ticks = 5000000 - i * 100;
    b.add(a);
  }
  a.side = 'S';
  for (int i = 0; i < levels; ++i) {
    a.ref = 2000 + i;
    a.price_ticks = 5000100 + i * 100;
    b.add(a);
  }
  return b;
}

void BM_BookAddLadder(benchmark::State& s) {
  std::optional<NasdaqBookParser<NasdqBookWithLadder>> book;
  AddOrder a{};
  a.side = 'B';
  a.shares = 100;
  a.price_ticks = 5000000;
  constexpr uint64_t kBatch = 4096;
  uint64_t ref = 0;
  for (auto _ : s) {
    if ((ref % kBatch) == 0) {
      s.PauseTiming();
      book.emplace(NasdqBookWithLadder(kLadderBase, kLadderInc, kLadderSlots));
      s.ResumeTiming();
    }
    a.ref = ++ref;
    benchmark::DoNotOptimize(book->add(a));
  }
}
BENCHMARK(BM_BookAddLadder);

void BM_BookReduceLadder(benchmark::State& s) {
  NasdaqBookParser<NasdqBookWithLadder> book(
      NasdqBookWithLadder(kLadderBase, kLadderInc, kLadderSlots));
  AddOrder a{};
  a.ref = 1;
  a.side = 'B';
  a.shares = 0xFFFFFFFFu;
  a.price_ticks = 5000000;
  book.add(a);
  for (auto _ : s) benchmark::DoNotOptimize(book.reduce(1, 1));
}
BENCHMARK(BM_BookReduceLadder);

void BM_BookRemoveLadder(benchmark::State& s) {
  std::optional<NasdaqBookParser<NasdqBookWithLadder>> book;
  AddOrder a{};
  a.side = 'B';
  a.shares = 100;
  a.price_ticks = 5000000;
  constexpr uint64_t kBatch = 4096;
  uint64_t base = 0;
  auto refill = [&] {
    book.emplace(NasdqBookWithLadder(kLadderBase, kLadderInc, kLadderSlots));
    for (uint64_t i = 0; i < kBatch; ++i) {
      a.ref = base + i;
      book->add(a);
    }
  };
  refill();
  uint64_t k = 0;
  for (auto _ : s) {
    if (k == kBatch) {
      s.PauseTiming();
      base += kBatch;
      refill();
      s.ResumeTiming();
      k = 0;
    }
    benchmark::DoNotOptimize(book->remove(base + k));
    ++k;
  }
}
BENCHMARK(BM_BookRemoveLadder);

void BM_BookReplaceLadder(benchmark::State& s) {
  NasdaqBookParser<NasdqBookWithLadder> book(
      NasdqBookWithLadder(kLadderBase, kLadderInc, kLadderSlots));
  AddOrder a{};
  a.ref = 1;
  a.side = 'B';
  a.shares = 100;
  a.price_ticks = 5000000;
  book.add(a);
  Replace r{};
  r.shares = 100;
  r.price = 5000000;
  uint64_t cur = 1;
  for (auto _ : s) {
    r.old_ref = cur;
    r.new_ref = cur + 1;
    benchmark::DoNotOptimize(book.replace(r));
    ++cur;
  }
}
BENCHMARK(BM_BookReplaceLadder);

void BM_BookBestLadder(benchmark::State& s) {
  auto book = make_book_ladder(100);
  for (auto _ : s) benchmark::DoNotOptimize(book.best());
}
BENCHMARK(BM_BookBestLadder);

void BM_BookDepth5Ladder(benchmark::State& s) {
  auto book = make_book_ladder(100);
  for (auto _ : s) benchmark::DoNotOptimize(book.depth(5));
}
BENCHMARK(BM_BookDepth5Ladder);

// ===========================================================================
// Parameterized sweep — map vs ladder over book size (N) and sparsity (gap).
//   arg0 = levels per side, arg1 = tick gap between levels
//   (gap 100 = dense/consecutive; 1000/5000 = sparse)
// ===========================================================================
NasdaqBookParser<NasdqBook> make_map_sweep(int n, int64_t step) {
  NasdaqBookParser<NasdqBook> b(NasdqBook{});
  AddOrder a{};
  a.shares = 100;
  a.side = 'B';
  for (int i = 0; i < n; ++i) {
    a.ref = 100000 + i;
    a.price_ticks = static_cast<uint64_t>(5000000 - static_cast<int64_t>(i) * step);
    b.add(a);
  }
  a.side = 'S';
  for (int i = 0; i < n; ++i) {
    a.ref = 200000 + i;
    a.price_ticks = static_cast<uint64_t>(5000100 + static_cast<int64_t>(i) * step);
    b.add(a);
  }
  return b;
}

NasdaqBookParser<NasdqBookWithLadder> make_ladder_sweep(int n, int64_t step) {
  const int64_t lo = 5000000 - static_cast<int64_t>(n + 1) * step;
  const int64_t hi = 5000100 + static_cast<int64_t>(n + 1) * step;
  const size_t slots = static_cast<size_t>((hi - lo) / 100) + 1;
  NasdaqBookParser<NasdqBookWithLadder> b(NasdqBookWithLadder(lo, 100, slots));
  AddOrder a{};
  a.shares = 100;
  a.side = 'B';
  for (int i = 0; i < n; ++i) {
    a.ref = 100000 + i;
    a.price_ticks = static_cast<uint64_t>(5000000 - static_cast<int64_t>(i) * step);
    b.add(a);
  }
  a.side = 'S';
  for (int i = 0; i < n; ++i) {
    a.ref = 200000 + i;
    a.price_ticks = static_cast<uint64_t>(5000100 + static_cast<int64_t>(i) * step);
    b.add(a);
  }
  return b;
}

// new ask price at the top: on-grid, no collision, no crossing
uint64_t sweep_new_ask(int n, int64_t step) {
  return static_cast<uint64_t>(5000100 + static_cast<int64_t>(n) * step + 100);
}

void BM_SweepAddRemoveMap(benchmark::State& s) {
  const int n = static_cast<int>(s.range(0));
  const int64_t step = s.range(1);
  auto book = make_map_sweep(n, step);
  AddOrder a{};
  a.side = 'S';
  a.shares = 100;
  a.price_ticks = sweep_new_ask(n, step);
  uint64_t ref = 1;
  for (auto _ : s) {
    a.ref = ref++;
    benchmark::DoNotOptimize(book.add(a));
    benchmark::DoNotOptimize(book.remove(a.ref));
  }
}
BENCHMARK(BM_SweepAddRemoveMap)->ArgsProduct({{100, 1000, 10000}, {100, 1000, 5000}});

void BM_SweepAddRemoveLadder(benchmark::State& s) {
  const int n = static_cast<int>(s.range(0));
  const int64_t step = s.range(1);
  auto book = make_ladder_sweep(n, step);
  AddOrder a{};
  a.side = 'S';
  a.shares = 100;
  a.price_ticks = sweep_new_ask(n, step);
  uint64_t ref = 1;
  for (auto _ : s) {
    a.ref = ref++;
    benchmark::DoNotOptimize(book.add(a));
    benchmark::DoNotOptimize(book.remove(a.ref));
  }
}
BENCHMARK(BM_SweepAddRemoveLadder)->ArgsProduct({{100, 1000, 10000}, {100, 1000, 5000}});

void BM_SweepDepthMap(benchmark::State& s) {
  auto book = make_map_sweep(static_cast<int>(s.range(0)), s.range(1));
  for (auto _ : s) benchmark::DoNotOptimize(book.depth(5));
}
BENCHMARK(BM_SweepDepthMap)->ArgsProduct({{100, 1000, 10000}, {100, 1000, 5000}});

void BM_SweepDepthLadder(benchmark::State& s) {
  auto book = make_ladder_sweep(static_cast<int>(s.range(0)), s.range(1));
  for (auto _ : s) benchmark::DoNotOptimize(book.depth(5));
}
BENCHMARK(BM_SweepDepthLadder)->ArgsProduct({{100, 1000, 10000}, {100, 1000, 5000}});

// ===========================================================================
// Dense SCALE sweep — push the working set past L1 (64 KB) and L2 (M4 ~16 MB)
// so the tree's lines start missing. gap = 100 (consecutive slots).
//   map  :   N nodes  x ~48 B   (+ 2 allocators)
//   ladder:  ~2N slots x 8 B     (+ bitmap)
// ===========================================================================
void BM_ScaleDepthMap(benchmark::State& s) {
  auto book = make_map_sweep(static_cast<int>(s.range(0)), 100);
  for (auto _ : s) benchmark::DoNotOptimize(book.depth(5));
}
BENCHMARK(BM_ScaleDepthMap)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);

void BM_ScaleDepthLadder(benchmark::State& s) {
  auto book = make_ladder_sweep(static_cast<int>(s.range(0)), 100);
  for (auto _ : s) benchmark::DoNotOptimize(book.depth(5));
}
BENCHMARK(BM_ScaleDepthLadder)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);

void BM_ScaleAddRemoveMap(benchmark::State& s) {
  const int n = static_cast<int>(s.range(0));
  auto book = make_map_sweep(n, 100);
  AddOrder a{};
  a.side = 'S';
  a.shares = 100;
  a.price_ticks = sweep_new_ask(n, 100);
  uint64_t ref = 1;
  for (auto _ : s) {
    a.ref = ref++;
    benchmark::DoNotOptimize(book.add(a));
    benchmark::DoNotOptimize(book.remove(a.ref));
  }
}
BENCHMARK(BM_ScaleAddRemoveMap)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);

void BM_ScaleAddRemoveLadder(benchmark::State& s) {
  const int n = static_cast<int>(s.range(0));
  auto book = make_ladder_sweep(n, 100);
  AddOrder a{};
  a.side = 'S';
  a.shares = 100;
  a.price_ticks = sweep_new_ask(n, 100);
  uint64_t ref = 1;
  for (auto _ : s) {
    a.ref = ref++;
    benchmark::DoNotOptimize(book.add(a));
    benchmark::DoNotOptimize(book.remove(a.ref));
  }
}
BENCHMARK(BM_ScaleAddRemoveLadder)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);

}  // namespace
