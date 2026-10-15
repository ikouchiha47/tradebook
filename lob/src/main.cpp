// lob-replay: replay ITCH 5.0 through parsers + order book for ONE symbol.
// Book has no locate dimension, so we filter to a single symbol (auto-picked
// from the first A/F unless --locate is given). Book type is selectable:
//   --book ladder (default) | map
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "book.h"
#include "feed.h"
#include "itch.h"

namespace {
constexpr uint64_t kNoLocate = UINT64_MAX;
inline std::span<const uint8_t> body(const NasdaqRawMsg& m) {
  return std::span<const uint8_t>(m.payload.data(), m.len);
}

struct Options {
  std::string path = "../07302019.NASDAQ_ITCH50.gz";
  uint64_t limit = 0;           // 0 = whole file
  uint64_t stats_every = 0;     // 0 = off
  uint64_t target = kNoLocate;  // single-symbol filter
  bool latency = false;         // per-message percentile report
  bool stages = false;          // read-vs-route stage split
  std::string book = "ladder";  // "ladder" | "map"
  int64_t ladder_base = 0;      // raw ticks (dollars x 10000)
  int64_t ladder_inc = 100;     // 100 raw ticks = 1 cent per slot
  size_t ladder_slots = 200000; // covers [base, base + slots*inc) = $0 .. $2000
  bool raw = false;             // read a pre-decompressed .bin (no zlib)
};
}  // namespace

template <class Book, class Reader>
static int run_replay(const Options& o, Book book, Reader feed) {
  if (!feed.open_ok()) {
    std::fprintf(stderr, "FAIL: cannot open %s\n", o.path.c_str());
    return 1;
  }

  NasdaqRawMsg m;
  uint64_t target = o.target;  // mutable: auto-picked from the first A/F
  std::unordered_map<uint16_t, std::string> sym;  // locate -> symbol (from R)

  uint64_t n = 0;              // messages read
  uint64_t applied = 0;        // messages that touched the book
  uint64_t payload_bytes = 0;  // wire payload bytes decoded
  uint64_t parse_errs = 0;     // malformed messages (size mismatch / bad layout)

  // Surface parse failures instead of silently skipping the message.
  auto on_parse_error = [&](char type) {
    ++parse_errs;
    std::fprintf(stderr, "parse error: type=%c len=%u locate=%u msg=%llu\n",
                 type, m.len, m.locate, (unsigned long long)n);
  };

  std::vector<uint32_t> samples;         // per-message ns (latency mode)
  const uint64_t kSampleCap = 20000000;  // ~80 MB cap; enough for stable p99.9
  auto t_prev = std::chrono::steady_clock::now();
  uint64_t read_ns = 0;   // time in feed.next (gzip + framing)
  uint64_t route_ns = 0;  // time in parse + book.apply
  auto t_stage_prev = t_prev;

  const auto t0 = std::chrono::steady_clock::now();
  while (feed.next(m)) {
    const auto t_read = std::chrono::steady_clock::now();
    if (o.stages) {
      read_ns += static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(t_read - t_stage_prev).count());
    }
    ++n;
    if (o.limit && n > o.limit) break;
    payload_bytes += m.len;

    switch (m.type) {
      case 'R': {  // reference: record symbol for all locates
        StockDir d;
        bool ok = parsers::nasdaq::parse_R(body(m), d);
        if (!ok) { on_parse_error('R'); break; }
        sym[d.locate] = d.symbol;
        break;
      }
      case 'A':
      case 'F': {
        AddOrder a;
        const bool ok = (m.type == 'A') ? parsers::nasdaq::parse_A(body(m), a)
                                        : parsers::nasdaq::parse_F(body(m), a);
        if (!ok) { on_parse_error(m.type); break; }
        if (target == kNoLocate) target = a.locate;  // auto-pick first traded symbol
        if (a.locate == target) { book.add(a); ++applied; }
        break;
      }
      case 'E': {
        Exec e;
        bool ok = parsers::nasdaq::parse_E(body(m), e);
        if (!ok) {
          on_parse_error('E');
          break;
        }

        if (m.locate == target) {
          book.reduce(e.ref, e.shares);
          ++applied;
        }

        break;
      }
      case 'C': {
        ExecPx c;
        bool ok = parsers::nasdaq::parse_C(body(m), c);
        if (!ok) { on_parse_error('C'); break; }

        if (m.locate == target) {
          book.reduce(c.ref, c.shares);
          ++applied;
        }

        break;
      }
      case 'X': {
        Cancel x;
        bool ok = parsers::nasdaq::parse_X(body(m), x);
        if (!ok) { on_parse_error('X'); break; }

        if (m.locate == target) {
          book.reduce(x.ref, x.shares);
          ++applied;
        }

        break;
      }
      case 'D': {
        Delete d;
        bool ok = parsers::nasdaq::parse_D(body(m), d);
        if (!ok) { on_parse_error('D'); break; }

        if (m.locate == target) { book.remove(d.ref); ++applied; }
        break;
      }
      case 'U': {
        Replace r;
        bool ok = parsers::nasdaq::parse_U(body(m), r);
        if (!ok) { on_parse_error('U'); break; }

        if (m.locate == target) { book.replace(r); ++applied; }
        break;
      }
      default:  // S/H/Y/L/P/Q/B/I/N/... not book state
        break;
    }

    if (o.stats_every && n % o.stats_every == 0) {
      TopOfBook t = book.best();
      std::printf("n=%llu applied=%llu  bid=%lld x %u  ask=%lld x %u\n",
                  (unsigned long long)n, (unsigned long long)applied,
                  (long long)t.bid_px, t.bid_sz,
                  (long long)t.ask_px, t.ask_sz);
    }

    if (o.latency) {  // interval = route work + one timer call (see note)
      const auto now = std::chrono::steady_clock::now();
      if (samples.size() < kSampleCap) {
        samples.push_back(static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - t_prev).count()));
      }
      t_prev = now;
    }

    if (o.stages) {
      const auto t_route = std::chrono::steady_clock::now();
      route_ns += static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(t_route - t_read).count());
      t_stage_prev = t_route;
    }
  }

  const bool have = (target != kNoLocate) && sym.count(static_cast<uint16_t>(target));
  const std::string name = have ? sym[static_cast<uint16_t>(target)] : "?";
  const TopOfBook t = book.best();
  const Counters& c = book.counters();

  const double secs =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("book=%s", o.book.c_str());
  if (o.book == "ladder") {
    std::printf(" base=%lld inc=%lld slots=%zu", (long long)o.ladder_base,
                (long long)o.ladder_inc, o.ladder_slots);
  }
  std::printf("\n");
  std::printf("elapsed=%.3fs  msgs/s=%.2fM  payload=%.1fMB  MB/s=%.1f\n",
              secs, static_cast<double>(n) / 1e6 / secs,
              static_cast<double>(payload_bytes) / 1e6,
              static_cast<double>(payload_bytes) / 1e6 / secs);
  std::printf("--- done ---\n");
  std::printf("messages=%llu applied=%llu locate=%llu (%s)\n",
              (unsigned long long)n, (unsigned long long)applied,
              (unsigned long long)(target == kNoLocate ? 0 : target), name.c_str());
  std::printf("best bid=%lld x %u   ask=%lld x %u\n",
              (long long)t.bid_px, t.bid_sz, (long long)t.ask_px, t.ask_sz);
  std::printf("counters: unknown_ref=%llu clamped=%llu crossed=%llu parse_errs=%llu\n",
              (unsigned long long)c.unknown_ref, (unsigned long long)c.clamped,
              (unsigned long long)c.crossed, (unsigned long long)parse_errs);

  if (o.stages && (read_ns + route_ns) > 0) {
    const double total_ns = static_cast<double>(read_ns + route_ns);
    std::printf("stages: read=%.3fs (%.0f%%)  route=%.3fs (%.0f%%)  [+2 clock calls/msg overhead]\n",
                static_cast<double>(read_ns) / 1e9, 100.0 * read_ns / total_ns,
                static_cast<double>(route_ns) / 1e9, 100.0 * route_ns / total_ns);
  }

  if (o.latency && !samples.empty()) {
    std::sort(samples.begin(), samples.end());
    const auto pct = [&](double p) -> uint32_t {
      const size_t idx = static_cast<size_t>((p / 100.0) * (samples.size() - 1));
      return samples[idx];
    };
    double sum = 0;
    for (uint32_t v : samples) sum += v;
    std::printf("latency(ns) N=%zu min=%u p50=%u p90=%u p99=%u p99.9=%u max=%u mean=%.1f\n",
                samples.size(), samples.front(), pct(50), pct(90), pct(99), pct(99.9),
                samples.back(), sum / static_cast<double>(samples.size()));
    std::printf("(per-message interval includes one steady_clock::now() call; ingest->book scope)\n");
  }
  return 0;
}

template <class Book>
static int dispatch_book(const Options& o, Book book) {
  if (o.raw) return run_replay(o, std::move(book), FeedReader<LinearBuf<RawSource>>(o.path));
  return run_replay(o, std::move(book), FeedReader<LinearBuf<GZSource>>(o.path));
}

int main(int argc, char** argv) {
  Options o;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--file" && i + 1 < argc) o.path = argv[++i];
    else if (a == "--limit" && i + 1 < argc) o.limit = std::stoull(argv[++i]);
    else if (a == "--locate" && i + 1 < argc) o.target = std::stoull(argv[++i]);
    else if (a == "--stats-every" && i + 1 < argc) o.stats_every = std::stoull(argv[++i]);
    else if (a == "--latency") o.latency = true;
    else if (a == "--stages") o.stages = true;
    else if (a == "--raw") o.raw = true;
    else if (a == "--book" && i + 1 < argc) o.book = argv[++i];
    else if (a == "--ladder-base" && i + 1 < argc) o.ladder_base = std::stoll(argv[++i]);
    else if (a == "--ladder-inc" && i + 1 < argc) o.ladder_inc = std::stoll(argv[++i]);
    else if (a == "--ladder-slots" && i + 1 < argc) o.ladder_slots = std::stoull(argv[++i]);
  }

  if (o.book == "ladder") {
    return dispatch_book(o, NasdaqBookParser<NasdqBookWithLadder>(
                               NasdqBookWithLadder(o.ladder_base, o.ladder_inc, o.ladder_slots)));
  }
  return dispatch_book(o, NasdaqBookParser<NasdqBook>(NasdqBook{}));
}
