#pragma once

#include "BattleState.h"
#include "Types.h"

#include <vector>

namespace SimRTS {

struct PathFindResult {
    // Destinations only (start excluded). Empty if already there / no progress.
    std::vector<Vec2i> waypoints;
    bool reached_requested = false;
};

// True when the Euclidean lerp+round segment keeps the disc of `diameter` clear.
bool HasLineOfSight(Vec2i start, Vec2i end, const PathingGrid& pathing, int32_t diameter);

// LOS shortcut, else A* (8-neighbor) + LOS-safe waypoint compression.
// Unreachable requested goals fall back to the closest cell that fits `diameter`.
PathFindResult FindMoveWaypoints(
    Vec2i start,
    Vec2i requested_goal,
    const PathingGrid& pathing,
    int32_t diameter);

} // namespace SimRTS
