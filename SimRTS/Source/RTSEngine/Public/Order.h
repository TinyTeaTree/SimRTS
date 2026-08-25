#pragma once

#include "Types.h"

#include <vector>

namespace SimRTS {

// Live battle data: pending order consumed by the tick engine.
struct Order {
    PlayerId player_id = 0;
    std::vector<UnitId> unit_ids;
    OrderType type = OrderType::Move;
    Vec2i target; // Move destination in discrete world points
    // false: immediate replace (clears active move + queued is_next for these units).
    // true: run after the unit's current move finishes (shift-click / waypoint chain).
    bool is_next = false;
};

} // namespace SimRTS
