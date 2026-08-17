# Segment progress (linear Euclidean, fixed-point)

Straight-line moves must advance **linearly** along the segment: halfway in time ⇒ halfway in space. Implemented in `BeginMove` + `TickEngine::AdvanceMovement` via [`DetMath.h`](../SimRTS/Source/RTSEngine/Public/DetMath.h).

## Float goal (what we match)

$$
\text{distance} = \sqrt{dx^2 + dy^2},\quad
\text{traveled} = \text{speed} \cdot \frac{\text{elapsed}}{\text{tps}}
$$

$$
\text{position} = \text{start} + (dx, dy) \cdot \frac{\text{traveled}}{\text{distance}}
$$

then nearest-snap each axis to the integer grid.

## Why not put $dx^2+dy^2$ in the fraction

You **can** avoid a square root when only asking “have we arrived?”: compare `traveled²` to `dx²+dy²`.

You **cannot** use that trick for “where are we on the line?”. Halfway along a path of length 10 means the fraction walked is $0.5$, so the unit sits halfway between start and end.

If someone instead divides *squared* walked distance by *squared* path length:

$$
\frac{\text{traveled}^2}{\text{distance}^2} = 0.25 \neq 0.5
$$

Same numbers, wrong place on the segment (a quarter of the way in parameter space, not halfway). Squaring both sides of a length comparison is fine for arrival; using those squares as the interpolation weight is not.

So we take a fixed-point Euclidean length **once** per move (scale `dx`,`dy` then `isqrt`), then divide by that length every tick.

## Integer form (`SCALE = 1024`)

Scale exists so measurable points/ticks **survive intermediate division** without throwing away almost a whole point of remainder — widen into `× SCALE`, compute, snap back. See [fixed_point.md](fixed_point.md).

**On BeginMove**

```text
length_fp = isqrt((dx * SCALE)² + (dy * SCALE)²)
// equals floor(SCALE * sqrt(dx²+dy²)) — finer than isqrt(dx²+dy²)*SCALE
```

**Each tick**

```text
traveled_fp = speed * elapsed * SCALE / tps

if traveled_fp >= length_fp:
  position = end; clear move
else:
  ox = DivRoundNearest(dx * traveled_fp, length_fp)
  oy = DivRoundNearest(dy * traveled_fp, length_fp)
  position = start + (ox, oy)   // clamp to world
```

`DivRoundNearest`: `q = numer/denom`, `rem = numer%denom`; if `2*|rem| ≥ |denom|` bump `q` (nearest snap; e.g. `5+37/88→5`, `5+66/88→6`).

Heavy work once (`isqrt`); each tick is mul/div/mod only. No `double`, no per-tick `sqrt`.

## Files

- [`DetMath.h`](../SimRTS/Source/RTSEngine/Public/DetMath.h) / `DetMath.cpp` — `kMoveScale`, `IsqrtU64`, `DivRoundNearest`
- [`Unit.h`](../SimRTS/Source/RTSEngine/Public/Unit.h) — `UnitMove.length_fp`
- [`TickEngine.cpp`](../SimRTS/Source/RTSEngine/Private/TickEngine.cpp) — `BeginMove`, `AdvanceMovement`

See: [fixed_point.md](fixed_point.md), [integer_sqrt.md](integer_sqrt.md), [overflow.md](overflow.md).
