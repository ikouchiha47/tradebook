# ITCH 5.0 research — data on disk, per-type specs (for itch.*)

Source of truth: NASDAQ TotalView-ITCH 5.0 spec (NQTVITCHSpecification_5.0.pdf).
File: `07302019.NASDAQ_ITCH50.gz` = BinaryFILE framing (2B BE length + payload) over gzip.
All multi-byte numbers big-endian. Prices are uint32 ÷ 10,000 → int64 ticks. Text is ASCII space-padded.

## Field glossary (what ref/side/etc. mean)
- `type` (1B char): message letter. Routes everything: R/S = reference, A/F = add order, E/C = execute, X = cancel, D = delete, U = replace, P/Q/B = prints (tape only).
- `locate` (u16): per-day stock index (R assigns locate→symbol). 0 = global (S). The join key: every order message says which symbol via locate, not text.
- `tracking` (u16): Nasdaq internal sequence; ignore in P1 (count only).
- `timestamp` (6B): ns since midnight ET. Order across the day, not wall-clock monotonic across channels — never assume sorted.
- `ref` / `order-ref` (u64): exchange-assigned order id. THE identity of a resting order: adds create it, E/C/X/D/U reference it. Never reuse meaning across orders (U issues a NEW ref).
- `side` (char B/S): Buy or Sell. Only on adds (A/F) and replaces (new side).
- `shares` (u32): quantity for this event (add size, executed size, cancelled size — read per type).
- `price` (u32): limit price ×10,000. $10.50 → 105000. Prints (C) carry their own execution price (may differ from book).
- `stock` (8B text): symbol, space-padded ("A       "). Only on A (F adds MPID too). Later messages use locate instead.
- `mpid` (4B text, F only): attributed firm id. T2: accept + ignore.
- `event code` (1B char, S): O=Start, S=Start of System Hours, Q=Market Hours, M=End, E=Halted, C=Closed.

## 11B common header (every message, offsets 0..10)
| Off | Len | Field |
|-----|-----|-------|
| 0 | 1 | type |
| 1 | 2 | locate |
| 3 | 2 | tracking |
| 5 | 6 | timestamp |

## Per-type layouts (payload offsets; total incl. header)
- `S` 12B: header + `[11]` event code. Ex: `53 00 00 00 00 09 FA 4D 3A D2 6B 4F` → S/loc0/O.
- `R` 39B: header + `[11..18]` stock(8) `[19]` market-cat `[20]` fin-status `[21..24]` round-lot u32 `[25]` lots-only `[26]` class `[27..28]` sub-type `[29]` authenticity `[30]` short-threshold `[31]` IPO `[32]` LULD tier `[33]` ETP `[34..37]` leverage `[38]` inverse. Ex: our dump `52 00 01 ... 41 20×7 ...` → loc1/symbol "A"/lot 100.
- `A` 36B (Add, no MPID): header + `[11..18]` ref u64 `[19]` side `[20..23]` shares u32 `[24..31]` stock(8) `[32..35]` price u32.
- `F` 40B (Add, MPID): A + `[36..39]` mpid(4).
- `E` 31B (Executed): header + `[11..18]` ref `[19..22]` executed shares u32 `[23..30]` match number u64. Reduces resting qty.
- `C` 36B (Executed with price): E + `[31]` printable flag + `[32..35]` price u32. Printable FIRST (verified: byte31 always Y/N across samples; price $6.59 sane), then price. Hidden/late prints; reduce + tape. CORRECTED 2026-09: was listed as price@31, off by one.
- `X` 23B (Cancel): header + `[11..18]` ref `[19..22]` cancelled shares u32. Partial reduce.
- `D` 19B (Delete): header + `[11..18]` ref. Full removal.
- `U` 35B (Replace): header + `[11..18]` old ref `[19..26]` new ref `[27..30]` shares u32 `[31..34]` price u32. Atomic cancel+add (new id, loses queue priority).
- `P` 44B (Trade, non-cross): tape print only — NEVER touches book. Count + log.

## T2 scope
Parse R/A/F fully (verify `len` == 39/36/40, else false). E/C/X/D/U layouts above are for T3; P/Q/B/I/N count-only.
