# Lifecycle — what each letter means and what the book does with it

One order's full vocabulary. Read a record as: WHO acted on WHICH order,
and the book folds it the same way every time.

## Reference (before any trading)
- `S` — exchange timetable bell (`O` start, `Q` open, `M` close...).
  Book: nothing. Count only. Ex: dump #0 `S loc0 evO`.
- `R` — contact card: `494 = ARGX`, lot 100, tier flags.
  Book: nothing. `main` files `sym[494] = "ARGX"`. Ex: dump #498.
- `H/Y/L/...` — halt states, SHO flags, MPID positions (~230k lines of morning
  phonebook). Book: nothing. Count only.

## Births (orders appear)
- `A` — anonymous post: side/shares/price/ref minted.
  Book: `add()` — file order, grow level. Ex: #237170 buy 600 ARGX @ $140.99.
- `F` — same as `A`, signed with MPID (`LEHM`...). Book: identical `add()`.
  The signature is attribution, not state.

## Bites (shares leave while the order lives)
- `E` — visible fill: `ref` lost `shares` to a taker.
  Book: `reduce()` both maps. Ex: ref 539 −200 (match 17698), later −300 (17699):
  two takers, two times, one shrinking order.
- `C` — hidden fill with its own price (`printable N` = kept off the public tape).
  Book: same `reduce()`; tape keeps the price.

## Trims and deaths (owner pulls back)
- `X` — partial cancel: `ref` gives back `shares`. Book: `reduce()`.
- `D` — full pull: `ref` gone. Book: `remove()`.
- `U` — replace: kill `old_ref`, file `new_ref` at new price/qty, queue spot lost.
  Book: atomic `remove` + `add`. Only two-id letter.

## Prints (money moved, book untouched)
- `P/Q/B` — tape lines for off-book and cross prints. Book: never touched.
  Counted for the tape only. (`B` = a print later busted — back-office undoes it.)

## Reading any record in one line
`len` (how much) → `type` (which verb above) → `locate` (whose book) →
fields (who/how-many) → fold into `orders` + `levels`, or count and move on.
Unknown ref on a mutating verb: counter, skip, keep folding — the shares were
never resting, so there is no level to touch (see robustness.md).
