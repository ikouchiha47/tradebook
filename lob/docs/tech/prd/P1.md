# START-HERE — P1 ITCH50 LOB (C++20 + Conan + Makefile)

Goal: replay `../07302019.NASDAQ_ITCH50.gz` into a correct LOB. You code, this doc is the contract.
Toolchain: Apple Clang 17 (`-std=c++20`), CMake 4.3, Conan (install via `brew install conan`), `make` wraps everything. See `CONAN-SETUP.md` + `../Makefile`.

## conan new vs conan build — answer
- `conan new` = scaffolds a NEW library template. You don't need it; I already give you `conanfile.txt` + `CMakeLists.txt` layout below.
- `conan build` = creator flow (build a package). You are a CONSUMER. Use only:
  `conan install . --output-folder=build --build=missing -s compiler.cppstd=20`
  then `cmake --preset conan-release && cmake --build build/Release`.
- In short: never run `conan new`/`conan build` here. `make setup && make build` does the right two commands.

## Step-by-step (do in order, each has Done)
- T0 scaffold: create layout below + `conanfile.txt` (zlib, gtest, benchmark as test_requires) + `CMakeLists.txt` (C++20, lob-replay, lob_test, lob_bench). Done: `make build` green on stubs.
- T1 framing: stream gz (zlib), loop 2B BE len + payload, histogram by `msg[0]`. Done: `--limit 100000` prints A/F/E/C/X/D/U/P counts, zero framing panics.
- T2 adds: parse 11B header (locate/tracking/ts 6B BE) + R39 + A36/F40 (price/10000 → int64 ticks). Insert into book. Done: add-only replay shows best().
- T3 mutates: E31/C36/X23/D19/U35 + unknown-ref counter (no throw). Done: fixture A→E→X→D yields expected best; unknown E increments counter.
- T4 book correctness: cached best bid/ask, depth on demand, invariants (bid<ask, no zero levels). Done: `make test` (gtest) green.
- T5 replay determinism: `--locate --limit --stats-every`, top-hash every 1M. Done: same 1M slice twice → identical hash, `make replay LIMIT=1000000`.
- T6 bench: Google Benchmark micro + `make perf`. Done: `make bench` table + one-line method note.

## Files/folders expected (create exactly this, rooted at lob/ itself)
```
./
  Makefile            (already there — don't touch)
  conanfile.txt       (from CONAN-SETUP.md)
  CMakeLists.txt      (C++20, ZLIB, GTest, benchmark)
  src/main.cpp        (lob-replay bin: args, loop, stats, hash)
  src/feed.cpp        (gz stream + len-prefix framing)
  src/itch.cpp        (all message parsers, BE reads)
  src/book.cpp        (apply, cached best, counters)
  include/feed.h itch.h book.h
  tests/test_book.cpp (gtest: add, mutate, unknown-ref, invariants)
  benches/bench.cpp   (benchmark: parse, apply, best)
```

## Modularize like this
- `itch.*` knows wire only (bytes→structs, no book logic). Pure, fastest to unit test.
- `book.*` knows state only (structs→book, no I/O). Owns `unordered_map<id,Order>` + `map<price,Level>` + cached best + counters.
- `feed.*` knows framing only (gz→messages). Owns `--locate/--limit` filter.
- `main.cpp` wires them (parse→apply→stats/hash). No logic, just loop.
- Rule: itch never includes book; main includes both; tests include itch+book, never main.

## Minimum KT — read this, skip rest
- ITCH 5.0: 11B header (type/locate/tracking/ts-6B-BE ns). Length prefix 2B BE per message (BinaryFILE).
- Sizes: A36 F40(+MPID) E31 C36 X23 D19 U35 R39 P44. Price u32/10000. Lifecycle: A/F add → E/C/X reduce → D kill → U replace (old→new). P = print only, never touches book.
- Book: price int64 ticks, never float. Missing E/C refs + crosses + negative qty → count + continue, never throw.
- Full spec only if stuck: NASDAQ NQTVITCH 5.0 PDF.

## What to optimize vs ignore
- Optimize: BE parse (no per-msg alloc, reuse buffers), `apply()` (hash lookup + cached top, no `new`/`string`), `best()` O(1) cached.
- Ignore in v1: lock-free, SIMD, mmap, multicore, ladder/bitboard (note as P1.5 bench vs BTreeMap).

## What to bench + libraries
- Libs: `zlib` (gz), `gtest` (correctness), `benchmark` (micro). All via `conanfile.txt` test_requires for the latter two.
- Bench: micro `parse/apply/best` ns/op + p50/p99 (Google Benchmark repetitions), macro `make replay` msgs/sec + GB/s, system `make perf` (cycles, cache-miss, branch-miss). Targets: >1M msgs/s, p99 apply <200ns, zero heap alloc in apply().
