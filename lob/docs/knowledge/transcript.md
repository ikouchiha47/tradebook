# Transcript — decoded lines straight from 07302019.NASDAQ_ITCH50.gz

Every line below is real bytes, fully decoded (not len/type summaries).
Read top to bottom: this is what "the file occurring" looks like.

## Opening bell — the phonebook (messages #1–6)
```
#1 S loc=0 ev=O
#2 R loc=1 sym=A    lot=100
#3 R loc=2 sym=AA   lot=100
#4 R loc=3 sym=AAAU lot=100
#5 R loc=4 sym=AABA lot=100
#6 R loc=5 sym=AAC  lot=100
```
`S/O` = start of messages, then one contact card per stock, ~8,849 of them,
then H/Y/L blocks (~230k reference lines total). Zero trading yet — the
exchange publishes the whole universe before anyone may quote.

## First order — #237170 (236k reference lines later)
```
#237170 A loc=494 ref=8617 side=B shares=600 price=$140.99 (sym ARGX)
```
The day's first posted order: buy 600 of ARGX @ $140.99. Locate 494 resolves
through card #498 (`R loc=494 sym=ARGX`), filed 236k lines earlier.

## First blood — executions on resting flow
```
#237486 E ref=854  shares=100 match=17697
#237722 E ref=539  shares=200 match=17698
#237741 E ref=539  shares=300 match=17699
```
Two strangers bite order 539 at different times (different matches);
each line shrinks one resting order and its price level.

## The rest of the alphabet — one real line each
```
F loc=8842 ref=6648 side=S shares=100 price=$1002.00 mpid=LEHM
```
Same birth shape as `A`, signed: Goldman (LEHM-slot attribution) offers 100.
Book effect identical to `A`.
```
X loc=1087 ref=24949 shares=200
D loc=8697 ref=432
```
Trim 200 off order 24949; pull order 432 entirely.
```
C loc=5022 ref=5304083 shares=100 match=37501 printable=N price=$6.59
```
Hidden fill: 100 shares trade at $6.59 but print `N` — kept off the public
tape. Book reduces all the same.
```
U loc=6612 old=951 new=8407 shares=300 price=$64.16
```
Replace: kill 951, file 8407 at the new price/qty, queue spot lost.
```
H loc=1 action=A
P loc=6104 price=$40.99
Q loc=1888
```
`H/A` = halt-state line for stock 1; `P`/`Q` = off-book and cross prints —
tape only, the book never sees them (hence no ref/shares that concern us).

## The pattern from here to close
`A/F` births interleave with `E/C` bites and `X/D` pulls, ~268M lines,
one symbol's story woven through thousands of others — separable only by
`locate`. The book replays exactly this transcript, in order, folding each
line into `orders` + `levels`.
