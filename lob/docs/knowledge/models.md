# Models — every struct, field by field

Where each field's value comes from and what unit it is in. Wire offsets refer to
ITCH payload bytes (see ITCH-SPEC.md); "derived" means computed by our code.

## Parsed structs (include/itch.h)

### Header (11B common, every message)
| field | type | wire | meaning |
|-------|------|------|---------|
| type | char | [0] | message letter (routes the switch) |
| locate | uint16 | [1..2] | stock index for the day; 0 = global |
| tracking | uint16 | [3..4] | Nasdaq internal sequence; counted, never branched on |
| ts48 | uint64 | [5..10] | 6B BE nanoseconds since midnight ET |

### StockDir (from R)
| field | type | wire | meaning |
|-------|------|------|---------|
| locate | uint16 | [1..2] | index this card defines |
| symbol | string | [11..18] | 8B space-padded ticker, trimmed ("ARGX") |

### AddOrder (from A/F) — a birth
| field | type | wire | meaning |
|-------|------|------|---------|
| ref | uint64 | [11..18] | order id minted by this message |
| price_ticks | uint64 | [32..35] | limit price ×10,000 (int64-ready; ticks, never dollars) |
| mpid | uint32 | [36..39] | F only: 4B firm tag ("LEHM"); 0 for A |
| shares | uint32 | [20..23] | resting quantity |
| locate | uint16 | [1..2] | which stock's book |
| side | char | [19] | 'B' buy / 'S' sell |

### Cancel (from X)
| field | type | wire | meaning |
|-------|------|------|---------|
| ref | uint64 | [11..18] | which order loses shares |
| shares | uint32 | [19..22] | how many cancelled (partial) |

### Delete (from D)
| field | type | wire | meaning |
|-------|------|------|---------|
| ref | uint64 | [11..18] | order fully removed |

### Exec (from E)
| field | type | wire | meaning |
|-------|------|------|---------|
| ref | uint64 | [11..18] | order that traded |
| match | uint64 | [23..30] | execution id (tape grouping; book ignores) |
| shares | uint32 | [19..22] | shares executed |

### ExecPx (from C) — hidden/late priced fill
| field | type | wire | meaning |
|-------|------|------|---------|
| ref | uint64 | [11..18] | order that traded |
| shares | uint32 | [19..22] | shares executed |
| match | uint64 | [23..30] | execution id |
| printable | char | [31] | 'Y' public tape / 'N' hidden |
| price | int64 | [32..35] | execution price in ticks (may differ from limit) |

### Replace (from U) — atomic kill + rebirth
| field | type | wire | meaning |
|-------|------|------|---------|
| old_ref | uint64 | [11..18] | order being removed |
| new_ref | uint64 | [19..26] | order being created (new id, queue spot lost) |
| shares | uint32 | [27..30] | new resting quantity |
| price | int64 | [31..34] | new limit price in ticks |

## Book structs (include/book.h)

### Order — one resting order (values copied from AddOrder)
| field | type | source | meaning |
|-------|------|--------|---------|
| ref | uint64 | AddOrder.ref | identity, key of orders map |
| price | int64 | cast(AddOrder.price_ticks) | tick price (map key in bids/asks) |
| qty | uint32 | AddOrder.shares | remaining resting quantity |
| side | char | AddOrder.side | 'B'/'S' — selects which level map |
Note: `locate` and `mpid` are dropped — routing and attribution, not resting state.

### Level — storage at one price (map value)
| field | type | meaning |
|-------|------|---------|
| total | uint32 | sum of qty of all orders at this price |
| count | uint32 | number of live orders at this price (row dies at 0) |

### LevelRow — one depth row for readers
| field | type | meaning |
|-------|------|---------|
| price | int64 | the row's price (carried because a vector has no map key) |
| total | uint32 | copied from Level.total |
| count | uint32 | copied from Level.count |

### Depth — snapshot returned by depth(n)
| field | type | meaning |
|-------|------|---------|
| bids | vector<LevelRow> | top n buy rows, best first |
| asks | vector<LevelRow> | top n sell rows, best first |

### TopOfBook — snapshot returned by best()
| field | type | meaning |
|-------|------|---------|
| bid_px | int64 | best (highest) bid price; 0 = none |
| ask_px | int64 | best (lowest) ask price; 0 = none |
| bid_sz | uint32 | size at best bid |
| ask_sz | uint32 | size at best ask |

### Counters — telemetry on dirty input
| field | type | meaning |
|-------|------|---------|
| unknown_ref | uint64 | E/X/D/U hit an id never filed (hidden flow, late A) |
| clamped | uint64 | a reduce asked for more than resting; floored at 0 |
| crossed | uint64 | bid >= ask seen transiently; counted, not halted |

### NasdaqBook (storage root)
| field | type | meaning |
|-------|------|---------|
| orders | unordered_map<uint64,Order> | ref → live order (for cancels) |
| bids | map<int64,Level> | buy rows, ordered by price |
| asks | map<int64,Level> | sell rows, ordered by price |
| cached | TopOfBook | best bid/ask, restamped per mutation (best() is O(1)) |
| stats | Counters | dirty-input counters |

### NasdaqBookParser — the public facade
Holds one `NasdaqBook`. `add/reduce/remove/replace` mutate; `best/depth` read (const).
Note: struct is named `NasdqBook` in book.h (typo — missing 'a'); rename to `NasdaqBook`
next time you touch it.
