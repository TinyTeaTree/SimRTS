# Integer square root

Euclidean segment length needs `√(dx² + dy²)`. Do **not** call `std::sqrt` for lockstep.

Use an **integer square root**: largest integer `r` such that `r * r ≤ n`.

Implemented as `IsqrtU64` in [`DetMath.cpp`](../SimRTS/Source/RTSEngine/Private/DetMath.cpp). Called **once** in `BeginMove`.

## Scale before the root (movement)

To keep fractional points in `length_fp`, multiply coordinates by `SCALE` **first**:

$$
\text{length\_fp}
= \mathrm{isqrt}\big((dx\cdot\text{SCALE})^2 + (dy\cdot\text{SCALE})^2\big)
\approx \lfloor \text{SCALE}\cdot\sqrt{dx^2+dy^2} \rfloor
$$

Do **not** use `isqrt(dx²+dy²) * SCALE`: that floors to a whole point, then pads zeros — the fraction is already lost. See [fixed_point.md](fixed_point.md).

Watch overflow: `(dx·SCALE)² + (dy·SCALE)²` must fit in `uint64` ([overflow.md](overflow.md)).

## Sketch (binary search — clear and portable)

```cpp
// Returns floor(sqrt(n)) for n >= 0.
uint64_t IsqrtU64(uint64_t n) {
    if (n == 0) return 0;
    uint64_t lo = 0;
    uint64_t hi = 1ull << 32;
    while (lo + 1 < hi) {
        uint64_t mid = lo + (hi - lo) / 2;
        if (mid <= n / mid) lo = mid;  // mid*mid <= n, overflow-safe
        else hi = mid;
    }
    return lo;
}
```

## Why we need it for linear moves

`distance² = dx² + dy²` is enough to test **arrival** (`traveled² ≥ distance²`).

Linear **position** needs the real length in the denominator:

$$
\text{offset} = \Delta \cdot \frac{\text{traveled}}{\text{distance}}
$$

not `traveled² / distance²`. See [segment_progress.md](segment_progress.md).

Slight quantization vs real `√` is OK if **identical on all peers**.

## Avoiding sqrt entirely (different metrics)

| Approach | Pros | Cons |
|----------|------|------|
| Compare with **distance²** for arrival only | no sqrt | still need length for lerp |
| **Octile / Chebyshev / Manhattan** length | integer, no isqrt | not Euclidean travel time |
| Lookup table for √ of small ints | fast, deterministic | limited range |

For true straight-line Euclidean, **integer sqrt once per move** (with SCALE applied to `dx`,`dy` first) is the approach we use.

Related: [disc_overlap.md](disc_overlap.md) uses distance² (×4) so overlap never needs sqrt.
