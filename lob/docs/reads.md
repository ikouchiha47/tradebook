# Reads — blogs/papers worth your time (all URLs live-checked 2026-10-05)

## Firm engineering (first-hand)
- Jane Street — How to Build an Exchange (JX matching engine, replication, µs)
  https://www.janestreet.com/tech-talks/building-an-exchange/
- HRT Beat — Huge Pages Pt 1 & 2 (TLB cost, 4.5×; production memory tuning)
- HRT Beat — Engineering/Interviewing at HRT; HRT intern NIC-to-NVME (DPDK/SPDK, pinning, tails)
- Databento — Serving real-time US options tick data on one server (line-rate OPRA, MBO)
- Databento — Why exporting inline matters (feed-handler architecture)
- Optiver Insights (mixed; use selectively)

## Architecture classics
- Martin Fowler — The LMAX Architecture (~6M orders/s single-thread + Disruptor)
- Martin Thompson — Mechanical Sympathy (cache lines, false sharing, barriers)
- Disruptor paper (sequence barriers; PDF)
- Aeron docs + trading resources (rings, flow control, cluster)
- Jeff Preshing — An Introduction to Lock-Free Programming (atomics fundamentals)

## Order book / matching design
- howtohft — How to Build a Fast Limit Order Book (2011; array-of-levels + hash, anchoring note)
- QuantCup 2010 (low-latency matching-engine design exercise)
- Databento docs — Constructing the limit order book (MBO→book, sequences)
- Jane Street exchange talk (cross-listed: queue priority, deterministic replay)

## Market data / ITCH / feeds
- Databento — What is the ITCH protocol? (MBO vs MBP, venue variants; best short primer)
- Databento — Nasdaq TotalView live (prop vs SIP, PTP, capture at NY4)
- amBrain — L2 bursts: gaps, recovery, fan-out (practitioner, vendor-biased)
- MoldUDP64 spec PDF (primary doc: session/sequence/heartbeat/end-of-session)

## Perf methodology
- Gil Tene — How NOT to Measure Latency (coordinator omission, HdrHistogram, p99.9)
- Brendan Gregg — Flame Graphs; perf Examples (stat/record, cache/IPC)
- Igor Ostrovsky — Gallery of Processor Cache Effects
- Ulrich Drepper — What Every Programmer Should Know About Memory (long, authoritative)
- Emery Berger — Performance Matters (measurement artifacts)

## Skip / caution
- 1024cores is dead (redirects to localhost) — use web.archive.org copy.
- HRT Beat/Cloudflare-blocked pages 403 to bots, valid in a browser.
- Avoid SEO spam (iotdigitaltwinplm, pixel case studies, generic Medium feed-handler posts).
