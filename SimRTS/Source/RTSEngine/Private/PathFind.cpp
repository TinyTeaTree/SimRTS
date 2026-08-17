#include "PathFind.h"

#include "PathClamp.h"
#include "PathingClearance.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <queue>
#include <vector>

namespace SimRTS {

namespace {

constexpr float kOrthoCost = 1.f;
constexpr float kDiagCost = 1.41421356f;
constexpr int32_t kMaxExpansions = 250000;

int32_t CellIndex(int32_t x, int32_t y, int32_t width) {
    return y * width + x;
}

float OctileDistance(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    const int32_t dx = std::abs(ax - bx);
    const int32_t dy = std::abs(ay - by);
    const int32_t mn = std::min(dx, dy);
    const int32_t mx = std::max(dx, dy);
    return kOrthoCost * static_cast<float>(mx - mn) + kDiagCost * static_cast<float>(mn);
}

// Start may sit too close to a wall; still allowed as a search origin.
bool CanOccupy(const PathingGrid& pathing, int32_t x, int32_t y, Vec2i start, int32_t diameter) {
    if (x == start.x && y == start.y) {
        return pathing.InBounds(x, y);
    }
    return FitsDiameter(pathing, x, y, diameter);
}

bool CanStepDiagonal(
    const PathingGrid& pathing,
    int32_t from_x,
    int32_t from_y,
    int32_t to_x,
    int32_t to_y,
    Vec2i start,
    int32_t diameter) {
    // No corner-cutting through ortho neighbors that don't fit this diameter.
    const int32_t corner_a_x = to_x;
    const int32_t corner_a_y = from_y;
    const int32_t corner_b_x = from_x;
    const int32_t corner_b_y = to_y;
    return CanOccupy(pathing, corner_a_x, corner_a_y, start, diameter)
        && CanOccupy(pathing, corner_b_x, corner_b_y, start, diameter);
}

Vec2i FindNearestFittingCell(Vec2i from, const PathingGrid& pathing, int32_t diameter) {
    if (pathing.width <= 0 || pathing.height <= 0 || pathing.cells.empty()) {
        return from;
    }
    if (!pathing.InBounds(from.x, from.y)) {
        return from;
    }
    if (FitsDiameter(pathing, from.x, from.y, diameter)) {
        return from;
    }

    const int32_t width = pathing.width;
    const int32_t height = pathing.height;
    const int32_t total = width * height;
    std::vector<uint8_t> visited(total, 0);
    std::queue<Vec2i> q;
    q.push(from);
    visited[CellIndex(from.x, from.y, width)] = 1;

    static const int32_t kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    while (!q.empty()) {
        const Vec2i cur = q.front();
        q.pop();

        for (const auto& d : kDirs) {
            const int32_t nx = cur.x + d[0];
            const int32_t ny = cur.y + d[1];
            if (!pathing.InBounds(nx, ny)) {
                continue;
            }
            const int32_t idx = CellIndex(nx, ny, width);
            if (visited[idx]) {
                continue;
            }
            visited[idx] = 1;
            if (FitsDiameter(pathing, nx, ny, diameter)) {
                return {nx, ny};
            }
            q.push({nx, ny});
        }
    }

    return from;
}

std::vector<Vec2i> CompressPath(
    const std::vector<Vec2i>& path,
    const PathingGrid& pathing,
    int32_t diameter) {
    if (path.size() <= 2) {
        return path;
    }

    std::vector<Vec2i> compressed;
    size_t i = 0;
    compressed.push_back(path[0]);

    while (i + 1 < path.size()) {
        size_t best = i + 1;
        for (size_t j = i + 1; j < path.size(); ++j) {
            if (HasLineOfSight(path[i], path[j], pathing, diameter)) {
                best = j;
            }
        }
        compressed.push_back(path[best]);
        i = best;
    }

    return compressed;
}

struct OpenNode {
    float f = 0.f;
    float g = 0.f;
    int32_t x = 0;
    int32_t y = 0;
};

struct OpenNodeGreater {
    bool operator()(const OpenNode& a, const OpenNode& b) const {
        if (a.f != b.f) {
            return a.f > b.f;
        }
        return a.g < b.g;
    }
};

std::vector<Vec2i> ReconstructPath(
    const std::vector<int32_t>& came_from,
    int32_t goal_index,
    int32_t width) {
    std::vector<Vec2i> path;
    int32_t idx = goal_index;
    while (idx >= 0) {
        const int32_t x = idx % width;
        const int32_t y = idx / width;
        path.push_back({x, y});
        idx = came_from[idx];
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<Vec2i> RunAStar(Vec2i start, Vec2i goal, const PathingGrid& pathing, int32_t diameter) {
    const int32_t width = pathing.width;
    const int32_t height = pathing.height;
    if (width <= 0 || height <= 0 || pathing.cells.empty()) {
        return {start, goal};
    }
    if (!pathing.InBounds(start.x, start.y) || !pathing.InBounds(goal.x, goal.y)) {
        return {};
    }
    if (start == goal) {
        return {start};
    }

    const int32_t total = width * height;
    std::vector<float> g_score(total, std::numeric_limits<float>::infinity());
    std::vector<int32_t> came_from(total, -1);
    std::vector<uint8_t> closed(total, 0);

    const int32_t start_i = CellIndex(start.x, start.y, width);
    const int32_t goal_i = CellIndex(goal.x, goal.y, width);
    g_score[start_i] = 0.f;

    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeGreater> open;
    open.push({OctileDistance(start.x, start.y, goal.x, goal.y), 0.f, start.x, start.y});

    int32_t best_i = start_i;
    float best_h = OctileDistance(start.x, start.y, goal.x, goal.y);
    int32_t expansions = 0;

    static const int32_t kOffsets[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
    };

    while (!open.empty() && expansions < kMaxExpansions) {
        const OpenNode cur = open.top();
        open.pop();
        const int32_t cur_i = CellIndex(cur.x, cur.y, width);

        if (closed[cur_i]) {
            continue;
        }
        if (cur.g > g_score[cur_i]) {
            continue;
        }
        closed[cur_i] = 1;
        ++expansions;

        const float h_here = OctileDistance(cur.x, cur.y, goal.x, goal.y);
        if (h_here < best_h) {
            best_h = h_here;
            best_i = cur_i;
        }

        if (cur_i == goal_i) {
            return ReconstructPath(came_from, goal_i, width);
        }

        for (const auto& off : kOffsets) {
            const int32_t nx = cur.x + off[0];
            const int32_t ny = cur.y + off[1];
            if (!CanOccupy(pathing, nx, ny, start, diameter)) {
                continue;
            }

            const bool diagonal = off[0] != 0 && off[1] != 0;
            if (diagonal && !CanStepDiagonal(pathing, cur.x, cur.y, nx, ny, start, diameter)) {
                continue;
            }

            const int32_t n_i = CellIndex(nx, ny, width);
            if (closed[n_i]) {
                continue;
            }

            const float step = diagonal ? kDiagCost : kOrthoCost;
            const float tentative_g = g_score[cur_i] + step;
            if (tentative_g >= g_score[n_i]) {
                continue;
            }

            came_from[n_i] = cur_i;
            g_score[n_i] = tentative_g;
            const float f = tentative_g + OctileDistance(nx, ny, goal.x, goal.y);
            open.push({f, tentative_g, nx, ny});
        }
    }

    // Goal unreachable (or capped): path to the closest explored fitting cell.
    if (best_i == start_i) {
        return {start};
    }
    return ReconstructPath(came_from, best_i, width);
}

} // namespace

bool HasLineOfSight(Vec2i start, Vec2i end, const PathingGrid& pathing, int32_t diameter) {
    return ClampMoveDestination(start, end, pathing, diameter) == end;
}

PathFindResult FindMoveWaypoints(
    Vec2i start,
    Vec2i requested_goal,
    const PathingGrid& pathing,
    int32_t diameter) {
    PathFindResult result;
    if (start == requested_goal) {
        result.reached_requested = true;
        return result;
    }

    Vec2i goal = requested_goal;
    if (!FitsDiameter(pathing, goal.x, goal.y, diameter)) {
        goal = FindNearestFittingCell(requested_goal, pathing, diameter);
    }

    if (start == goal) {
        result.reached_requested = (goal == requested_goal);
        return result;
    }

    // Fast path: one straight segment, no A*.
    if (HasLineOfSight(start, goal, pathing, diameter)) {
        result.waypoints.push_back(goal);
        result.reached_requested = (goal == requested_goal);
        return result;
    }

    std::vector<Vec2i> path = RunAStar(start, goal, pathing, diameter);
    if (path.size() <= 1) {
        result.reached_requested = false;
        return result;
    }

    path = CompressPath(path, pathing, diameter);
    result.reached_requested = (!path.empty() && path.back() == requested_goal);

    // Destinations only.
    for (size_t i = 1; i < path.size(); ++i) {
        result.waypoints.push_back(path[i]);
    }
    return result;
}

} // namespace SimRTS
