# lob — ITCH 5.0 feed handler + limit order book (C++20)

Replays a NASDAQ TotalView-ITCH 5.0 capture (gzip, BinaryFILE framing) through a
zero-copy parser into a limit order book, for **one symbol** at a time.
This is portfolio project P1 (feed handler + book); matching engine is P2.

## Layout
```
include/   feed.h  itch.h  book.h  feed_concept.h
src/       main.cpp  feed.cpp  parsers/nasdaq.cpp  book.cpp
tests/     test_parsers.cpp  test_book.cpp  test_alloc.cpp
benches/   bench.cpp
docs/      README docs: ITCH-SPEC, DOMAIN-PRIMER, lifecycle, transcript,
           book, feed, models, robustness, PERFORMANCE, oss-landscape
```

## Build
```bash
brew install conan                 # once
make setup                         # conan install (zlib, gtest, benchmark)
make build                         # cmake --preset conan-release + build
```
Binaries land in `build/build/Release/` (cmake_layout nests them).

## Run
```bash
./build/build/Release/lob-replay --file ../07302019.NASDAQ_ITCH50.gz
```
Flags:
| flag | meaning |
|---|---|
| `--file PATH` | input .gz (default `../07302019.NASDAQ_ITCH50.gz`) |
| `--limit N` | stop after N messages (0 = whole file) |
| `--locate N` | restrict the book to one symbol (default: first traded) |
| `--stats-every N` | print top-of-book every N messages |
| `--latency` | per-message p50/p90/p99/p99.9 report |
| `--stages` | read-vs-route time split |

## Test
```bash
make test          # ctest -> lob_test (19 tests: parsers, book, alloc)
```

## Bench
```bash
make bench         # Google Benchmark, all parsers + book ops
# or with repetitions/error bars:
./build/build/Release/lob_bench --benchmark_min_time=0.2 --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true
```

## Profiling / bottleneck hunting

**Stage split first (no tooling):**
```bash
./build/build/Release/lob-replay --limit 20000000 --stages --file ../07302019.NASDAQ_ITCH50.gz
# -> stages: read=85% route=15%   (gzip decompress dominates)
```

**CPU profile with gperftools (closest to Go's pprof).**
macOS uses `DYLD_INSERT_LIBRARIES`, not `LD_PRELOAD`:
```bash
brew install gperftools
DYLD_INSERT_LIBRARIES=/opt/homebrew/lib/libprofiler.dylib \
  CPUPROFILE=/tmp/prof.out \
  ./build/build/Release/lob-replay --limit 20000000 --file ../07302019.NASDAQ_ITCH50.gz
```
To read the profile: Homebrew's gperftools does **not** install the `pprof`
script. Either fetch it from source, or use the built-in sampler:
```bash
# Option A: upstream pprof script (perl)
curl -fsSL https://raw.githubusercontent.com/gperftools/gperftools/gperftools-2.18.1/src/pprof -o /tmp/pprof
perl /tmp/pprof --text ./build/build/Release/lob-replay /tmp/prof.out

# Option B: native macOS sampler (zero deps, what we used)
./build/build/Release/lob-replay --file ../07302019.NASDAQ_ITCH50.gz &
PID=$!; sleep 4; sample $PID 4 -file /tmp/sample.txt; wait $PID
sed -n '/Call graph/,/^Total number/p' /tmp/sample.txt
```

**Linux** (has `perf`):
```bash
perf stat -e cycles,instructions,cache-misses,branch-misses \
  ./build/build/Release/lob-replay --limit 1000000 --file ../07302019.NASDAQ_ITCH50.gz
perf record -g ./build/build/Release/lob-replay --limit 1000000 --file ../07302019.NASDAQ_ITCH50.gz
perf report
```

**What we found:** ~75% of replay time is zlib `inflate_fast` (gzip), ~13% is the
parsers + book. The `.gz` is the bottleneck, not the code. Fix: decompress once to
a raw `.bin`, use libdeflate/zlib-ng, or use the live (uncompressed) feed path.
Details in `docs/PERFORMANCE.md`.

## Notes
- Single-threaded, one symbol per replay (the book has no `locate` dimension).
- All offsets/prices/sizes: `docs/ITCH-SPEC.md`; struct fields: `docs/models.md`.
