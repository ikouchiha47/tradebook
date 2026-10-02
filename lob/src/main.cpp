// lob-replay: dump first N messages + type histogram. No book yet.
#include <cstdio>
#include <string>
#include "feed.h"

int main(int argc, char** argv) {
  std::string path = "../07302019.NASDAQ_ITCH50.gz";
  uint64_t limit = 20;
  bool hex = false;

  for (int i = 1; i < argc; ++i) {  // argv[0] is the program name
    std::string a = argv[i];
    if (a == "--file" && i + 1 < argc) path = argv[++i];
    if (a == "--limit" && i + 1 < argc) limit = std::stoull(argv[++i]);
    if (a == "--hex") hex = true;
  }

  FeedReader feed(path);
  if (!feed.open_ok()) {
    std::fprintf(stderr, "FAIL: cannot open %s\n", path.c_str());
    return 1;
  }

  NasdaqRawMsg m;  // single reusable message; refilled per lap
  uint64_t hist[256] = {};  // one counter per possible type byte
  uint64_t i = 0;

  while (i < limit && feed.next(m)) {
    hist[(unsigned char)m.type]++;

    std::printf("#%llu len=%u type=%c locate=%u\n", (unsigned long long)i, m.len,
                (m.type >= 32 && m.type < 127) ? m.type : '?', m.locate);
    if (hex) {
      std::printf("  bytes:");
      for (uint16_t b = 0; b < m.len; ++b) std::printf(" %02X", m.payload[b]);
      std::printf("\n");
    }
    ++i;
  }

  std::printf("--- hist over %llu ---\n", (unsigned long long)i);
  for (int t = 0; t < 256; ++t)
    if (hist[t]) std::printf("  %c : %llu\n", (t >= 32 && t < 127) ? t : '?', (unsigned long long)hist[t]);
  return 0;
}
