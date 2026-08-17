#pragma once

#include "BattleState.h"

namespace SimRTS {

// Fills GridCell::obstruction_distance for every cell: integer floor of the
// Euclidean distance in points (1 point = 1 dm) to the nearest blocked cell.
// Floor so FitsDiameter with 2 * obstruction_distance >= diameter is safe.
// Blocked cells get 0. Open maps (no blocked cells) get a large sentinel.
// O(width * height) separable EDT — cheap enough to run in-engine on load (no bake).
// Call once after the level's blocked flags are loaded (TickEngine::LoadLevel).
void ComputeObstructionDistances(PathingGrid& pathing);

// True when a disc of the given diameter (points) can be centered on (x, y).
// Uses integer half-size: 2 * obstruction_distance >= diameter.
bool FitsDiameter(const PathingGrid& pathing, int32_t x, int32_t y, int32_t diameter);

} // namespace SimRTS
