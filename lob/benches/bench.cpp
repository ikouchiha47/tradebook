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
NasdaqBookParser make_book(int levels) {
  NasdaqBookParser b;
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
  std::optional<NasdaqBookParser> book;
  AddOrder a{};
  a.side = 'B';
  a.shares = 100;
  a.price_ticks = 5000000;
  constexpr uint64_t kBatch = 4096;
  uint64_t ref = 0;
  for (auto _ : s) {
    if ((ref % kBatch) == 0) {
      s.PauseTiming();
      book.emplace();
      s.ResumeTiming();
    }
    a.ref = ++ref;
    benchmark::DoNotOptimize(book->add(a));
  }
}
BENCHMARK(BM_BookAdd);

void BM_BookReduce(benchmark::State& s) {
  NasdaqBookParser book;
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
  std::optional<NasdaqBookParser> book;
  AddOrder a{};
  a.side = 'B';
  a.shares = 100;
  a.price_ticks = 5000000;
  constexpr uint64_t kBatch = 4096;
  uint64_t base = 0;
  auto refill = [&] {
    book.emplace();
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
  NasdaqBookParser book;
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
  NasdaqBookParser book = make_book(100);
  for (auto _ : s) benchmark::DoNotOptimize(book.best());
}
BENCHMARK(BM_BookBest);

void BM_BookDepth5(benchmark::State& s) {
  NasdaqBookParser book = make_book(100);
  for (auto _ : s) benchmark::DoNotOptimize(book.depth(5));
}
BENCHMARK(BM_BookDepth5);

// ---- pipeline: parse one real A + file it (compute only, no gzip) ----

void BM_ParseThenApplyA(benchmark::State& s) {
  std::optional<NasdaqBookParser> book;
  std::vector<uint8_t> d = kA;
  AddOrder a;
  constexpr uint64_t kBatch = 4096;
  uint64_t ref = 0;
  for (auto _ : s) {
    if ((ref % kBatch) == 0) {
      s.PauseTiming();
      book.emplace();
      s.ResumeTiming();
    }
    d[19] ^= 1;
    parsers::nasdaq::parse_A(d, a);
    a.ref = ++ref;
    benchmark::DoNotOptimize(book->add(a));
  }
}
BENCHMARK(BM_ParseThenApplyA);

}  // namespace
