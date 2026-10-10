# Tick-ladder vs map — parameterized sweep (N levels × gap)

`lob_bench --benchmark_filter=BM_Sweep`. Machine: Apple M4, Release. `ns/op`.
`arg0 = levels per side`, `arg1 = tick gap between levels` (100 = dense/consecutive;
1000/5000 = sparse). All numbers are **add/remove of a NEW level** unless marked depth.

## add/remove a new level (create + destroy) — ladder wins, gap widens with N

| N levels/side | map | ladder | ratio |
|---|---|---|---|
| 100 | 43.7 | **30.9** | 1.4× |
| 1000 | 54.9 | **32.2** | 1.7× |
| 10000 | 71.4 | **32.5** | 2.2× |

(gap 100; gap 1000 and 5000 give the same shape.)
```
map    :  44 → 55 → 71 ns   (grows with N: O(log N) tree + node alloc + cache misses)
ladder :  31 → 32 → 33 ns   (flat: O(1) index + one bitmap bit)
```
The map's level create/destroy cost climbs with size; the ladder's is **scale-invariant**.
Note: this is create/destroy (a tree `malloc`/`free` vs a bit flip). Adding to an
*existing* level is a different, near-tie case (map ~15 ns vs ladder ~16 ns).

## depth(5) — after the occupied→occupied jump: ladder flat in N and gap

`top()` now emits the best level, then jumps straight to the next occupied slot via
`Occupancy::prev_occupied`/`next_occupied` — no gap-walk.

| gap | map (N=100→10000) | ladder (N=100→10000) | winner |
|---|---|---|---|
| 100 (dense) | 45.0 → 52.5 | 46.6 → 44.2 | tie / ladder |
| 1000 (10-slot gap) | 47.6 → 58.1 | **41.3 → 41.7** | ladder |
| 5000 (50-slot gap) | 44.3 → 54.2 | **44.5 → 46.6** | ladder |

```
map    :  44 → 58 ns   (tree walk driven by N, not gap)
ladder :  ~40–47 ns    (flat in BOTH N and gap)
```
Before the jump (gap-walk), the ladder was `dense 39 · sparse 57 · very-sparse 205`.
The `205` at gap 5000 is gone — depth is now **~40–47 ns at every N and gap**, while
the map still climbs with N (44 → 58).

## Verdict
```
op                | dense          | sparse
add/remove level  | ladder (1.4×)  | ladder (2.2×)   ← scale-invariant win
depth(5)          | ladder / tie   | ladder           ← gap-walk fixed
```
- **Structural ops (add/remove):** the ladder wins at all N and all densities — the
  advantage *grows* with size, not a density crossover.
- **depth:** after `top()` jumps occupied→occupied via `prev_occupied`/`next_occupied`,
  the ladder is flat in N and gap and wins or ties everywhere; the map's tree walk still
  grows with N.
- Net: the former density crossover for depth is **gone** — both ops now favor the ladder.
