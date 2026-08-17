#pragma once

#include "Types.h"

namespace SimRTS {

// Static battle data: definition for a unit type.
struct UnitDef {
    UnitType type = UnitType::Soldier;
    int32_t speed = 1;     // discrete points per second (min 1)
    int32_t diameter = 1;  // disc diameter in points (1 point = 1 dm); path clearance
    // Idle-push mass: lighter units take more of each separation share (min 1).
    int32_t weight = 1;
    // Group-move ring slots around the click (default on).
    bool sparse_goals = true;
    // Soft separation when idle and overlapping another unit (default on).
    bool idle_push = true;
};

} // namespace SimRTS
