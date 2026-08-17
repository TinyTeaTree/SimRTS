#pragma once

#include <cstdint>

namespace SimRTS {

using Tick = int32_t;
using UnitId = int32_t;

struct Vec2i {
    int32_t x = 0;
    int32_t y = 0;

    bool operator==(const Vec2i& other) const { return x == other.x && y == other.y; }
    bool operator!=(const Vec2i& other) const { return !(*this == other); }
};

enum class UnitType : uint8_t {
    Soldier = 0,
    Vehicle = 1,
};

enum class OrderType : uint8_t {
    Move = 0,
};

} // namespace SimRTS
