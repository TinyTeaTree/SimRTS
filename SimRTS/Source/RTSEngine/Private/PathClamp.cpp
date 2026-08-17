#include "PathClamp.h"

#include "PathingClearance.h"

#include <algorithm>
#include <cmath>

namespace SimRTS {

namespace {

int32_t RoundToInt(double value) {
    return static_cast<int32_t>(std::llround(value));
}

Vec2i SnapPoint(double x, double y, const PathingGrid& pathing) {
    int32_t ix = RoundToInt(x);
    int32_t iy = RoundToInt(y);
    if (pathing.width > 0) {
        ix = std::clamp(ix, 0, pathing.width - 1);
    }
    if (pathing.height > 0) {
        iy = std::clamp(iy, 0, pathing.height - 1);
    }
    return {ix, iy};
}

bool IsEnterBlocked(const PathingGrid& pathing, Vec2i cell, int32_t diameter) {
    if (pathing.width <= 0 || pathing.height <= 0 || pathing.cells.empty()) {
        return false;
    }
    return !FitsDiameter(pathing, cell.x, cell.y, diameter);
}

} // namespace

Vec2i ClampMoveDestination(Vec2i start, Vec2i end, const PathingGrid& pathing, int32_t diameter) {
    if (start == end) {
        return start;
    }

    const double dx = static_cast<double>(end.x - start.x);
    const double dy = static_cast<double>(end.y - start.y);
    const double distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 0.0) {
        return start;
    }

    // Speed-independent dense sampling of the same lerp+round path UnitMove uses.
    // Step 0.5 points so round-to-nearest cannot skip a grid cell along the segment.
    constexpr double kStepPoints = 0.5;
    const int32_t steps = std::max(1, static_cast<int32_t>(std::ceil(distance / kStepPoints)));

    Vec2i last_free = start;
    Vec2i prev = start;

    for (int32_t i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const Vec2i cell = SnapPoint(
            static_cast<double>(start.x) + dx * t,
            static_cast<double>(start.y) + dy * t,
            pathing);

        if (cell == prev) {
            continue;
        }
        prev = cell;

        if (IsEnterBlocked(pathing, cell, diameter)) {
            return last_free;
        }
        last_free = cell;
    }

    // Ensure the exact endpoint is considered (floating step accumulation / rounding).
    if (end != prev) {
        if (IsEnterBlocked(pathing, end, diameter)) {
            return last_free;
        }
        last_free = end;
    }

    return last_free;
}

} // namespace SimRTS
