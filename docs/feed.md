# Feed — function contracts (feed.h / feed.cpp / main.cpp driver)

Wire format: `[2B BE length][N payload bytes]`, repeated, gzip-compressed.
Every ITCH payload opens with the 11B header: type(1) locate(2) tracking(2)
timestamp(6). Hot-path rules: no alloc per message after warmup, no throws,
BE decoded arithmetically (never by reinterpret-cast).

## FeedReader(path) / ~FeedReader — RAII owner
- Ctor: `gzopen(path, "rb")`; null on failure → `open_ok()` false.
- Dtor: `gzclose` iff opened. Non-copyable (`= delete` both): two owners would
  double-close one handle. No moves in v1 (single owner in main).

## next(RawMsg&) -> bool — one framed message per call
1. `read_exact(hdr, 2)`; fail → false (clean EOF or truncated tail: stop).
2. `len = be16(hdr)`; `len == 0` → recurse (padding record, no message).
3. Grow `out.payload` only if this message is bigger than any seen before.
   Otherwise the existing buffer is reused as-is.
4. `read_exact(payload.data(), len)`; fail → false. New bytes overwrite the
   old ones in place; nothing is allocated, nothing is freed.
5. Stamp `len`, `type = payload[0]`, `locate = be16(payload+1)` iff `len >= 3`
   else 0. `++n_`; return true.
- One buffer for the whole run: the caller's struct keeps the same address
  while every call paints fresh bytes over it. Bytes past `len` may hold the
  previous message's tail — nobody reads them, `len` is the boundary.

## read_exact(gz, dst, n) — static helper, this file only
Loops `gzread` until n bytes land or `r <= 0` (EOF/error → false). Exists because
one `gzread` may return short (deflate block edges, page-cache chunks).

## be16(p) — 2B big-endian decode
`(p[0] << 8) | p[1]`. Pure value arithmetic: correct on any host endianness.
Same helper decodes framing lengths and locate fields.

## Byte walkthrough — one real record, all sizes (dump #0, `S`)
Decompressed stream bytes, in order:
```
[00 0C] [53] [00 00] [00 00] [09 FA 4D 3A D2 6B] [4F]
 2B len  type   loc    track   timestamp, 6B        event 'O'
 = 12 ──→ read_exact pulls exactly these 12 payload bytes ──→ RawMsg{len 12, type 'S', locate 0}
```
Framing (2B) tells HOW MUCH; payload byte 0 tells WHAT; bytes 1..2 tell
WHICH stock; the rest is per-letter fields (see ITCH-SPEC.md tables).
Next record starts immediately after: `[00 27][52 ...38 more...]` (len 39, `R`).

## main loop (lob-replay) — the driver
Owns `feed + book + sym`; pumps `while (i < limit && feed.next(m))`:
route on `m.type` (`R`→sym map, `A/F`→book.add, `E/C/X/D/U`→mutators,
rest count-only), print `--hex` dumps + 256-slot type histogram on demand.
Exit 0 on EOF/limit, 1 on unopenable file. owns nothing else.
