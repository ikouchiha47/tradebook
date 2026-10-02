// tests/test_parsers.cpp — parser unit tests on REAL file bytes.
// Every vector below is a verbatim payload dumped from 07302019.NASDAQ_ITCH50.gz
// (see the --hex runs). GTest. Built as lob_test via CMakeLists.
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "itch.h"

// Paste hex verbatim (even length); converts to bytes. Catches transcription
// slips via the size EXPECTs below, not by eyeballing pairs.
static std::vector<uint8_t> unhex(const char* h) {
  std::vector<uint8_t> out;
  for (; h[0] && h[1]; h += 2) {
    unsigned v = 0;
    std::sscanf(h, "%2x", &v);
    out.push_back((uint8_t)v);
  }
  return out;
}

// #1 S? no — dump #1: R, locate 1, symbol "A" (39B)
static const char* kR =
    "52000100000a392d5f038c41202020202020204e20000000644e415a20504e20314e000000"
    "004e";
// #237170: A, locate 494, ref 8617, B, 600 sh @1409900 ticks (36B)
static const char* kA =
    "4101ee00000d18c2ed9da800000000000021a9420000025841524758202020200015836c";
// real F: locate 8842, ref 6648, S, 100 sh @10020000, mpid LEHM (40B)
static const char* kF =
    "46228a00000d18c55ae5cf00000000000019f853000000645a565a5a542020200098e4a04c"
    "45484d";
// real X: locate 1087, ref 24949, 200 sh (23B)
static const char* kX = "58043f00000d18c6e7dad50000000000006175000000c8";
// real D: locate 8697, ref 432 (19B)
static const char* kD = "4421f900000d18c39030b400000000000001b0";
// real E: locate 2762, ref 854, 100 sh, match 17697 (31B)
static const char* kE =
    "450aca00020d18c3e203760000000000000356000000640000000000004521";
// real C: locate 5022, ref 5304083, 100 sh, match 37501, printable N,
// price 65900 @32..35 (36B). NOTE: printable lives at 31, price at 32..35.
static const char* kC =
    "43139e00011c2bb49c4da7000000000050ef1300000064000000000000927d4e0001016c";
// real U: locate 6612, old 951, new 8407, 300 sh, price 641600 (35B)
static const char* kU =
    "5519d400000d18c4f0020c00000000000003b700000000000020d70000012c0009ca40";

TEST(Parsers, RDecodesContactCard) {
  auto v = unhex(kR);
  ASSERT_EQ(v.size(), 39u);
  StockDir dir;
  std::span<const uint8_t> bytes(v.data(), v.size());
  EXPECT_TRUE(parsers::nasdaq::parse_R(bytes, dir));
  EXPECT_EQ(dir.locate, 1);
  EXPECT_EQ(dir.symbol, "A");
}

TEST(Parsers, ADecodesOrderBirth) {
  auto v = unhex(kA);
  ASSERT_EQ(v.size(), 36u);
  AddOrder add;
  std::span<const uint8_t> bytes(v.data(), v.size());
  EXPECT_TRUE(parsers::nasdaq::parse_A(bytes, add));
  EXPECT_EQ(add.locate, 494);
  EXPECT_EQ(add.ref, 8617u);
  EXPECT_EQ(add.side, 'B');
  EXPECT_EQ(add.shares, 600u);
  EXPECT_EQ(add.price_ticks, 1409900u);
}

TEST(Parsers, FDecodesAttributedBirth) {
  auto v = unhex(kF);
  ASSERT_EQ(v.size(), 40u);
  AddOrder add;
  std::span<const uint8_t> bytes(v.data(), v.size());
  EXPECT_TRUE(parsers::nasdaq::parse_F(bytes, add));
  EXPECT_EQ(add.locate, 8842);
  EXPECT_EQ(add.ref, 6648u);
  EXPECT_EQ(add.side, 'S');
  EXPECT_EQ(add.shares, 100u);
  EXPECT_EQ(add.price_ticks, 10020000u);
  EXPECT_EQ(add.mpid, 0x4C45484Du);  // "LEHM"
}

TEST(Parsers, XDecodesCancel) {
  auto v = unhex(kX);
  ASSERT_EQ(v.size(), 23u);
  Cancel c;
  std::span<const uint8_t> bytes(v.data(), v.size());
  EXPECT_TRUE(parsers::nasdaq::parse_X(bytes, c));
  EXPECT_EQ(c.ref, 24949u);
  EXPECT_EQ(c.shares, 200u);
}

TEST(Parsers, DDecodesDelete) {
  auto v = unhex(kD);
  ASSERT_EQ(v.size(), 19u);
  Delete d;
  std::span<const uint8_t> bytes(v.data(), v.size());
  EXPECT_TRUE(parsers::nasdaq::parse_D(bytes, d));
  EXPECT_EQ(d.ref, 432u);
}

TEST(Parsers, EDecodesExecution) {
  auto v = unhex(kE);
  ASSERT_EQ(v.size(), 31u);
  Exec e;
  std::span<const uint8_t> bytes(v.data(), v.size());
  EXPECT_TRUE(parsers::nasdaq::parse_E(bytes, e));
  EXPECT_EQ(e.ref, 854u);
  EXPECT_EQ(e.shares, 100u);
  EXPECT_EQ(e.match, 17697u);
}

TEST(Parsers, CDecodesPricedExecution) {
  auto v = unhex(kC);
  ASSERT_EQ(v.size(), 36u);
  ExecPx c;
  std::span<const uint8_t> bytes(v.data(), v.size());
  EXPECT_TRUE(parsers::nasdaq::parse_C(bytes, c));
  EXPECT_EQ(c.ref, 5304083u);
  EXPECT_EQ(c.shares, 100u);
  EXPECT_EQ(c.match, 37501u);
  EXPECT_EQ(c.printable, 'N');
  EXPECT_EQ(c.price, 65900);
}

TEST(Parsers, UDecodesReplace) {
  auto v = unhex(kU);
  ASSERT_EQ(v.size(), 35u);
  Replace r;
  std::span<const uint8_t> bytes(v.data(), v.size());
  EXPECT_TRUE(parsers::nasdaq::parse_U(bytes, r));
  EXPECT_EQ(r.old_ref, 951u);
  EXPECT_EQ(r.new_ref, 8407u);
  EXPECT_EQ(r.shares, 300u);
  EXPECT_EQ(r.price, 641600);
}

TEST(Parsers, RejectsWrongSize) {
  auto v = unhex(kR);
  std::span<const uint8_t> short_bytes(v.data(), 10);
  StockDir dir;
  EXPECT_FALSE(parsers::nasdaq::parse_R(short_bytes, dir));
  std::span<const uint8_t> full(v.data(), v.size());
  AddOrder add;
  EXPECT_FALSE(parsers::nasdaq::parse_A(full, add));  // 39B R payload is not an A
}
