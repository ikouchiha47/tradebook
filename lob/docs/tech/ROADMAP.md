# Roadmap — P1..P4, feed to backtest

One system, four stages, each independently shippable. The point is coverage: it touches every
subfield these roles hire for — low-latency I/O, data structures, matching, market
microstructure, performance, quant.

```
P1  feed handler + order book      ITCH 5.0 replay -> ladder book            DONE
P2  matching engine                the venue: levels, FIFO, pool, matching   prd/P2.md · adr/0002
P3  market maker                   the trader: quote, skew, inventory        prd/P3.md
P4  backtest                       the verdict: fills, P&L, latency          prd/P4.md
```

## Why this shape

```
stage   the subfield it proves                       who leans in
P1      low-latency I/O, cache-aware data structures  systems
P2      matching, allocation, order lifecycle         systems / HFT
P3      market microstructure, quoting, risk          quant trading / MM desks
P4      backtesting, P&L, adverse selection           quant research / trading
```

The story a repo + README should tell: *"I built a feed handler, a tick-ladder book, a
matching engine, and a market maker; here are the ns numbers, the invariants, and the things I
got wrong."* The research-log style of the existing docs is a differentiator — keep it.

## Boundaries (keep the seams)

```
feed.*    bytes + framing          (P1)
book.*    price indexing + levels  (P1)
engine.*  orders + matching        (P2)  in-process library, no I/O
strat.*   decisions                (P3)  in-process, driven by engine callbacks
backtest  replay + metrics         (P4)  wires feed -> engine <-> strat
```
Each stage depends only on the one below it; `engine` knows nothing about `feed`; `strat` knows
nothing about ITCH wire format.
