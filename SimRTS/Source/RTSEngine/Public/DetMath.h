#pragma once

#include <cstdint>

namespace SimRTS {

// Power-of-2 fixed-point scale for movement (1 grid point = kMoveScale).
constexpr int32_t kMoveScale = 1024;

// Floor(sqrt(n)) for n >= 0. Deterministic; no libm.
uint64_t IsqrtU64(uint64_t n);

// Nearest integer to numer/denom: q = numer/denom, rem = numer%denom;
// bump q when 2*|rem| >= |denom| (ties away from zero). denom != 0.
int64_t DivRoundNearest(int64_t numer, int64_t denom);

} // namespace SimRTS
