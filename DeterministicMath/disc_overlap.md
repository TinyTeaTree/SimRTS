# Integer disc overlap (no floats)

Used by idle push and formation sparse goals: two units are discs centered on integer grid points. Decide overlap with **only integer multiply/add/compare** — no `sqrt`, no division, no floats.

## Geometric rule

Discs overlap when center distance is **strictly less** than the sum of radii:

$$
\sqrt{dx^2 + dy^2} < r_a + r_b
$$

With diameter $d$ in the same units as the grid (points):

$$
r_a + r_b = \frac{d_a + d_b}{2}
$$

So:

$$
\sqrt{dx^2 + dy^2} < \frac{d_a + d_b}{2}
$$

## Squaring both sides

Both sides are non-negative whenever the check is meaningful, so squaring preserves the inequality:

$$
dx^2 + dy^2 < \left(\frac{d_a + d_b}{2}\right)^2 = \frac{(d_a + d_b)^2}{4}
$$

Multiply through by 4 to clear the denominator:

$$
4(dx^2 + dy^2) < (d_a + d_b)^2
$$

## Code form

```cpp
bool DiscsOverlap(Vec2i a, int32_t diameter_a, Vec2i b, int32_t diameter_b) {
    const int32_t dx = a.x - b.x;
    const int32_t dy = a.y - b.y;
    const int32_t min_sep = diameter_a + diameter_b;
    return 4 * (dx * dx + dy * dy) < min_sep * min_sep;
}
```

Equivalent one-liner:

```text
4 * (dx² + dy²) < (diameter_a + diameter_b)²
```

## Notes

- **Strict `<`**: discs that only touch at the sum of radii are **not** overlapping. That keeps “just touching” legal for sparse slots and settled crowds.
- **Same cell** (`dx = dy = 0`): always overlaps for positive diameters.
- **Overflow**: for large worlds, prefer `int64_t` intermediates for `dx*dx` and `min_sep*min_sep` if diameters or coordinates can get big.
- **Why not compare to squared radii with floats?** Even `float` distance or `sqrt` can disagree across platforms near the boundary; this form is bit-identical on every peer.

## Where used

- `DiscsOverlap` in `TickEngine.cpp` (idle push pairs, sparse `FitsAgainstPlaced`)
