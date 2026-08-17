#include "DetMath.h"

namespace SimRTS {

uint64_t IsqrtU64(uint64_t n) {
    if (n == 0) {
        return 0;
    }
    uint64_t lo = 0;
    uint64_t hi = 1ull << 32;
    while (lo + 1 < hi) {
        const uint64_t mid = lo + (hi - lo) / 2;
        // mid*mid <= n without overflowing uint64.
        if (mid <= n / mid) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

int64_t DivRoundNearest(int64_t numer, int64_t denom) {
    if (denom == 0) {
        return 0;
    }
    if (denom < 0) {
        numer = -numer;
        denom = -denom;
    }
    int64_t q = numer / denom;
    const int64_t r = numer % denom;
    if (r >= 0) {
        if (2 * r >= denom) {
            ++q;
        }
    } else {
        if (-2 * r >= denom) {
            --q;
        }
    }
    return q;
}

} // namespace SimRTS
