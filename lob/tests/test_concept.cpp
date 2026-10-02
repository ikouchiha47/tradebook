// Compile + TDD check for feed_concept.h. No gtest needed: plain assert.
// Build: clang++ -std=c++20 -I include tests/test_concept.cpp src/feed.cpp -lz -o /tmp/test_concept && /tmp/test_concept
#include <cassert>
#include <cstdio>
#include "feed_concept.h"

int main() {
  RawMsg a, b;  // two canned messages (no gz involved)
  a.type = 'A';
  a.locate = 1;
  a.len = 3;
  a.payload = {'A', 0, 1};
  b.type = 'E';
  b.locate = 1;
  b.len = 3;
  b.payload = {'E', 0, 1};
  const RawMsg arr[2] = {a, b};  // fixed array: VecFeed reads FROM here (copies into `out`)
  VecFeed fake{arr, arr + 2};    // begin/end range over the array
  assert(fake.open_ok());        // fake is "open" when given an array
  uint64_t seen = 0;             // count via the replay callback
  char types[2] = {};            // record delivery order
  uint64_t n = replay(fake, 10, [&](const RawMsg& m) { types[seen++] = m.type; });
  assert(n == 2);          // both delivered despite limit 10 (EOF stops us)
  assert(seen == 2);       // callback ran twice
  assert(types[0] == 'A' && types[1] == 'E');  // order preserved
  std::printf("concept OK: replay(VecFeed) delivered %llu in order\n", (unsigned long long)n);
  return 0;
}
