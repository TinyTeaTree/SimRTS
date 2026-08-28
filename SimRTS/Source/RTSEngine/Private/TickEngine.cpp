#include "TickEngine.h"

#include "DetMath.h"
#include "PathClamp.h"
#include "PathFind.h"
#include "PathingClearance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace SimRTS {

namespace {

int32_t ClampCoord(int32_t value, int32_t max_exclusive) {
    if (value < 0) {
        return 0;
    }
    if (value >= max_exclusive) {
        return max_exclusive - 1;
    }
    return value;
}

int32_t RoundToInt(double value) {
    return static_cast<int32_t>(std::llround(value));
}

void ClearMove(Unit& unit) {
    unit.move = {};
}

void ClearPushPressure(Unit& unit) {
    unit.push_pressure_x = 0;
    unit.push_pressure_y = 0;
}

int32_t NormalizeDegrees(int32_t degrees) {
    degrees %= 360;
    if (degrees < 0) {
        degrees += 360;
    }
    return degrees;
}

// Visual yaw only: face the Euclidean travel direction (not snapped cell steps).
void UpdateRotationToward(Unit& unit, const Vec2i& from, const Vec2i& to) {
    const int32_t dx = to.x - from.x;
    const int32_t dy = to.y - from.y;
    if (dx == 0 && dy == 0) {
        return;
    }
    constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
    const double radians = std::atan2(static_cast<double>(dy), static_cast<double>(dx));
    unit.rotation = NormalizeDegrees(RoundToInt(radians * kRadToDeg));
}

void RemoveUnitFromOrders(std::vector<Order>& orders, UnitId id) {
    for (auto it = orders.begin(); it != orders.end();) {
        auto& ids = it->unit_ids;
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
        if (ids.empty()) {
            it = orders.erase(it);
        } else {
            ++it;
        }
    }
}

bool OrderContainsUnit(const Order& order, UnitId id) {
    return std::find(order.unit_ids.begin(), order.unit_ids.end(), id) != order.unit_ids.end();
}

Vec2i PathStartForQueuedUnit(const Unit& unit, const std::vector<Order>& orders) {
    for (int32_t i = static_cast<int32_t>(orders.size()) - 1; i >= 0; --i) {
        if (OrderContainsUnit(orders[i], unit.id)) {
            return orders[i].target;
        }
    }
    if (unit.move.active) {
        return unit.move.end;
    }
    return unit.position;
}

int32_t DiameterForUnit(const Unit& unit, const StaticBattleData& static_data) {
    const UnitDef* def = static_data.FindDef(unit.type);
    if (def == nullptr) {
        return 1;
    }
    return std::max(1, def->diameter);
}

int32_t WeightForUnit(const Unit& unit, const StaticBattleData& static_data) {
    const UnitDef* def = static_data.FindDef(unit.type);
    if (def == nullptr) {
        return 1;
    }
    return std::max(1, def->weight);
}

const UnitDef* DefForUnit(const Unit& unit, const StaticBattleData& static_data) {
    return static_data.FindDef(unit.type);
}

bool UnitWantsSparseGoals(const Unit& unit, const StaticBattleData& static_data) {
    const UnitDef* def = DefForUnit(unit, static_data);
    return def == nullptr || def->sparse_goals;
}

bool UnitWantsIdlePush(const Unit& unit, const StaticBattleData& static_data) {
    const UnitDef* def = DefForUnit(unit, static_data);
    return def == nullptr || def->idle_push;
}

bool IsIdle(const Unit& unit) {
    return !unit.move.active;
}

// Too close when center distance < (diameter_a + diameter_b) / 2.
bool DiscsOverlap(const Vec2i& a, int32_t diameter_a, const Vec2i& b, int32_t diameter_b) {
    const int32_t dx = a.x - b.x;
    const int32_t dy = a.y - b.y;
    const int32_t min_sep = diameter_a + diameter_b;
    return 4 * (dx * dx + dy * dy) < min_sep * min_sep;
}

Vec2i NudgeAway(Vec2i from, Vec2i away_from) {
    const int32_t dx = from.x - away_from.x;
    const int32_t dy = from.y - away_from.y;
    if (dx == 0 && dy == 0) {
        // Same cell: deterministic bias by id is applied by caller via secondary axis.
        return {from.x + 1, from.y};
    }
    if (std::abs(dx) >= std::abs(dy)) {
        return {from.x + (dx > 0 ? 1 : -1), from.y};
    }
    return {from.x, from.y + (dy > 0 ? 1 : -1)};
}

// Fixed-point: kPushPressureScale == 1 grid point of idle-push residual.
constexpr int32_t kPushPressureScale = 1024;

int32_t ClampPushPressure(int32_t value) {
    if (value > kPushPressureScale) {
        return kPushPressureScale;
    }
    if (value < -kPushPressureScale) {
        return -kPushPressureScale;
    }
    return value;
}

void AddPushPressure(Unit& unit, int32_t step_x, int32_t step_y, int32_t share) {
    if (share <= 0) {
        return;
    }
    if (step_x != 0) {
        unit.push_pressure_x = ClampPushPressure(unit.push_pressure_x + step_x * share);
    }
    if (step_y != 0) {
        unit.push_pressure_y = ClampPushPressure(unit.push_pressure_y + step_y * share);
    }
}

bool CanStepTo(
    Vec2i next,
    int32_t diameter,
    const StaticBattleData& static_data) {
    if (next.x < 0 || next.y < 0
        || next.x >= static_data.world_width
        || next.y >= static_data.world_height) {
        return false;
    }
    return FitsDiameter(static_data.pathing, next.x, next.y, diameter);
}

// Separation step for `from` away from `away_from` (same-cell uses id bias).
Vec2i SeparationStep(const Unit& from, const Unit& away_from) {
    Vec2i next = NudgeAway(from.position, away_from.position);
    if (from.position == away_from.position && from.id > away_from.id) {
        next = {from.position.x - 1, from.position.y};
    }
    return {
        next.x - from.position.x,
        next.y - from.position.y,
    };
}

struct PlacedSlot {
    Vec2i pos;
    int32_t diameter = 1;
};

bool FitsAgainstPlaced(Vec2i pos, int32_t diameter, const std::vector<PlacedSlot>& placed) {
    for (const PlacedSlot& slot : placed) {
        if (DiscsOverlap(pos, diameter, slot.pos, slot.diameter)) {
            return false;
        }
    }
    return true;
}

// Formation-preserving sparse goals: rays from selection COM through each unit,
// expand from the click along that ray until pairwise unit diameters fit.
// Walls are ignored here — pathfinding clamps segments; idle_push separates clumps.
// Placement order = ascending unit id (keeps relative formation, no largest-first).
std::vector<Vec2i> FormationSparseGoals(
    Vec2i click,
    const std::vector<Unit*>& sparse_members,
    const StaticBattleData& static_data,
    const std::vector<PlacedSlot>& initially_placed) {
    const int32_t world_w = static_data.world_width;
    const int32_t world_h = static_data.world_height;
    const int32_t n = static_cast<int32_t>(sparse_members.size());
    std::vector<Vec2i> goals(static_cast<size_t>(n), click);
    if (n <= 0) {
        return goals;
    }
    if (n == 1) {
        goals[0] = click;
        return goals;
    }

    double com_x = 0.0;
    double com_y = 0.0;
    for (const Unit* unit : sparse_members) {
        com_x += static_cast<double>(unit->position.x);
        com_y += static_cast<double>(unit->position.y);
    }
    com_x /= static_cast<double>(n);
    com_y /= static_cast<double>(n);

    constexpr double kPi = 3.14159265358979323846;
    constexpr double kEps = 1e-6;
    // Cap search so a jammed ray cannot loop forever.
    const int32_t max_t = std::max(world_w, world_h) + 1;

    std::vector<PlacedSlot> placed = initially_placed;
    for (int32_t i = 0; i < n; ++i) {
        Unit& unit = *sparse_members[static_cast<size_t>(i)];
        const int32_t diameter = DiameterForUnit(unit, static_data);

        double dir_x = static_cast<double>(unit.position.x) - com_x;
        double dir_y = static_cast<double>(unit.position.y) - com_y;
        const double len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
        if (len < kEps) {
            // Stacked on COM: deterministic fallback ring directions by id-rank.
            const double angle = (2.0 * kPi * static_cast<double>(i)) / static_cast<double>(n);
            dir_x = std::cos(angle);
            dir_y = std::sin(angle);
        } else {
            dir_x /= len;
            dir_y /= len;
        }

        Vec2i chosen = click;
        bool found = false;
        for (int32_t t = 0; t <= max_t; ++t) {
            const Vec2i candidate = {
                ClampCoord(click.x + RoundToInt(dir_x * static_cast<double>(t)), world_w),
                ClampCoord(click.y + RoundToInt(dir_y * static_cast<double>(t)), world_h),
            };
            if (!FitsAgainstPlaced(candidate, diameter, placed)) {
                continue;
            }
            chosen = candidate;
            found = true;
            break;
        }
        if (!found) {
            // Ray exhausted: keep click (pathfinding will clamp). Still record for later units.
            chosen = click;
        }

        goals[static_cast<size_t>(i)] = chosen;
        placed.push_back({chosen, diameter});
    }
    return goals;
}

void BeginMove(
    Unit& unit,
    Vec2i target,
    Tick tick,
    const PathingGrid& pathing,
    int32_t diameter) {
    const Vec2i clamped = ClampMoveDestination(unit.position, target, pathing, diameter);

    ClearPushPressure(unit);
    unit.move.active = true;
    unit.move.start = unit.position;
    unit.move.end = clamped;
    unit.move.start_tick = tick;

    if (unit.move.start == unit.move.end) {
        ClearMove(unit);
        return;
    }

    const int64_t dx = static_cast<int64_t>(unit.move.end.x) - unit.move.start.x;
    const int64_t dy = static_cast<int64_t>(unit.move.end.y) - unit.move.start.y;
    // Scale before isqrt so length_fp keeps fractional points:
    // SCALE * sqrt(dx²+dy²) = sqrt((dx*SCALE)² + (dy*SCALE)²).
    const int64_t sx = dx * static_cast<int64_t>(kMoveScale);
    const int64_t sy = dy * static_cast<int64_t>(kMoveScale);
    const uint64_t d2 = static_cast<uint64_t>(sx * sx + sy * sy);
    unit.move.length_fp = static_cast<int64_t>(IsqrtU64(d2));
    if (unit.move.length_fp <= 0) {
        ClearMove(unit);
        return;
    }

    UpdateRotationToward(unit, unit.move.start, unit.move.end);
}

void EnqueuePathForUnit(
    std::vector<Order>& orders,
    UnitId unit_id,
    Vec2i start,
    Vec2i requested_goal,
    bool first_is_next,
    const PathingGrid& pathing,
    int32_t diameter,
    PlayerId player_id) {
    const PathFindResult found = FindMoveWaypoints(start, requested_goal, pathing, diameter);
    for (size_t i = 0; i < found.waypoints.size(); ++i) {
        Order segment;
        segment.player_id = player_id;
        segment.type = OrderType::Move;
        segment.unit_ids = {unit_id};
        segment.target = found.waypoints[i];
        segment.is_next = first_is_next || (i > 0);
        orders.push_back(std::move(segment));
    }
}

} // namespace

void TickEngine::LoadLevel(const Level& level) {
    static_data_ = level.static_data;
    state_ = {};
    tick_ = 0;

    // blocked flags come from JSON; clearance is derived once in the engine.
    ComputeObstructionDistances(static_data_.pathing);

    state_.units.reserve(level.spawns.size());
    for (const LevelUnitSpawn& spawn : level.spawns) {
        Unit unit;
        unit.id = spawn.id;
        unit.type = spawn.type;
        unit.position.x = ClampCoord(spawn.position.x, static_data_.world_width);
        unit.position.y = ClampCoord(spawn.position.y, static_data_.world_height);
        unit.rotation = NormalizeDegrees(spawn.rotation);
        state_.units.push_back(unit);
    }

    scheduled_.clear();
}

void TickEngine::SubmitOrder(Order order) {
    if (order.unit_ids.empty()) {
        return;
    }

    if (order.type != OrderType::Move) {
        state_.orders.push_back(std::move(order));
        return;
    }

    const Vec2i requested{
        ClampCoord(order.target.x, static_data_.world_width),
        ClampCoord(order.target.y, static_data_.world_height),
    };

    // Stable assignment order for sparse slots (lockstep-friendly).
    std::vector<UnitId> unit_ids = order.unit_ids;
    std::sort(unit_ids.begin(), unit_ids.end());
    unit_ids.erase(std::unique(unit_ids.begin(), unit_ids.end()), unit_ids.end());

    std::vector<Unit*> members;
    std::vector<Unit*> sparse_members;
    members.reserve(unit_ids.size());
    sparse_members.reserve(unit_ids.size());
    for (UnitId id : unit_ids) {
        Unit* unit = FindUnit(id);
        if (unit == nullptr) {
            continue;
        }
        members.push_back(unit);
        if (UnitWantsSparseGoals(*unit, static_data_)) {
            sparse_members.push_back(unit);
        }
    }
    if (members.empty()) {
        return;
    }

    // Non-sparse units keep the raw click; treat them as already occupying that cell
    // so sparse expansion respects their diameters.
    std::vector<PlacedSlot> initially_placed;
    for (Unit* unit : members) {
        if (!UnitWantsSparseGoals(*unit, static_data_)) {
            initially_placed.push_back({requested, DiameterForUnit(*unit, static_data_)});
        }
    }

    const std::vector<Vec2i> sparse_goals = FormationSparseGoals(
        requested,
        sparse_members,
        static_data_,
        initially_placed);

    // Map sparse participant → formation slot; others keep the raw click.
    std::vector<Vec2i> goals(members.size(), requested);
    for (size_t s = 0; s < sparse_members.size(); ++s) {
        for (size_t m = 0; m < members.size(); ++m) {
            if (members[m] == sparse_members[s]) {
                goals[m] = sparse_goals[s];
                break;
            }
        }
    }

    const bool queue_as_next = order.is_next;
    for (size_t i = 0; i < members.size(); ++i) {
        Unit& unit = *members[i];
        const Vec2i goal = goals[i];

        Vec2i start;
        if (!queue_as_next) {
            RemoveUnitFromOrders(state_.orders, unit.id);
            ClearMove(unit);
            start = unit.position;
        } else {
            start = PathStartForQueuedUnit(unit, state_.orders);
        }

        EnqueuePathForUnit(
            state_.orders,
            unit.id,
            start,
            goal,
            queue_as_next,
            static_data_.pathing,
            DiameterForUnit(unit, static_data_),
            order.player_id);
    }
}

void TickEngine::SubmitScheduled(Order order, uint32_t order_id, Tick scheduled_tick) {
    if (order.unit_ids.empty()) {
        return;
    }
    for (const ScheduledCommand& command : scheduled_) {
        if (command.order.player_id == order.player_id && command.order_id == order_id) {
            return;
        }
    }
    ScheduledCommand command;
    command.order = std::move(order);
    command.order_id = order_id;
    command.scheduled_tick = scheduled_tick;
    scheduled_.push_back(std::move(command));
}

void TickEngine::ActivateScheduled() {
    std::vector<ScheduledCommand> due;
    std::vector<ScheduledCommand> keep;
    due.reserve(scheduled_.size());
    keep.reserve(scheduled_.size());
    for (ScheduledCommand& command : scheduled_) {
        if (command.scheduled_tick <= tick_) {
            due.push_back(std::move(command));
        } else {
            keep.push_back(std::move(command));
        }
    }
    scheduled_ = std::move(keep);
    std::sort(due.begin(), due.end(), [](const ScheduledCommand& a, const ScheduledCommand& b) {
        if (a.order.player_id != b.order.player_id) {
            return a.order.player_id < b.order.player_id;
        }
        return a.order_id < b.order_id;
    });
    for (ScheduledCommand& command : due) {
        SubmitOrder(std::move(command.order));
    }
}

void TickEngine::StepForward() {
    ActivateScheduled();
    ApplyOrders();
    AdvanceMovement();
    // Units that finished a segment this tick can start the next is_next waypoint immediately.
    ApplyOrders();
    ApplyIdlePush();
    ++tick_;
}

uint64_t TickEngine::GameplayHash() const {
    uint64_t hash = 14695981039346656037ull;
    const auto mix8 = [&](uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    const auto mix32 = [&](uint32_t value) {
        mix8(value);
        mix8(value >> 8);
        mix8(value >> 16);
        mix8(value >> 24);
    };
    const auto mix_i32 = [&](int32_t value) {
        mix32(static_cast<uint32_t>(value));
    };
    const auto mix64 = [&](uint64_t value) {
        mix8(value);
        mix8(value >> 8);
        mix8(value >> 16);
        mix8(value >> 24);
        mix8(value >> 32);
        mix8(value >> 40);
        mix8(value >> 48);
        mix8(value >> 56);
    };

    std::vector<const Unit*> units;
    units.reserve(state_.units.size());
    for (const Unit& unit : state_.units) {
        units.push_back(&unit);
    }
    std::sort(units.begin(), units.end(), [](const Unit* a, const Unit* b) {
        return a->id < b->id;
    });

    mix32(static_cast<uint32_t>(units.size()));
    for (const Unit* unit : units) {
        mix_i32(unit->id);
        mix8(static_cast<uint8_t>(unit->type));
        mix_i32(unit->position.x);
        mix_i32(unit->position.y);
        mix8(unit->move.active ? 1 : 0);
        mix_i32(unit->move.start.x);
        mix_i32(unit->move.start.y);
        mix_i32(unit->move.end.x);
        mix_i32(unit->move.end.y);
        mix_i32(unit->move.start_tick);
        mix64(static_cast<uint64_t>(unit->move.length_fp));
        mix_i32(unit->push_pressure_x);
        mix_i32(unit->push_pressure_y);
    }

    mix32(static_cast<uint32_t>(state_.orders.size()));
    for (const Order& order : state_.orders) {
        mix_i32(order.player_id);
        mix32(static_cast<uint32_t>(order.unit_ids.size()));
        for (UnitId id : order.unit_ids) {
            mix_i32(id);
        }
        mix8(static_cast<uint8_t>(order.type));
        mix_i32(order.target.x);
        mix_i32(order.target.y);
        mix8(order.is_next ? 1 : 0);
    }
    return hash;
}

const Unit* TickEngine::FindUnit(UnitId id) const {
    for (const Unit& unit : state_.units) {
        if (unit.id == id) {
            return &unit;
        }
    }
    return nullptr;
}

Unit* TickEngine::FindUnit(UnitId id) {
    for (Unit& unit : state_.units) {
        if (unit.id == id) {
            return &unit;
        }
    }
    return nullptr;
}

void TickEngine::ApplyOrders() {
    for (auto order_it = state_.orders.begin(); order_it != state_.orders.end();) {
        if (order_it->type != OrderType::Move) {
            ++order_it;
            continue;
        }

        const Vec2i target{
            ClampCoord(order_it->target.x, static_data_.world_width),
            ClampCoord(order_it->target.y, static_data_.world_height),
        };

        auto& ids = order_it->unit_ids;
        for (auto id_it = ids.begin(); id_it != ids.end();) {
            Unit* unit = FindUnit(*id_it);
            if (unit == nullptr) {
                id_it = ids.erase(id_it);
                continue;
            }

            // is_next waits until the unit is idle; only one active move per unit.
            if (order_it->is_next && unit->move.active) {
                ++id_it;
                continue;
            }

            BeginMove(*unit, target, tick_, static_data_.pathing, DiameterForUnit(*unit, static_data_));
            id_it = ids.erase(id_it);
        }

        if (ids.empty()) {
            order_it = state_.orders.erase(order_it);
        } else {
            ++order_it;
        }
    }
}

void TickEngine::AdvanceMovement() {
    const int32_t tps = static_data_.ticks_per_second;
    if (tps <= 0) {
        return;
    }

    for (Unit& unit : state_.units) {
        if (!unit.move.active) {
            continue;
        }

        const UnitDef* def = static_data_.FindDef(unit.type);
        if (def == nullptr || def->speed <= 0 || unit.move.length_fp <= 0) {
            ClearMove(unit);
            continue;
        }

        const int64_t elapsed = static_cast<int64_t>(tick_) - unit.move.start_tick;
        if (elapsed < 0) {
            continue;
        }

        // traveled_fp = speed * elapsed * SCALE / tps  (points × SCALE).
        const int64_t traveled_fp =
            (static_cast<int64_t>(def->speed) * elapsed * static_cast<int64_t>(kMoveScale))
            / static_cast<int64_t>(tps);

        if (traveled_fp >= unit.move.length_fp) {
            unit.position = unit.move.end;
            ClearMove(unit);
            continue;
        }

        const int64_t dx = static_cast<int64_t>(unit.move.end.x) - unit.move.start.x;
        const int64_t dy = static_cast<int64_t>(unit.move.end.y) - unit.move.start.y;
        // Linear lerp: offset = round(delta * traveled_fp / length_fp).
        const int64_t ox = DivRoundNearest(dx * traveled_fp, unit.move.length_fp);
        const int64_t oy = DivRoundNearest(dy * traveled_fp, unit.move.length_fp);

        unit.position = {
            ClampCoord(static_cast<int32_t>(unit.move.start.x + ox), static_data_.world_width),
            ClampCoord(static_cast<int32_t>(unit.move.start.y + oy), static_data_.world_height),
        };
    }
}

void TickEngine::ApplyIdlePush() {
    // Weighted separation: each overlapping pair contributes 1 point of push split by
    // inverse weight (lighter moves more). Shares accumulate as fixed-point pressure;
    // a unit only steps when |pressure| reaches one point. Walls act as infinite mass.
    const int32_t n = static_cast<int32_t>(state_.units.size());
    if (n <= 1) {
        return;
    }

    std::vector<Unit*> ordered;
    ordered.reserve(static_cast<size_t>(n));
    for (Unit& unit : state_.units) {
        ordered.push_back(&unit);
    }
    std::sort(ordered.begin(), ordered.end(), [](const Unit* a, const Unit* b) {
        return a->id < b->id;
    });

    for (int32_t i = 0; i < n; ++i) {
        Unit& a = *ordered[static_cast<size_t>(i)];
        const int32_t diameter_a = DiameterForUnit(a, static_data_);
        const bool a_receives = IsIdle(a) && UnitWantsIdlePush(a, static_data_);

        for (int32_t j = i + 1; j < n; ++j) {
            Unit& b = *ordered[static_cast<size_t>(j)];
            const int32_t diameter_b = DiameterForUnit(b, static_data_);
            const bool b_receives = IsIdle(b) && UnitWantsIdlePush(b, static_data_);
            if (!a_receives && !b_receives) {
                continue;
            }
            if (!DiscsOverlap(a.position, diameter_a, b.position, diameter_b)) {
                continue;
            }

            const Vec2i step_a = SeparationStep(a, b);
            const Vec2i step_b = SeparationStep(b, a);
            const Vec2i next_a = {a.position.x + step_a.x, a.position.y + step_a.y};
            const Vec2i next_b = {b.position.x + step_b.x, b.position.y + step_b.y};

            // Obstruction (or world edge) = infinite weight pushing back.
            const bool a_blocked = (step_a.x == 0 && step_a.y == 0)
                || !CanStepTo(next_a, diameter_a, static_data_);
            const bool b_blocked = (step_b.x == 0 && step_b.y == 0)
                || !CanStepTo(next_b, diameter_b, static_data_);

            int32_t share_a = 0;
            int32_t share_b = 0;
            if (a_blocked && b_blocked) {
                // Neither can take the separation this tick.
            } else if (a_blocked) {
                share_b = kPushPressureScale;
            } else if (b_blocked) {
                share_a = kPushPressureScale;
            } else {
                const int32_t weight_a = WeightForUnit(a, static_data_);
                const int32_t weight_b = WeightForUnit(b, static_data_);
                const int32_t total = weight_a + weight_b;
                // Lighter unit receives the larger share.
                share_a = (weight_b * kPushPressureScale) / total;
                share_b = (weight_a * kPushPressureScale) / total;
            }

            if (a_receives) {
                AddPushPressure(a, step_a.x, step_a.y, share_a);
            }
            if (b_receives) {
                AddPushPressure(b, step_b.x, step_b.y, share_b);
            }
        }
    }

    // Apply at most one grid step per unit from accumulated pressure (dominant axis).
    for (Unit* unit_ptr : ordered) {
        Unit& unit = *unit_ptr;
        if (!IsIdle(unit) || !UnitWantsIdlePush(unit, static_data_)) {
            continue;
        }

        const int32_t abs_x = std::abs(unit.push_pressure_x);
        const int32_t abs_y = std::abs(unit.push_pressure_y);
        if (abs_x < kPushPressureScale && abs_y < kPushPressureScale) {
            continue;
        }

        const int32_t diameter = DiameterForUnit(unit, static_data_);
        int32_t step_x = 0;
        int32_t step_y = 0;
        if (abs_x >= abs_y) {
            step_x = unit.push_pressure_x > 0 ? 1 : -1;
        } else {
            step_y = unit.push_pressure_y > 0 ? 1 : -1;
        }

        const Vec2i next = {unit.position.x + step_x, unit.position.y + step_y};
        if (!CanStepTo(next, diameter, static_data_)) {
            // Wall / clearance: keep pressure (infinite mass already handled in shares;
            // still blocked at apply time if the cell became illegal).
            continue;
        }

        unit.position = next;
        if (step_x != 0) {
            unit.push_pressure_x -= step_x * kPushPressureScale;
        }
        if (step_y != 0) {
            unit.push_pressure_y -= step_y * kPushPressureScale;
        }
    }
}

} // namespace SimRTS
