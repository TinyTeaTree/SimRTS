// Standalone smoke test for the pure C++ RTSEngine core (no Unreal / no JSON).
//
// From repo root (Demo/):
//   clang++ -std=c++17 -DRTSENGINE_STANDALONE \
//     -ISimRTS/Source/RTSEngine/Public \
//     tools/simrts_smoke_test.cpp \
//     SimRTS/Source/RTSEngine/Private/TickEngine.cpp \
//     SimRTS/Source/RTSEngine/Private/PathClamp.cpp \
//     SimRTS/Source/RTSEngine/Private/PathFind.cpp \
//     SimRTS/Source/RTSEngine/Private/PathingClearance.cpp \
//     SimRTS/Source/RTSEngine/Private/DetMath.cpp \
//     -o tools/simrts_smoke_test
//   ./tools/simrts_smoke_test

#include "DetMath.h"
#include "PathClamp.h"
#include "PathFind.h"
#include "PathingClearance.h"
#include "TickEngine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace SimRTS;

static void Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

static Level MakeSmokeLevel() {
    Level level;
    level.static_data.world_width = 1000;
    level.static_data.world_height = 1000;
    level.static_data.ticks_per_second = 10;
    level.static_data.pathing.ResizeOpen(1000, 1000);

    UnitDef soldier;
    soldier.type = UnitType::Soldier;
    soldier.speed = 10; // 10 points/s → 1 point per tick at 10 Hz
    soldier.diameter = 1;
    soldier.weight = 1;
    soldier.sparse_goals = true;
    soldier.idle_push = false;
    level.static_data.unit_defs[static_cast<size_t>(UnitType::Soldier)] = soldier;

    UnitDef vehicle;
    vehicle.type = UnitType::Vehicle;
    vehicle.speed = 20;
    vehicle.diameter = 2;
    vehicle.weight = 100;
    vehicle.sparse_goals = true;
    vehicle.idle_push = false;
    level.static_data.unit_defs[static_cast<size_t>(UnitType::Vehicle)] = vehicle;

    level.spawns.push_back({1, UnitType::Soldier, {10, 10}, 0});
    level.spawns.push_back({2, UnitType::Vehicle, {20, 10}, 0});
    return level;
}

static void StepUntilIdle(TickEngine& engine, UnitId id, int max_steps) {
    for (int i = 0; i < max_steps; ++i) {
        if (const Unit* unit = engine.FindUnit(id); unit != nullptr && !unit->move.active) {
            if (engine.GetState().orders.empty()) {
                return;
            }
        }
        engine.StepForward();
    }
}

int main() {
    TickEngine engine;
    engine.LoadLevel(MakeSmokeLevel());

    Order move;
    move.type = OrderType::Move;
    move.unit_ids = {1};
    move.target = {20, 10};
    engine.SubmitOrder(std::move(move));

    // Tick 0: order applied, elapsed 0 → still at start, move active on unit.
    engine.StepForward();
    const Unit* soldier = engine.FindUnit(1);
    if (soldier->position.x != 10 || !soldier->move.active) {
        Fail("move should start at A with active UnitMove");
    }

    // Axis move: 10 points at 10 pts/s, 10 Hz → ~1 point per tick.
    engine.StepForward();
    soldier = engine.FindUnit(1);
    if (soldier->position.x != 11) {
        Fail("soldier should advance ~1 point after one tick of travel");
    }

    for (int i = 0; i < 20 && engine.FindUnit(1)->move.active; ++i) {
        engine.StepForward();
    }
    soldier = engine.FindUnit(1);
    if (soldier->position.x != 20 || soldier->position.y != 10 || soldier->move.active) {
        Fail("soldier should arrive at axis target and clear move");
    }

    // Diagonal: length_fp ≈ SCALE*sqrt(200); ~14.14 points at 1 pt/tick.
    Order diag;
    diag.type = OrderType::Move;
    diag.unit_ids = {1};
    diag.target = {30, 20};
    engine.SubmitOrder(std::move(diag));
    engine.StepForward(); // apply at (20,10); elapsed 0 → still at start

    const int64_t scale = kMoveScale;
    const int64_t length_fp = static_cast<int64_t>(
        IsqrtU64(static_cast<uint64_t>(10 * scale * 10 * scale + 10 * scale * 10 * scale)));
    if (length_fp <= 14 * scale || length_fp >= 15 * scale) {
        Fail("scale-before-isqrt length should sit between 14 and 15 points");
    }
    // Mid-segment nearest snap: after 7 ticks of travel, ~halfway along (10,10).
    for (int i = 0; i < 7; ++i) {
        engine.StepForward();
    }
    soldier = engine.FindUnit(1);
    if (soldier->position.x != 25 || soldier->position.y != 15) {
        Fail("diagonal mid-point should nearest-snap to (25,15)");
    }
    for (int i = 0; i < 20 && engine.FindUnit(1)->move.active; ++i) {
        engine.StepForward();
    }
    soldier = engine.FindUnit(1);
    if (soldier->position.x != 30 || soldier->position.y != 20 || soldier->move.active) {
        Fail("soldier should arrive on diagonal target");
    }

    // Replace pending move before apply via SubmitOrder queue, and replace active via new order.
    Order first;
    first.type = OrderType::Move;
    first.unit_ids = {1};
    first.target = {0, 20};
    engine.SubmitOrder(std::move(first));
    Order second;
    second.type = OrderType::Move;
    second.unit_ids = {1};
    second.target = {40, 20};
    engine.SubmitOrder(std::move(second));
    if (engine.GetState().orders.size() != 1 || engine.GetState().orders[0].target.x != 40) {
        Fail("queued order replace");
    }
    engine.StepForward();
    soldier = engine.FindUnit(1);
    if (!soldier->move.active || soldier->move.end.x != 40) {
        Fail("active UnitMove should be the latest target");
    }

    // Finish that move, then test is_next waypoint chaining.
    StepUntilIdle(engine, 1, 80);
    soldier = engine.FindUnit(1);
    if (soldier->position.x != 40 || soldier->position.y != 20 || soldier->move.active) {
        Fail("soldier should be idle at (40,20) before chain test");
    }

    Order leg_a;
    leg_a.type = OrderType::Move;
    leg_a.unit_ids = {1};
    leg_a.target = {50, 20};
    engine.SubmitOrder(std::move(leg_a));
    engine.StepForward(); // start first leg
    soldier = engine.FindUnit(1);
    if (!soldier->move.active || soldier->move.end.x != 50) {
        Fail("first leg should be active");
    }

    Order leg_b;
    leg_b.type = OrderType::Move;
    leg_b.is_next = true;
    leg_b.unit_ids = {1};
    leg_b.target = {50, 30};
    engine.SubmitOrder(std::move(leg_b));

    Order leg_c;
    leg_c.type = OrderType::Move;
    leg_c.is_next = true;
    leg_c.unit_ids = {1};
    leg_c.target = {60, 30};
    engine.SubmitOrder(std::move(leg_c));

    if (engine.GetState().orders.size() != 2
        || !engine.GetState().orders[0].is_next
        || engine.GetState().orders[0].target.y != 30
        || engine.GetState().orders[1].target.x != 60) {
        Fail("is_next waypoints should remain queued while moving");
    }

    // Still on first leg — must not jump to waypoint early.
    engine.StepForward();
    soldier = engine.FindUnit(1);
    if (soldier->move.end.x != 50 || soldier->move.end.y != 20) {
        Fail("active move must stay on first leg while is_next is queued");
    }

    StepUntilIdle(engine, 1, 80);
    soldier = engine.FindUnit(1);
    if (soldier->position.x != 60 || soldier->position.y != 30 || soldier->move.active) {
        Fail("soldier should follow is_next chain to final waypoint");
    }
    if (!engine.GetState().orders.empty()) {
        Fail("order list should be empty after chain completes");
    }

    // Regular order cancels active move + discards queued is_next.
    Order cancel_start;
    cancel_start.type = OrderType::Move;
    cancel_start.unit_ids = {1};
    cancel_start.target = {70, 30};
    engine.SubmitOrder(std::move(cancel_start));
    engine.StepForward();

    Order queued_next;
    queued_next.type = OrderType::Move;
    queued_next.is_next = true;
    queued_next.unit_ids = {1};
    queued_next.target = {80, 30};
    engine.SubmitOrder(std::move(queued_next));

    Order interrupt;
    interrupt.type = OrderType::Move;
    interrupt.unit_ids = {1};
    interrupt.target = {70, 40};
    engine.SubmitOrder(std::move(interrupt));

    if (engine.GetState().orders.size() != 1 || engine.GetState().orders[0].is_next
        || engine.GetState().orders[0].target.y != 40) {
        Fail("regular order should discard is_next queue");
    }
    soldier = engine.FindUnit(1);
    if (soldier->move.active) {
        Fail("regular order should clear active move on submit");
    }
    engine.StepForward();
    soldier = engine.FindUnit(1);
    if (!soldier->move.active || soldier->move.end.x != 70 || soldier->move.end.y != 40) {
        Fail("interrupted unit should start the regular replacement move");
    }

    // Clamp util still stops a single straight segment before a wall (diameter 1 ≈ point).
    constexpr int32_t kPointDiameter = 1;
    PathingGrid wall;
    wall.ResizeOpen(1000, 1000);
    for (int32_t y = 0; y <= 20; ++y) {
        wall.At(15, y).blocked = true;
    }
    ComputeObstructionDistances(wall);
    if (ClampMoveDestination({10, 10}, {20, 10}, wall, kPointDiameter).x != 14) {
        Fail("clamp should stop short before a mid-path obstruction");
    }
    if (HasLineOfSight({10, 10}, {20, 10}, wall, kPointDiameter)) {
        Fail("LOS should be false through a wall");
    }

    // Partial wall: A* goes around and reaches the far side.
    {
        const PathFindResult around = FindMoveWaypoints({10, 10}, {20, 10}, wall, kPointDiameter);
        if (around.waypoints.empty() || around.waypoints.back().x != 20 || around.waypoints.back().y != 10) {
            Fail("pathfinding should route around a finite wall");
        }
        if (around.waypoints.size() < 2) {
            Fail("around-the-wall path should compress to multiple waypoints");
        }
        if (!HasLineOfSight({10, 10}, around.waypoints[0], wall, kPointDiameter)) {
            Fail("first compressed waypoint must have LOS from start");
        }
        for (size_t i = 0; i + 1 < around.waypoints.size(); ++i) {
            if (!HasLineOfSight(around.waypoints[i], around.waypoints[i + 1], wall, kPointDiameter)) {
                Fail("compressed waypoints must have LOS between consecutive points");
            }
        }
    }

    // Full-height wall: goal unreachable → closest free on the near side.
    PathingGrid sealed;
    sealed.ResizeOpen(1000, 1000);
    for (int32_t y = 0; y < 1000; ++y) {
        sealed.At(15, y).blocked = true;
    }
    ComputeObstructionDistances(sealed);
    {
        const PathFindResult blocked = FindMoveWaypoints({10, 10}, {20, 10}, sealed, kPointDiameter);
        if (blocked.reached_requested) {
            Fail("sealed wall should not reach requested goal");
        }
        if (blocked.waypoints.empty()
            || blocked.waypoints.back().x != 14
            || blocked.waypoints.back().y != 10) {
            Fail("unreachable goal should fall back near the wall");
        }
    }

    TickEngine path_engine;
    Level path_level = MakeSmokeLevel();
    for (int32_t y = 0; y <= 20; ++y) {
        path_level.static_data.pathing.At(15, y).blocked = true;
    }
    path_level.spawns.clear();
    path_level.spawns.push_back({1, UnitType::Soldier, {10, 10}, 0});
    path_engine.LoadLevel(path_level);

    Order around_order;
    around_order.type = OrderType::Move;
    around_order.unit_ids = {1};
    around_order.target = {20, 10};
    path_engine.SubmitOrder(std::move(around_order));
    if (path_engine.GetState().orders.size() < 2) {
        Fail("submit should enqueue multi-segment path around wall");
    }
    StepUntilIdle(path_engine, 1, 400);
    soldier = path_engine.FindUnit(1);
    if (soldier->position.x != 20 || soldier->position.y != 10 || soldier->move.active) {
        Fail("unit should pathfind around wall to destination");
    }

    // Sealed map via engine submit → stop on near side.
    TickEngine sealed_engine;
    Level sealed_level = MakeSmokeLevel();
    for (int32_t y = 0; y < 1000; ++y) {
        sealed_level.static_data.pathing.At(15, y).blocked = true;
    }
    sealed_level.spawns.clear();
    sealed_level.spawns.push_back({1, UnitType::Soldier, {10, 10}, 0});
    sealed_engine.LoadLevel(sealed_level);
    Order sealed_order;
    sealed_order.type = OrderType::Move;
    sealed_order.unit_ids = {1};
    sealed_order.target = {20, 10};
    sealed_engine.SubmitOrder(std::move(sealed_order));
    StepUntilIdle(sealed_engine, 1, 80);
    soldier = sealed_engine.FindUnit(1);
    if (soldier->position.x != 14 || soldier->position.y != 10 || soldier->move.active) {
        Fail("engine should stop at closest reachable when sealed");
    }

    // Group move: formation-preserving sparse goals (not both on the same cell).
    {
        TickEngine sparse_engine;
        Level sparse_level = MakeSmokeLevel();
        // Place left/right of COM so rays point west/east of the click.
        sparse_level.spawns.clear();
        sparse_level.spawns.push_back({1, UnitType::Soldier, {10, 50}, 0});
        sparse_level.spawns.push_back({2, UnitType::Soldier, {30, 50}, 0});
        sparse_level.static_data.unit_defs[static_cast<size_t>(UnitType::Soldier)].diameter = 10;
        sparse_engine.LoadLevel(sparse_level);

        Order group;
        group.type = OrderType::Move;
        group.unit_ids = {1, 2};
        group.target = {100, 50};
        sparse_engine.SubmitOrder(std::move(group));

        Vec2i goal_a{};
        Vec2i goal_b{};
        bool found_a = false;
        bool found_b = false;
        for (const Order& pending : sparse_engine.GetState().orders) {
            for (UnitId id : pending.unit_ids) {
                if (id == 1) {
                    goal_a = pending.target;
                    found_a = true;
                } else if (id == 2) {
                    goal_b = pending.target;
                    found_b = true;
                }
            }
        }
        if (!found_a || !found_b) {
            Fail("group move should enqueue a goal per unit");
        }
        if (goal_a == goal_b) {
            Fail("group move should assign distinct sparse goals");
        }
        // Preserve formation order along the axis (left unit stays left of right unit).
        if (goal_a.x >= goal_b.x) {
            Fail("formation sparse goals should preserve left/right order");
        }
        // Pairwise diameter clearance (soldier diameter 10 → centers at least 10 apart).
        const int32_t dx = goal_a.x - goal_b.x;
        const int32_t dy = goal_a.y - goal_b.y;
        if (4 * (dx * dx + dy * dy) < 20 * 20) {
            Fail("formation sparse goals should respect pairwise diameters");
        }
    }

    // Idle push: overlapping idle units accumulate weighted pressure and step apart.
    {
        TickEngine push_engine;
        Level push_level = MakeSmokeLevel();
        push_level.static_data.unit_defs[static_cast<size_t>(UnitType::Soldier)].idle_push = true;
        push_level.static_data.unit_defs[static_cast<size_t>(UnitType::Soldier)].diameter = 10;
        push_level.static_data.unit_defs[static_cast<size_t>(UnitType::Soldier)].weight = 1;
        push_level.spawns.clear();
        push_level.spawns.push_back({1, UnitType::Soldier, {50, 50}, 0});
        push_level.spawns.push_back({2, UnitType::Soldier, {50, 50}, 0});
        push_engine.LoadLevel(push_level);

        const Vec2i start_a = push_engine.FindUnit(1)->position;
        const Vec2i start_b = push_engine.FindUnit(2)->position;
        if (start_a != start_b) {
            Fail("idle push setup should stack units on the same cell");
        }
        for (int i = 0; i < 20; ++i) {
            push_engine.StepForward();
        }
        const Vec2i end_a = push_engine.FindUnit(1)->position;
        const Vec2i end_b = push_engine.FindUnit(2)->position;
        if (end_a == end_b) {
            Fail("idle push should separate stacked idle units");
        }
    }

    // Weighted idle push: light soldier moves before heavy vehicle.
    {
        TickEngine weight_engine;
        Level weight_level = MakeSmokeLevel();
        weight_level.static_data.unit_defs[static_cast<size_t>(UnitType::Soldier)].idle_push = true;
        weight_level.static_data.unit_defs[static_cast<size_t>(UnitType::Soldier)].diameter = 10;
        weight_level.static_data.unit_defs[static_cast<size_t>(UnitType::Soldier)].weight = 1;
        weight_level.static_data.unit_defs[static_cast<size_t>(UnitType::Vehicle)].idle_push = true;
        weight_level.static_data.unit_defs[static_cast<size_t>(UnitType::Vehicle)].diameter = 10;
        weight_level.static_data.unit_defs[static_cast<size_t>(UnitType::Vehicle)].weight = 100;
        weight_level.spawns.clear();
        weight_level.spawns.push_back({1, UnitType::Soldier, {50, 50}, 0});
        weight_level.spawns.push_back({2, UnitType::Vehicle, {50, 50}, 0});
        weight_engine.LoadLevel(weight_level);

        const Vec2i vehicle_start = weight_engine.FindUnit(2)->position;
        for (int i = 0; i < 3; ++i) {
            weight_engine.StepForward();
        }
        const Vec2i soldier_pos = weight_engine.FindUnit(1)->position;
        const Vec2i vehicle_pos = weight_engine.FindUnit(2)->position;
        if (soldier_pos == vehicle_start) {
            Fail("weighted idle push should move the light soldier within a few ticks");
        }
        if (vehicle_pos != vehicle_start) {
            Fail("weighted idle push should leave the heavy vehicle unmoved for several ticks");
        }
    }

    {
        TickEngine scheduled_engine;
        Level scheduled_level = MakeSmokeLevel();
        scheduled_engine.LoadLevel(scheduled_level);
        Order delayed;
        delayed.player_id = 2;
        delayed.unit_ids = {1};
        delayed.target = {20, 10};
        scheduled_engine.SubmitScheduled(delayed, 12, 5);
        scheduled_engine.SubmitScheduled(delayed, 12, 5);
        const Vec2i start = scheduled_engine.FindUnit(1)->position;
        for (int i = 0; i < 5; ++i) {
            scheduled_engine.StepForward();
            if (scheduled_engine.FindUnit(1)->position != start || scheduled_engine.FindUnit(1)->move.active) {
                Fail("scheduled order should not activate before scheduled_tick");
            }
        }
        scheduled_engine.StepForward();
        if (!scheduled_engine.FindUnit(1)->move.active) {
            Fail("scheduled order should pathfind when tick reaches scheduled_tick");
        }
    }

    {
        TickEngine hash_a;
        TickEngine hash_b;
        Level hash_level = MakeSmokeLevel();
        hash_a.LoadLevel(hash_level);
        hash_b.LoadLevel(hash_level);
        if (hash_a.GameplayHash() != hash_b.GameplayHash()) {
            Fail("identical loads should share a gameplay hash");
        }
        Order move;
        move.player_id = 1;
        move.unit_ids = {1};
        move.target = {20, 10};
        hash_a.SubmitOrder(move);
        hash_a.StepForward();
        hash_b.StepForward();
        if (hash_a.GameplayHash() == hash_b.GameplayHash()) {
            Fail("an extra SubmitOrder should change the gameplay hash");
        }
        TickEngine hash_c;
        hash_c.LoadLevel(hash_level);
        hash_c.SubmitOrder(move);
        hash_c.StepForward();
        if (hash_a.GameplayHash() != hash_c.GameplayHash()) {
            Fail("same inputs should share a gameplay hash");
        }
    }

    std::printf("OK: line-move + is_next + pathfinding smoke test passed (tick=%d, pos=%d,%d)\n",
                path_engine.GetTick(),
                path_engine.FindUnit(1)->position.x,
                path_engine.FindUnit(1)->position.y);
    return 0;
}
