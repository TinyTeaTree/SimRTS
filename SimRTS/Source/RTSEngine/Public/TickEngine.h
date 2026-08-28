#pragma once

#include "RTSEngineAPI.h"
#include "BattleState.h"

#include <cstdint>
#include <vector>

namespace SimRTS {

// Deterministic tick engine: apply orders, advance movement, increment tick.
class RTSENGINE_API TickEngine {
public:
    void LoadLevel(const Level& level);

    // Queue a move; pathfinds into straight-line waypoint segments.
    // Multi-unit moves: units with UnitDef.sparse_goals expand from the click along
    // formation rays (selection COM → unit), with pairwise diameter spacing.
    // Idle units with idle_push accumulate weighted separation pressure each tick.
    // Regular (is_next=false): clears that unit's active move + pending orders, then plans.
    // is_next=true: plans from the end of that unit's current queue / active move.
    void SubmitOrder(Order order);

    // Cache a raw order until scheduled_tick; pathfind happens then. Dedupe (player_id, order_id).
    void SubmitScheduled(Order order, uint32_t order_id, Tick scheduled_tick);

    void StepForward();

    Tick GetTick() const { return tick_; }
    uint64_t GameplayHash() const;
    const StaticBattleData& GetStaticData() const { return static_data_; }
    const BattleState& GetState() const { return state_; }

    const Unit* FindUnit(UnitId id) const;
    Unit* FindUnit(UnitId id);

private:
    struct ScheduledCommand {
        Order order;
        uint32_t order_id = 0;
        Tick scheduled_tick = 0;
    };

    void ActivateScheduled();
    void ApplyOrders();
    void AdvanceMovement();
    void ApplyIdlePush();

    StaticBattleData static_data_;
    BattleState state_;
    std::vector<ScheduledCommand> scheduled_;
    Tick tick_ = 0;
};

} // namespace SimRTS
