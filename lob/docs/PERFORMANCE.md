# Performance — ITCH5.0 feed + LOB (P1)

## Machine / build
- Apple M4, 10 cores, 16 GB, macOS 15.5 (Mac16,10)
- Apple clang 17.0.0, CMake 4.3.1
- Release build (`-O3 -DNDEBUG`), single-threaded, no pinning
- Data: `07302019.NASDAQ_ITCH50.gz` (TotalView-ITCH 5.0, gzip, BinaryFILE framing)

## Scope (be honest)
- **Measured:** gzip-decompress → frame → parse → apply to the book, for ONE symbol
  (locate 494 = ARGX). This is **ingest → book**, NOT wire-to-wire latency.
- Also measured: per-message percentiles (`--latency`), read-vs-route split (`--stages`).
- Not measured: network receive, multi-symbol parallelism.

## Macro — full-day replay (single symbol)
`./lob-replay --file ../07302019.NASDAQ_ITCH50.gz`

| metric | value |
|---|---|
| messages processed | 282,229,684 |
| elapsed | 25.293 s |
| throughput | **11.16M msgs/s** |
| payload decoded | 8,097.2 MB (8.1 GB) |
| payload rate | **320.1 MB/s** |
| applied (locate 494) | 122,254 |
| counters | unknown_ref=0, clamped=0, crossed=0 |

20M-message warm slice: 11.84M msgs/s, 357.5 MB/s (same counters = 0).
Zero counters over the whole session ⇒ the ARGX reconstruction held all day
(no unknown refs, no over-takes, no crossed book).

## Latency — per-message percentiles (`--latency`, 5M msgs)
| pct | ns |
|---|---|
| p50 | 41 |
| p90 | 42 |
| p99 | 83 |
| p99.9 | 34,458 |
| max | 1,360,958 |
| mean | 88.1 |

p50/p99 are tight (41 / 83 ns). The far tail (p99.9 ≈ 34 µs, max ≈ 1.4 ms) is **gzip
refills hitting disk/page-cache + OS scheduling, not the book** — expected for a file
replay. Each sample includes one `steady_clock::now()` call (timer overhead); scope is
ingest → book.

## Micro — Google Benchmark (ns/op, real payloads)
`./lob_bench --benchmark_min_time=0.2`

| benchmark | ns/op | what |
|---|---|---|
| BM_ParseA | 1.74 | decode A (ref/side/shares/price) |
| BM_ParseF | 1.75 | decode F (A + MPID) |
| BM_ParseX | 2.00 | decode X (ref/cancelled) |
| BM_ParseR | 2.60 | decode R (locate + symbol) |
| BM_ParseD | 3.72 | decode D (ref) |
| BM_ParseE | 6.09 | decode E (ref/shares/match) |
| BM_ParseC | 6.44 | decode C (E + printable + price) |
| BM_ParseU | 6.57 | decode U (old/new ref, shares, price) |
| BM_BookBest | 0.320 | read cached top-of-book |
| BM_BookReduce | 3.33 | partial reduce (E/X path) |
| BM_ParseThenApplyA | 8.57 | parse one real A + file it (compute only, no gzip) |
| BM_BookAdd | 14.2 | add a new order |
| BM_BookRemove | 17.8 | remove an order |
| BM_BookReplace | 35.9 | replace = remove-old + add-new |
| BM_BookDepth5 | 42.1 | build a 5-row depth snapshot |

Reading: all 8 parsers are 1.7–6.6 ns — decode is cheap and field-count-driven
(A/F/X minimal; E/C/U carry match/price so ~6 ns). On the book: `best()` is a
cached struct copy (0.32 ns); partial `reduce` touches two containers but no node
alloc (3.3 ns); `add`/`remove` (14/18 ns) pay `std::map`+`unordered_map` node ops +
`refresh()`; `replace` = remove+add (36 ns); `depth(5)` pays a small vector build (42 ns).
`BM_ParseThenApplyA` = the pure-compute per-order path (parse + file), no I/O.

**Takeaway: the book's read/reduce path is near-free; writes cost map nodes.**
`best()` (0.3 ns) and partial `reduce` (3.3 ns) are essentially free; `add`/`remove`
(14–18 ns) and `replace` (36 ns) pay `std::map`/`unordered_map` node insert/erase +
pointer chasing. That's the compute hotspot to attack with a tick ladder (ADR-0001).

## Bottlenecks (profiling)
Stage split (`--stages`, 20M msgs): **read 85% / route 15%** — the cost is gzip
decompression + framing, not the book.

Profiler note: macOS has no `perf`; Homebrew's gperftools ships **no `pprof`
script**, so the native `sample <pid>` was used on a live replay. Hottest stack:

| region | share of samples |
|---|---|
| `inflate_fast` (zlib) | ~64% |
| other `inflate` paths | ~7% |
| `gz_read`/`gzread` memmove | ~4% |
| `read(2)` (kernel) | ~2% |
| `main` = parse + book + loop | ~13% |

Conclusion: **~75% of replay time is zlib inflate** — inherent to reading a `.gz`.
The ITCH parsers + order book are ~13–15% (consistent with the micro benches:
B parse 1.7–6.6 ns, book ops 0.3–42 ns; the file I/O dominates).

How to remove it:
- **Decompress once** (`gunzip -c` to a raw `.bin`) and replay the uncompressed
  BinaryFILE — zlib leaves the hot path (`feed.next` degrades to a memcpy/read).
- **libdeflate / zlib-ng / ISA-L** — 2–3× faster inflate than stock zlib.
- **Live feed**: no decompression at all (raw MoldUDP64/UDP multicast) — the real
  low-latency path (P3).

## Methodology / gotchas
- `DoNotOptimize(out)` on the **output struct**, not the `bool` return — otherwise the
  compiler dead-code-eliminates the field writes.
- Input bytes are **mutated each iteration** (`d[19] ^= 1`) — otherwise the compiler
  hoists the loop-invariant decode and reports sub-ns (this happened: ParseA read
  0.98 ns hoisted → 1.77 ns honest).
- Benchmark's "CPU MHz" is misreported on Apple Silicon (says ~24 MHz) — ignore.
- `perf stat` is Linux-only; on macOS use `xcrun xctrace`/`time`. On a Linux CI:
  `perf stat -e cycles,instructions,cache-misses,branch-misses ./lob-replay --limit 1000000`.

## Reproduce
```
conan install . --output-folder=build --build=missing -s compiler.cppstd=20
cmake --preset conan-release && cmake --build build/build/Release -j
./build/build/Release/lob-replay --file ../07302019.NASDAQ_ITCH50.gz   # macro
./build/build/Release/lob_bench  --benchmark_min_time=0.2              # micro
```
