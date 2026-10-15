# Order types — the taxonomy

An order is not one type. It is a set of **orthogonal facets**, and most of the confusing
"order type" names are one facet or a small combination. This is the reference we design the
engine's order model against.

## The facets

```
side        buy | sell
price       limit (explicit price) | market (none) | pegged (derived)
qty         total | displayed (iceberg) | minimum
time        GTC | Day | GTD/GTT | IOC | FOK            ← time-in-force
trigger     none | stop / stop-limit / take-profit / MIT / LIT   ← dormant until a price prints
display     shown | hidden | post-only
constraint  none | AON / min-qty | self-trade-prevention | reduce-only
```

An order = a choice in each facet. `IOC` is a *time* choice; `iceberg` is a *display* choice;
`stop` is a *trigger*. They compose: e.g. "iceberg stop-limit, GTC".

## Core types (price behaviour)

| type | rests? | behaviour |
|---|---|---|
| **Limit** | yes | rests if it doesn't cross; matches if it does at the maker's price |
| **Market** | no | takes best-first until filled or liquidity ends; may partially fill |
| **Stop** (stop-market) | no (dormant) | sleeps until the market trades at the trigger, then becomes a market order |
| **Stop-Limit** | yes (dormant) | on trigger, becomes a limit at its limit price |
| **Take-Profit / -Limit** | dormant | mirror of stop, used to exit in profit |
| **Trailing Stop(-Limit)** | dormant | a stop whose trigger follows the market by a fixed distance |
| **Market-If-Touched (MIT)** | dormant | buy when price rises to the trigger, sell when it falls (inverse of stop) |
| **Limit-If-Touched (LIT)** | dormant | MIT that becomes a limit |
| **Market-to-Limit (MTL)** | partially | market that converts to a limit at the first execution price |

## Time-in-force (TIF)

Decides what happens to the **unfilled remainder**. Orthogonal to the type above.

| TIF | meaning |
|---|---|
| **Day** | rests until the close (default for market orders on equities) |
| **GTC** | good-till-cancel — rests until filled or cancelled |
| **GTD / GTT** | good-till-date / -time |
| **IOC** | immediate-or-cancel — fill what you can now, cancel the rest |
| **FOK** | fill-or-kill — all-or-nothing, immediately |
| **OPG** | at-the-open (auction) |
| **CLO / CLS** | at-the-close (auction) |

`FOK = IOC + AON` (all-or-none). `IOC`/`FOK` are near-free once the match loop exists — they
only change the *remainder* handling.

## Auction (a batch mode, not a continuous order)

| type | meaning |
|---|---|
| **MOO / MOC** | market-on-open / market-on-close |
| **LOO / LOC** | limit-on-open / limit-on-close |
| **IO** | imbalance-only |

Matched **once** at a single clearing price, not continuously. A different matching mode.

## Display / behaviour

| type | meaning |
|---|---|
| **Hidden / non-displayed** | dark liquidity; not shown in the book |
| **Iceberg / Reserve** | total > displayed; replenishes displayed qty as it fills |
| **Disclosed-qty** | explicit displayed amount |
| **Post-Only** | add liquidity; cancel if it would take |
| **Reduce-Only** | (derivatives) may only reduce an existing position |

## Pegged / derived price

| type | meaning |
|---|---|
| **Pegged** | price derived from primary / mid / bid / ask / last |
| **Discretionary** | a displayed price plus a hidden discretionary price |

## Constraints (modifiers, not types)

| constraint | meaning |
|---|---|
| **AON** | all-or-none; reject partial fills |
| **min-qty** | minimum acceptable fill |
| **STP** | self-trade prevention: cancel-new / cancel-old / decrement |
| **reduce-only** | only reduces exposure |

## Combinations — the OMS layer, not the venue

| combo | meaning |
|---|---|
| **OCO** | one-cancels-other |
| **OTO** | one-triggers-other |
| **Bracket** | entry + stop + take-profit as one unit |
| **OCA / trailing-OCO** | variants |
| **SWEEP / IOC-across-venues** | routing across venues |

These are built *from* venue orders; most venues don't implement them.

## Where each lives

```
matching engine (venue)   limit · market · stop/stop-limit · trailing · take-profit · MIT/LIT
                          × TIF (Day/GTC/IOC/FOK/GTD/OPG/CLO)
                          × display (iceberg/hidden/post-only)
                          × constraints (AON/min-qty/STP)
auction engine            MOO/MOC · LOO/LOC
OMS / algo layer          OCO · OTO · bracket · trailing-OCO · TWAP/VWAP
```

## Two things that clear up most confusion

1. **TIF is orthogonal to type.** The match loop is identical; TIF only decides the
   remainder → so `IOC`/`FOK` cost almost nothing once the loop works.
2. **Stop is a second subsystem.** It needs a trigger engine watching prices and injecting
   orders — a separate component, not a variant of the match loop.

## Build order (for our engine)

```
1. Limit (GTC)         foundation + rest/book
2. Market              same loop, no rest
3. Cancel / Replace    order identity + O(1) unlink
4. IOC / FOK           TIF on the remainder
5. Post-Only, Iceberg  display/budget behaviour
defer: Stop/Trigger, Auction, Pegged, OMS combos
```

For a single-symbol portfolio engine the honest core is **1–4**: it exercises price-time
priority, partial fills, the book, and O(1) cancel. See `lifecycle.md` (message lifecycle) and
`models.md` (field tables) for how this maps to wire messages.
