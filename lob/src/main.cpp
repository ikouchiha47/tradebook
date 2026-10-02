// lob-replay: replay ITCH 5.0 through parsers + order book for ONE symbol.
// Book has no locate dimension, so we filter to a single symbol (auto-picked
// from the first A/F unless --locate is given).
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <unordered_map>

#include "book.h"
#include "feed.h"
#include "itch.h"

namespace {
constexpr uint64_t kNoLocate = UINT64_MAX;
inline std::span<const uint8_t> body(const NasdaqRawMsg& m) {
  return std::span<const uint8_t>(m.payload.data(), m.len);
}
}  // namespace

int main(int argc, char** argv) {
  std::string path = "../07302019.NASDAQ_ITCH50.gz";
  uint64_t limit = 0;            // 0 = whole file
  uint64_t stats_every = 0;      // 0 = off
  uint64_t target = kNoLocate;   // single-symbol filter

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--file" && i + 1 < argc) path = argv[++i];
    else if (a == "--limit" && i + 1 < argc) limit = std::stoull(argv[++i]);
    else if (a == "--locate" && i + 1 < argc) target = std::stoull(argv[++i]);
    else if (a == "--stats-every" && i + 1 < argc) stats_every = std::stoull(argv[++i]);
  }

  FeedReader feed(path);
  if (!feed.open_ok()) {
    std::fprintf(stderr, "FAIL: cannot open %s\n", path.c_str());
    return 1;
  }

  NasdaqRawMsg m;
  NasdaqBookParser book;
  std::unordered_map<uint16_t, std::string> sym;  // locate -> symbol (from R)

  uint64_t n = 0;        // messages read
  uint64_t applied = 0;  // messages that touched the book

  while (feed.next(m)) {
    if (limit && ++n > limit) break;

    switch (m.type) {
      case 'R': {  // reference: record symbol for all locates
        StockDir d;
        if (parsers::nasdaq::parse_R(body(m), d)) sym[d.locate] = d.symbol;
        break;
      }
      case 'A':
      case 'F': {
        AddOrder a;
        const bool ok = (m.type == 'A') ? parsers::nasdaq::parse_A(body(m), a)
                                        : parsers::nasdaq::parse_F(body(m), a);
        if (!ok) break;
        if (target == kNoLocate) target = a.locate;  // auto-pick first traded symbol
        if (a.locate == target) { book.add(a); ++applied; }
        break;
      }
      case 'E': {
        Exec e;
        if (parsers::nasdaq::parse_E(body(m), e) && m.locate == target) {
          book.reduce(e.ref, e.shares); ++applied;
        }
        break;
      }
      case 'C': {
        ExecPx c;
        if (parsers::nasdaq::parse_C(body(m), c) && m.locate == target) {
          book.reduce(c.ref, c.shares); ++applied;
        }
        break;
      }
      case 'X': {
        Cancel x;
        if (parsers::nasdaq::parse_X(body(m), x) && m.locate == target) {
          book.reduce(x.ref, x.shares); ++applied;
        }
        break;
      }
      case 'D': {
        Delete d;
        if (parsers::nasdaq::parse_D(body(m), d) && m.locate == target) {
          book.remove(d.ref); ++applied;
        }
        break;
      }
      case 'U': {
        Replace r;
        if (parsers::nasdaq::parse_U(body(m), r) && m.locate == target) {
          book.replace(r); ++applied;
        }
        break;
      }
      default:  // S/H/Y/L/P/Q/B/I/N/... not book state
        break;
    }

    if (stats_every && n % stats_every == 0) {
      TopOfBook t = book.best();
      std::printf("n=%llu applied=%llu  bid=%lld x %u  ask=%lld x %u\n",
                  (unsigned long long)n, (unsigned long long)applied,
                  (long long)t.bid_px, t.bid_sz,
                  (long long)t.ask_px, t.ask_sz);
    }
  }

  const bool have = (target != kNoLocate) && sym.count(static_cast<uint16_t>(target));
  const std::string name = have ? sym[static_cast<uint16_t>(target)] : "?";
  const TopOfBook t = book.best();
  const Counters& c = book.counters();

  std::printf("--- done ---\n");
  std::printf("messages=%llu applied=%llu locate=%llu (%s)\n",
              (unsigned long long)n, (unsigned long long)applied,
              (unsigned long long)(target == kNoLocate ? 0 : target), name.c_str());
  std::printf("best bid=%lld x %u   ask=%lld x %u\n",
              (long long)t.bid_px, t.bid_sz, (long long)t.ask_px, t.ask_sz);
  std::printf("counters: unknown_ref=%llu clamped=%llu crossed=%llu\n",
              (unsigned long long)c.unknown_ref, (unsigned long long)c.clamped,
              (unsigned long long)c.crossed);
  return 0;
}
