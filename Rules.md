# Conventions

## Architecture

- Keep **RTSEngine** pure C++ (STL / data-only). No Unreal types, UObjects, or gameplay APIs in sim sources.
- Keep **RTSComms** pure C++ (STL + private TCP shim). No Unreal types in comms sources. Public API never blocks.
- Keep **SimRTS** as the Unreal layer: display, input, HUD, JSON I/O, and bridge into RTSEngine.
- **SimRTS is block-free:** never wait on HTTP/sockets/RTSComms I/O on the game thread. Enqueue + `TryPop` only.

## Casts — avoid noise

Readability and reading speed come first. Do not use `static_cast` (or other casts) when they don’t change behavior and only show type-system ceremony.

- Prefer `cells[y][x]` over `cells[static_cast<size_t>(y)][static_cast<size_t>(x)]` when indexing `std::vector` with non-negative `int32_t` coordinates already validated by `InBounds` (or equivalent). Implicit `int32_t` → `size_t` is fine there.
- Keep casts when they matter: narrowing that can truncate, signed/unsigned cases that change meaning (e.g. negative → huge `size_t`), casting through unrelated types, or silencing a real hazard.
- Default: write the obvious code. Add a cast only when it prevents a real bug or documents a non-obvious conversion.
