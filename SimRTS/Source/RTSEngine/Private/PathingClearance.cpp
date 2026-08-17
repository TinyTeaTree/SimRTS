#include "PathingClearance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace SimRTS {

namespace {

constexpr int64_t kInfD2 = 1LL << 50;

int32_t FloorSqrt(int64_t value) {
    if (value <= 0) {
        return 0;
    }
    // Largest int32 r with r*r <= value (value fits world diagonals).
    int64_t lo = 0;
    int64_t hi = static_cast<int64_t>(std::sqrt(static_cast<double>(value))) + 2;
    while (lo + 1 < hi) {
        const int64_t mid = lo + (hi - lo) / 2;
        if (mid * mid <= value) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return static_cast<int32_t>(lo);
}

// 1D squared Euclidean DT (Felzenszwalb & Huttenlocher).
void DistanceTransform1D(const int64_t* f, int64_t* d, int32_t n, std::vector<int32_t>& v, std::vector<double>& z) {
    v.resize(n);
    z.resize(n + 1);

    int32_t k = 0;
    v[0] = 0;
    z[0] = -std::numeric_limits<double>::infinity();
    z[1] = std::numeric_limits<double>::infinity();

    for (int32_t q = 1; q < n; ++q) {
        double s = 0.0;
        while (true) {
            const int32_t r = v[k];
            s = (static_cast<double>(f[q]) + static_cast<double>(q) * q
                 - (static_cast<double>(f[r]) + static_cast<double>(r) * r))
                / (2.0 * q - 2.0 * r);
            if (s > z[k]) {
                break;
            }
            --k;
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = std::numeric_limits<double>::infinity();
    }

    k = 0;
    for (int32_t q = 0; q < n; ++q) {
        while (z[k + 1] < q) {
            ++k;
        }
        const int32_t r = v[k];
        const int64_t dq = q - r;
        d[q] = dq * dq + f[r];
    }
}

} // namespace

void ComputeObstructionDistances(PathingGrid& pathing) {
    const int32_t width = pathing.width;
    const int32_t height = pathing.height;
    if (width <= 0 || height <= 0 || pathing.cells.empty()) {
        return;
    }

    const int32_t total = width * height;
    const int32_t open_sentinel = width + height;

    bool any_blocked = false;
    std::vector<int64_t> d2(total, kInfD2);
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            if (pathing.IsBlocked(x, y)) {
                d2[y * width + x] = 0;
                any_blocked = true;
            }
        }
    }

    if (!any_blocked) {
        for (int32_t y = 0; y < height; ++y) {
            for (int32_t x = 0; x < width; ++x) {
                pathing.At(x, y).obstruction_distance = open_sentinel;
            }
        }
        return;
    }

    std::vector<int64_t> column_f(height);
    std::vector<int64_t> column_d(height);
    std::vector<int32_t> envelope_v;
    std::vector<double> envelope_z;

    // Vertical pass per column.
    for (int32_t x = 0; x < width; ++x) {
        for (int32_t y = 0; y < height; ++y) {
            column_f[y] = d2[y * width + x];
        }
        DistanceTransform1D(column_f.data(), column_d.data(), height, envelope_v, envelope_z);
        for (int32_t y = 0; y < height; ++y) {
            d2[y * width + x] = column_d[y];
        }
    }

    std::vector<int64_t> row_f(width);
    std::vector<int64_t> row_d(width);

    // Horizontal pass per row → squared Euclidean distance to nearest blocked cell.
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            row_f[x] = d2[y * width + x];
        }
        DistanceTransform1D(row_f.data(), row_d.data(), width, envelope_v, envelope_z);
        for (int32_t x = 0; x < width; ++x) {
            // Floor of Euclidean length in points.
            pathing.At(x, y).obstruction_distance = FloorSqrt(row_d[x]);
        }
    }
}

bool FitsDiameter(const PathingGrid& pathing, int32_t x, int32_t y, int32_t diameter) {
    if (!pathing.InBounds(x, y) || pathing.cells.empty()) {
        return false;
    }
    const int32_t d = std::max(1, diameter);
    // Equivalent to obstruction_distance >= diameter/2 with integer half-points.
    return 2 * pathing.At(x, y).obstruction_distance >= d;
}

} // namespace SimRTS
