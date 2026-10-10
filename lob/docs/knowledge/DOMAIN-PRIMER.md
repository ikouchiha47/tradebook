# Domain primer — what you're actually parsing (read before itch.*)

## Cast (who acts)
- **Owner** (trader/algo): posts orders (`A/F`), trims (`X`), pulls (`D`), replaces (`U`).
- **Counterparty** (stranger, exchange-matched): takes resting shares (`E/C`).
- **Exchange**: trades nothing, narrates everything. Every record is its handwriting.

## One order's life (the only story in the file, repeated millions of times)
Someone posts "buy 100 of A at $10.50" (stored as ticks: 105000 = 10.50 × 10000):
```
1. A  ref7  B  100sh @105000  → bid 10.50 shows 100  (birth: order rests)
2. E  ref7  30sh              → bid 10.50 shows 70   (bite: stranger sold 30 INTO the bid; 30 traded away, 70 rest)
3. X  ref7  20sh              → bid 10.50 shows 50   (trim: owner cancelled 20)
4. D  ref7                    → bid 10.50 gone       (death: rest pulled)
```
`A/F` = birth, `E/C` = bite taken, `X` = trim, `D` = death, `U` = reincarnation (new ref, loses queue spot). Follow-ups carry NO side byte — side is remembered from birth via `orders[ref]`.

## Bid/ask/levels (the two piles)
- **Bid** = buyers' best price+size ("buy 100 @ 10.50": hit sell and that's your fill). **Ask** = sellers' mirror.
- **Level** = one price row: all orders there summed. Three buys (100+50+25 @10.50) → `levels[105000] = {total 175, count 3}`. Screens show levels, never single orders.
- **Two maps, one money**: `orders[ref]` (by id — cancels find their order) + `levels[price]` (by price — quotes read totals). Every event updates both: `orders[7].qty -= 30; levels[105000] -= 30;` erase level at zero. No sorting (map holds order), no heap (need both extremes + arbitrary cancel).

## Ticks (why 105000)
Wire counts money in 1/10000ths of a dollar: $10.50 → 105000. Integers compare/hash exactly; floats wobble and ledgers can't. Divide by 10,000 only for display.

## Payload parameters in plain words (every record's envelope)
- **len (2B framing, e.g. `00 27` = 39)**: "the next 39 bytes belong together." File format, not trading — lets us split records without knowing any type. Read it, pull that many bytes, repeat.
- **type (byte 0, e.g. `52` = 'R')**: which verb this record is (birth/bite/trim/death/directory/...). Routes everything downstream: the book switches on this letter.
- **locate (bytes 1..2, e.g. `00 01` = 1)**: which stock, as the day's index number. `R` records publish the map (1 = "A", 2 = next...); every later message just says the number. 0 = belongs to no stock (system events).
- **tracking (bytes 3..4)**: Nasdaq's internal sequence stamp. Proof of order on their side; we count it, never branch on it.
- **timestamp (bytes 5..10, 6B)**: nanoseconds since midnight ET. When this happened — the basis for replay order, latency stats, and the T5 determinism hash.
- **Tail (byte 11 on)**: the verb's details, different per letter (order-ref/side/shares/price for `A`, symbol text for `R`, event code for `S`, ...). See `ITCH-SPEC.md` tables; `itch.*` decodes one layout per letter.
