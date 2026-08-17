# Overflow checklist

- Grid up to ~1000, `dx*dx + dy*dy` fits in 32-bit; still use **64-bit** for products with `speed`, `tick`, and `SCALE`
- Length fixed-point: `(dx * SCALE)² + (dy * SCALE)²` must fit in `uint64` before `isqrt` (with `SCALE = 1024` and |dx|,|dy| ≲ a few million this is fine; raise care if SCALE or map size grows)
- Never multiply three large ints in `int32_t`
- Document max tick, max speed, max SCALE so `speed * elapsed * SCALE` cannot overflow `int64`
- Disc overlap: prefer `int64_t` for `dx*dx` and `(diameter_a + diameter_b)²` if coords or diameters grow ([disc_overlap.md](disc_overlap.md))

See: [fixed_point.md](fixed_point.md), [segment_progress.md](segment_progress.md).
