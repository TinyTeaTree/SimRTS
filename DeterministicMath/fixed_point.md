# Fixed-point (SCALE so measurable values survive division)

For a new author: this note is only about **why we multiply by a constant before dividing**, and how we get back to grid points afterward. How a unit walks a line is [segment_progress.md](segment_progress.md). How we take $\sqrt{\cdot}$ in integers is [integer_sqrt.md](integer_sqrt.md).

## The problem

Gameplay values we care about are **measurable integers**: points on the grid, ticks, diameters, weights.

Many formulas still need a **fraction** in the middle of the calculation, for example:

- how far a unit moved this tick: `speed * elapsed_ticks / ticks_per_second`
- how to split a push between two units: `weight_a / (weight_a + weight_b)`

In integer math, `/` keeps the quotient and **throws away the remainder**. If that happens in the middle of a multi-step formula, you can lose almost a whole point of meaning before the result is ever written to `position`.

## The idea

1. Multiply the measurable value by a constant **SCALE** (we use a **power of 2**, currently `1024`).
2. Do the mul/div work in that wider space. Remainders become low bits instead of disappearing.
3. When you need a real grid quantity again, divide by SCALE (or nearest-snap using the leftover from `%`) so you return to the **same** measurable unit you started with.

```text
measurable value          work in finer units              measurable again
(points, ticks, …)  →  × SCALE  →  compute  →  ÷ SCALE / snap  →  (points, …)
```

SCALE is not a new game unit you design levels in. It is a temporary wider ruler so intermediate division does not erase fractions.

**Why a power of 2?** It matches binary integers well; multiplying or dividing by SCALE can be a shift when you want. Any positive SCALE works; `1024` gives about 10 bits of fraction.

## What SCALE is not for

SCALE does **not** choose the formula. If the formula is wrong, more fractional bits will not make the answer right. (Straight-line movement still needs path **length** $\sqrt{dx^2+dy^2}$, not $dx^2+dy^2$ — see [segment_progress.md](segment_progress.md).)

**Widen before you lose bits.** For path length, multiply `dx` and `dy` by SCALE **before** `isqrt`, not after:

$$
\text{SCALE}\cdot\sqrt{dx^2+dy^2}
= \sqrt{(dx\cdot\text{SCALE})^2 + (dy\cdot\text{SCALE})^2}
$$

`isqrt(dx²+dy²) * SCALE` floors to a whole point first, then pads zeros — the fractional length is already gone. `isqrt((dx·SCALE)² + (dy·SCALE)²)` keeps that fraction in the low bits of `length_fp`.

## In this codebase

| Role | Constant | Value | Defined in |
|------|----------|-------|------------|
| Move length and distance walked | `kMoveScale` | 1024 | `DetMath.h` |
| Idle-push pressure accumulator | `kPushPressureScale` | 1024 | `TickEngine.cpp` |

Use `int64_t` for any product that mixes `speed`, elapsed ticks, `SCALE`, and lengths — see [overflow.md](overflow.md).

## Operations (one rounding rule, used everywhere)

| Step | Pattern |
|------|---------|
| Enter fixed space | `value * SCALE` |
| Multiply two fixed values | `(a * b) / SCALE` |
| Divide in fixed space | `(a * SCALE) / b` |
| Leave fixed space (nearest) | `DivRoundNearest(numer, denom)`: quotient `q`, remainder `r`; if `2 * \|r\| ≥ \|denom\|` then bump `q` |

If we ever need finer intermediates, raise SCALE (e.g. to `65536`) in one place and keep the same pattern.

## Where movement uses this

1. At move start: `length_fp = isqrt((dx·SCALE)² + (dy·SCALE)²)` (scale **before** the root)
2. Each tick: `traveled_fp = speed * elapsed * SCALE / ticks_per_second`
3. Place the unit with a linear fraction in fixed space, then snap each axis back to an integer point

Step-by-step: [segment_progress.md](segment_progress.md).
