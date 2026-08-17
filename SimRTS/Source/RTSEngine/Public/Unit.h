#pragma once

#include "Types.h"

namespace SimRTS {

// Active move kept on the unit (straight line A → B in discrete space).
struct UnitMove {
    bool active = false;
    Vec2i start;
    Vec2i end;
    Tick start_tick = 0;
    // Euclidean length in points × kMoveScale (isqrt once at BeginMove).
    int64_t length_fp = 0;
};

// Live battle data: one unit instance.
struct Unit {
    UnitId id = 0;
    UnitType type = UnitType::Soldier;
    Vec2i position;
    int32_t rotation = 0; // yaw degrees [0, 360); faces move direction while traveling
    UnitMove move;
    // Idle-push residual (fixed-point; full scale == 1 grid point). Cleared on BeginMove.
    // |pressure| is clamped to one point so fractional shares can carry across ticks.
    int32_t push_pressure_x = 0;
    int32_t push_pressure_y = 0;
};

} // namespace SimRTS
