# OSS landscape — repos to read (verified 2026-10-02)

Purpose: reference material for P1 (feed+LOB), P2 (matching), P3 (latency/strategy).
Stars/licenses/activity verified via GitHub API on 2026-10-02. Read-only inspiration;
copying rules per license at the bottom.

## Category 1 — Retail / crypto / backtesting frameworks (huge stars, commercial use)
| repo | lang | ★ | license | commercial evidence | read for |
|---|---|---|---|---|---|
| freqtrade/freqtrade | Python | 55.0k | GPL-3.0 | large retail/prop bot base | strategy/backtest loop |
| vnpy/vnpy | Python | 45.7k | MIT | dominant China futures/CTA | event-engine + gateway split |
| ccxt/ccxt | Python | 44.2k | MIT | de-facto exchange API lib | normalized venue API |
| microsoft/qlib | Python | 49.1k | MIT | Microsoft; fund adoption unknown | research pipeline |
| nautilus_trader | Rust/Python | 29.6k | LGPL-3.0 | Nautech institutional tier | production L2 book + matching core |
| QuantConnect/Lean | C# | 21.9k | Apache-2.0 | QuantConnect cloud | broker/model abstraction |
| hummingbot | Python | 20.3k | Apache-2.0 | retail/prop MM | connector + MM architecture |
| jesse-ai/jesse | Python | 8.6k | MIT | retail crypto | strategy loop |
| backtrader | Python | 23.4k | GPL-3.0 | retail backtesting (stale 2024-08) | backtest API |
| barter-rs | Rust | 2.3k | MIT | retail/research | event-driven Rust design |
| nkaz001/hftbacktest | Rust | 4.8k | MIT | researcher/retail HFT | L2 book + queue position |

## Category 2 — Production low-latency infra (genuinely battle-tested)
| repo | lang | ★ | license | commercial evidence | read for |
|---|---|---|---|---|---|
| aeron-io/aeron | Java | 8.9k | Apache-2.0 | EDXM, HSBC, Man Group, Bullish | lock-free transport, flow control, cluster |
| LMAX-Exchange/disruptor | Java | 18.5k | Apache-2.0 | runs the LMAX exchange | ring buffer, mechanical sympathy |
| aeron-io/simple-binary-encoding | Java | 3.5k | Apache-2.0 | Adaptive; FIX SBE standard | binary wire encoding |
| OpenHFT/Chronicle-Queue | Java | 3.8k | Apache-2.0 | Chronicle Software (banks) | off-heap persistence/queue |
| questdb/questdb | Java | 17.4k | Apache-2.0 | OKX, Brevan Howard, B3, Nomura LD | time-series store |
| perspective-dev/perspective | Rust | 11.3k | Apache-2.0 | JPMorgan-origin, FINOS | streaming analytics UI |

## Category 3 — Exchange-side matching / book engines
| repo | lang | ★ | license | note |
|---|---|---|---|---|
| exchange-core/exchange-core | Java | 2.6k | Apache-2.0 | full LMAX-style book+risk engine; stale 2023 |
| paritytrading/parity | Java | 500 | Apache-2.0 | complete trading stack; archived 2022 |
| CoinTossX | Java | 122 | MIT | clearest price-time matcher; stale 2022 |
| KareemJandali/itchbook | C++20 | 64 | MIT | slab + intrusive levels + refmap; active |
| bbalouki/itchcpp | C++20 | 14 | MIT | clean small book; active |
| martinobdl/ITCH | C++ | 282 | Apache-2.0 | canonical full-depth ITCH50 reconstructor; stale 2021 |
| joaquinbejar/OrderBook-rs | Rust | 544 | MIT | active Rust LOB with benches |
| i25959341/orderbook | Go | 558 | MIT | Go LOB |
| Crypto-toolbox/HFT-Orderbook | C | 1.4k | MIT | cache-friendly price levels |
| bmoscon/orderbook | Python+C | 322 | GPL-3.0 | C extension LOB |

## Messaging / serialization (the open, production-proven layer)
- `aeron-io/aeron` — transport: lock-free ring, flow control, clustered log. Concepts translate to SPSC/MPSC.
- `aeron-io/simple-binary-encoding` (SBE) — schema-driven binary encoding; how to layout wire messages.
- `OpenHFT/Chronicle-Queue` — off-heap queue/persistence (banks).
- `questdb/questdb` — columnar time-series ingestion.
- `quickfix/quickfix` — FIX engine (order-entry/connectivity), C++.

## Ranked reads for a C++ ITCH/LOB portfolio
1. roq-trading/roq-api — C++, MIT, modern, actively maintained; closest structure to `lob/`.
2. quickfix/quickfix — C++, custom-BSD; the FIX order-entry *out* path we don't have yet.
3. aeron-io/aeron + SBE — Java; read for lock-free transport + binary encoding concepts.
4. LMAX-Exchange/disruptor — Java; the ring-buffer bible.
5. exchange-core/exchange-core — Java; matching-engine design (stale).
6. nkaz001/hftbacktest — Rust; L2 book + queue-position modeling.
7. martinobdl/ITCH — C++; canonical ITCH full-depth reconstruction algorithm (stale, low stars).

## Duplicate-ref handling (checked in real repos)
- `nautilus_trader` — HANDLED: on add, if id exists at a different price, erase from old level
  (drop empty level); same price overwrites in place. Test: `..._leave_no_ghost`.
- `itchbook`, `itchcpp`, `martinobdl/ITCH` — OVERWRITE-ONLY: `operator[]`/insert overwrites the
  order map while old level contribution leaks → silent double-count.
- `OrderBook-rs` — duplicate check exists ONLY in snapshot restore (`DuplicateOrderId`), not the live path.
- `itchy-rust` — parser only, no book.
→ Our `add()` should follow the nautilus policy (find ref → unwind old level → insert).

## License reality (portfolio vs commercial)
- MIT / Apache-2.0 / BSD — safest; reuse freely with attribution. (roq-api, aeron, disruptor, SBE,
  Lean, questdb, hummingbot, vnpy, ccxt, qlib, databento, barter-rs, hftbacktest, quickfix custom-BSD).
- LGPL-3.0 (nautilus) / LGPL-2.1 (man-group/arctic) — fine for portfolio; dynamic-link for closed products.
- GPL-3.0 (freqtrade, backtrader, tectonicdb, bmoscon/orderbook) — copyleft; read/learn fine, copying into a
  distributed product makes it GPL.
- AGPL-3.0 — network use counts as distribution (SaaS-hostile). None of the verified repos is AGPL.
- BSL-1.1 (man-group/ArcticDB) — source-available, NOT open source; paid license for production. Read-only.
- No license at all — all rights reserved. Read-only; copying is not permitted (ask author).
- Rule: build from MIT/Apache/BSD; read GPL/AGPL/BSL sources for ideas only.
