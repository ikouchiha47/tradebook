# Measurement methodology — what to check, with what, and the HyDE habit

How we actually arrived at the map-vs-ladder conclusions — not just benchmarks — and what
should have been measured at each point. M4-verified facts: 128 B cache lines, 16 KiB
pages, 4 MiB L2 (`hw.cachelinesize=128`, `hw.pagesize=16384`, `hw.l2cachesize=4194304`).

## 1. The ladder of evidence (weakest → strongest)

| # | Method | Answers | Our use |
|---|---|---|---|
| 1 | Read the code, guess | nothing (a hypothesis) | "the map feels expensive" |
| 2 | Wall-clock / ns-op micro-bench | *that* A differs from B here | `BM_Book*`, the dense A/B |
| 3 | Parameterized sweep (N × density) | *where* A differs from B | `BM_Sweep*` (gap), `BM_Scale*` (size→cache) |
| 4 | Stage split (read vs route) | *whether* it matters end-to-end | `--stages`: gzip 85 / book 15 |
| 5 | Sampler profile (`sample`) | *where* inside the winner | zlib `inflate_fast` ~64% |
| 6 | Hardware counters (cache/branch/TLB/IPC) | *why* — the mechanism | **blocked here** (see §2); Linux path given |
| 7 | End-to-end A/B, identical output | *whether to ship* | `--book ladder\|map`, same best+counters |

Skipping 4 is how you optimize the wrong 15%. Skipping 6 is how you ship a story instead
of a mechanism — acceptable only if a structural sweep (3) already shows the slope.

## 2. What we should have checked — and the macOS blockers

To *prove* "tree misses cache, bitmap doesn't," the counters are:

```
cycles, instructions, branches, branch-misses,
L1-dcache-load-misses, LLC-load-misses, dTLB-load-misses (+ IPC = insn/cycle)
```

Expected signatures: map → LLC-misses/op and cycles/op climb with N past L1/L2, IPC
sags; ladder → flat misses/op, flat cycles/op; sparse-wide ladder → watch dTLB-misses/op
(a 2 MiB band is 128 pages here).

**Blockers on this box (verified, with output):** no `perf`, no `valgrind` (arm64 darwin),
no `gdb`; `/usr/bin/xctrace` refuses without full Xcode
(`tool 'xctrace' requires Xcode… CommandLineTools instance`); `dtrace` needs privileges
and exposes no PMC provider. So PMU counters are **unavailable** here.

Two real paths:
- **Linux CI:** `perf stat -e cycles,instructions,branches,branch-misses,L1-dcache-load-misses,LLC-load-misses,dTLB-load-misses,iTLB-load-misses -r 5 -- ./bin …`
  plus `-M TopdownL1`; pin the core, repeat, read mean±stddev and IPC/percentages.
- **This Mac with Xcode or a kperf wrapper:** `xctrace record --template 'Preconfigured CPU
  Counters'` (note: the template records *no events by default* — preconfigure Cycles,
  Instructions, L1D_CACHE_MISS_LD, L2-agent misses, BRANCH_MISPRED_NONSPEC first), or
  `sudo mperf-stat -e cycles -e instructions -e l1d-cache-misses -e llc-misses
  -e branch-misses -- ./bin …` (third-party kperf wrapper; per-chip events enumerated
  from `/usr/share/kpep/as4.plist`; run one counter group per pass, ≤ ~10 counters).
- **Fallback on this Mac:** the dense SCALE sweep to 1M levels — map add/remove
  45 → 258 ns, ladder 30 → 46 ns. No PMU, but the slope *is* the cache reading.

`scripts/counters.sh` codifies all three: it proves the blocker, prints the Linux command,
and runs the empirical fallback.

**Actually measured (AWS Linux, both arches):** we ran the probe on c7g (Graviton3) and
c7i (Sapphire Rapids) via `scripts/aws/ec2-probe.sh`. At N=1M the map hit ~30 L1-dcache
misses/op and ~950 cycles/op (IPC ~1.5), the ladder ~0.005 misses/op and ~225 cycles/op
(IPC ~3.7). Full tables + reproduction in `aws-perf-counters.md`. The mechanism is now
counter-proven, not just inferred.

## 3. The HyDE habit (hypothetical-first analysis)

HyDE (Gao et al.): write the hypothetical ideal answer *first*, then let measurement
ground or falsify it. Applied to perf work:

1. **Predict in writing, with numbers.** Before touching the ladder: "I expect
   LLC-misses/op ≈ ~log N for the map past L1, flat for the ladder; dTLB flat for both at
   this band; crossover near N ≈ L1/node-bytes." Mechanism + magnitudes, on paper.
2. **Measure as the lossy filter.** Counters (or the sweep) can only falsify stories you
   told. A bare wall-clock result is a query with no grounding — it confirms anything.
3. **One question per pass.** Branches this pass, cache next, TLB after — then rewrite
   the hypothetical. (Apple's counter modes work this way.)
4. **Keep a prediction ledger.** Predicted counter deltas per optimization, with the N /
   density condition. Deltas that *don't* appear are the useful negatives — they localize
   the real bottleneck.
5. **Distrust cache-resident confirmations.** Our dense 200-level A/B "proved" the map
   wins — true, and useless, because the regime was its best case. Ground every claim
   against a *hard* regime (out-of-cache, sparse, TLB-hostile) before trusting it.

## 4. Reusable scripts (run these, don't re-derive)

```
./scripts/sweep.sh       # micro + N×gap sweep + dense scale → scripts/results/
LIMIT=10000000 ./scripts/replay-ab.sh   # hyperfine end-to-end map-vs-ladder
./scripts/profile.sh     # `sample`-based profile of a bounded replay
./scripts/counters.sh    # blocker proof + Linux perf path + empirical cache read
```

Conventions that saved us (also in `benchmarks/PERFORMANCE.md`): `DoNotOptimize` the
*output struct*, mutate an input byte per iteration so the compiler can't hoist the
decode, ignore Apple Silicon "CPU MHz," report p50/p99 or full sweeps — never a single
mean — and present ranges at large N (the map's 100k number wobbled 84↔182 ns across
runs; that instability is data, not noise to average away).

## 5. Wrong-assumption ledger (measurement edition)

- "PMU counters are a `perf stat` away." Wrong here — macOS CLT has no path; full Xcode
  (`xctrace` + preconfigured CPU Counters) or a kperf wrapper is required.
- "A dense 200-level A/B decides the book." Wrong — cache-resident regimes always flatter
  the tree. The N×gap sweep and the 1M scale run were the actual evidence.
- "ns/op is the measurement." Wrong — it's the weakest retention of the mechanism. The
  durable signals are misses/op, cycles/op, IPC, and the slope across N.
- "x86 cache geometry transfers." Wrong on M4 — verify `hw.cachelinesize`/`hw.pagesize`
  (128 B / 16 KiB here) before doing working-set math.
