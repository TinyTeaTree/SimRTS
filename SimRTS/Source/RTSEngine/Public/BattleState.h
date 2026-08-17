#pragma once

#include "Order.h"
#include "Unit.h"
#include "UnitDef.h"

#include <array>
#include <vector>

namespace SimRTS {

inline constexpr int32_t kWorldWidth = 1000;
inline constexpr int32_t kWorldHeight = 1000;
inline constexpr int32_t kUnitTypeCount = 2;

// Per-cell static pathing data. Built once from the level obstruction string.
// Distances and unit radii share the same discrete unit: 1 point = 1 dm.
struct GridCell {
    bool blocked = false;
    // Floor of Euclidean distance in points to the nearest blocked cell.
    // 0 on blocked cells. Filled by ComputeObstructionDistances after level load.
    int32_t obstruction_distance = 0;
};

// Discrete world grid. JSON stores a linear '0'/'1' string (index = y * width + x);
// runtime keeps a 2D array for fast At(x, y) access. Static for the battle lifetime.
struct PathingGrid {
    int32_t width = 0;
    int32_t height = 0;
    // cells[y][x]
    std::vector<std::vector<GridCell>> cells;

    void ResizeOpen(int32_t w, int32_t h) {
        width = w;
        height = h;
        cells.assign(h, std::vector<GridCell>(w));
    }

    bool InBounds(int32_t x, int32_t y) const {
        return x >= 0 && y >= 0 && x < width && y < height;
    }

    const GridCell& At(int32_t x, int32_t y) const {
        return cells[y][x];
    }

    GridCell& At(int32_t x, int32_t y) {
        return cells[y][x];
    }

    bool IsBlocked(int32_t x, int32_t y) const {
        return At(x, y).blocked;
    }

    int32_t CellCount() const {
        return width * height;
    }
};

// Static battle data assembled from level + game-rules JSON.
// ticks_per_second comes from game rules; Unreal drives its timer from the loaded value.
struct StaticBattleData {
    int32_t world_width = kWorldWidth;
    int32_t world_height = kWorldHeight;
    int32_t ticks_per_second = 0;
    std::array<UnitDef, kUnitTypeCount> unit_defs{};
    PathingGrid pathing;

    const UnitDef* FindDef(UnitType type) const {
        const auto index = static_cast<size_t>(type);
        if (index >= unit_defs.size()) {
            return nullptr;
        }
        return &unit_defs[index];
    }
};

// Live battle data that changes each tick.
struct BattleState {
    std::vector<Unit> units;
    std::vector<Order> orders;
};

// Spawn description used when loading a level.
struct LevelUnitSpawn {
    UnitId id = 0;
    UnitType type = UnitType::Soldier;
    Vec2i position;
    int32_t rotation = 0;
};

struct Level {
    StaticBattleData static_data;
    std::vector<LevelUnitSpawn> spawns;
};

} // namespace SimRTS
