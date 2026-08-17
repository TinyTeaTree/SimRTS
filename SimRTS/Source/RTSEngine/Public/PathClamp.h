#pragma once

#include "BattleState.h"
#include "Types.h"

namespace SimRTS {

// Walk the discrete cells of the Euclidean segment start→end (same snap-to-nearest
// model as UnitMove), independent of unit speed. Returns the farthest cell on that
// path where a disc of `diameter` points still fits.
// If the path is clear, returns end. If the next cell after start is blocked for
// that diameter, returns start.
Vec2i ClampMoveDestination(Vec2i start, Vec2i end, const PathingGrid& pathing, int32_t diameter);

} // namespace SimRTS
