#pragma once

#include "feed.h"
#include <cstdint>
#include <span>
#include <string>
#include <sys/types.h>
#include <unordered_map>

struct Header { 
  char type;
  uint16_t locate, tracking;
  uint64_t ts48;
};

struct StockDir {
  uint16_t locate;
  std::string symbol;
};


struct AddOrder {
  uint64_t ref = 0;
  uint64_t price_ticks = 0;
  uint32_t mpid = 0;
  uint32_t shares = 0;
  uint16_t locate = 0;
  char side = 0;
};

struct Cancel {
  // handle X
  uint64_t ref = 0;
  uint32_t shares = 0;
};

struct Delete {
  // handles D
  uint64_t ref = 0;
};

struct Exec {
  // handles E
  uint64_t ref = 0;
  uint64_t match = 0;
  uint32_t shares = 0;
};

struct ExecPx {
  // handles C
  uint64_t ref = 0  ;
  uint32_t shares = 0;
  uint64_t match = 0;
  int64_t price = 0;
  char printable = 0;  // byte 31: 'Y' tape-published, 'N' hidden fill
};

struct Replace {
  // handles U
  uint64_t old_ref = 0, new_ref = 0;
  uint32_t shares = 0;
  int64_t price = 0;
};


namespace parsers::nasdaq {
  bool parse_R(std::span<const uint8_t> data, StockDir& dir);
  bool parse_A(std::span<const uint8_t> data, AddOrder& add);
  bool parse_F(std::span<const uint8_t> data, AddOrder& add);
  bool parse_X(std::span<const uint8_t> data, Cancel& cancel);
  bool parse_D(std::span<const uint8_t> data, Delete& del);
  bool parse_E(std::span<const uint8_t> data, Exec& exec);
  bool parse_C(std::span<const uint8_t> data, ExecPx& execp);
  bool parse_U(std::span<const uint8_t> data, Replace& r);
}

// class OrderParsing {
//   public:
//     explicit OrderParsing(const FeedReader* feed);
//     // ~OrderParsing();
//
//     OrderParsing(const OrderParsing&) = delete;
//     OrderParsing& operator=(const OrderParsing&) = delete;
//
//     void read();
//
//   private:
//     const FeedReader* feed_ = nullptr;
//     std::unordered_map<uint16_t, std::string> sym;
// };
