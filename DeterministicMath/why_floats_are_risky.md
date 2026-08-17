# Why floats are risky

IEEE-754 `float` / `double` are **not** reliably deterministic across all machines for game lockstep, especially with:

- `std::sqrt`, trig, and libm helpers
- multiply + add (FMA vs separate mul/add)
- different CPUs (e.g. ARM vs x86), compilers, and optimization flags (`-ffast-math`)

Casting small integers to `double` is usually exact, but **`sqrt` / division / rounding** can put two peers on different sides of a `.5` boundary before `llround`, so snapped grid cells diverge.

Snapping to `int` reduces visible error but does **not** guarantee the same snap everywhere.

See also: [disc_overlap.md](disc_overlap.md) for a comparison that avoids distance floats entirely.
