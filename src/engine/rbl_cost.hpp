#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <cstddef>
namespace rbl {
// Checked 64-bit integer arithmetic helpers (preventing signed integer UB)
inline bool checkedAdd(int64_t a, int64_t b, int64_t& out) {
    if ((b > 0 && a > std::numeric_limits<int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<int64_t>::min() - b)) {
        return false;
    }
    out = a + b;
    return true;
}

inline bool checkedSub(int64_t a, int64_t b, int64_t& out) {
    if ((b < 0 && a > std::numeric_limits<int64_t>::max() + b) ||
        (b > 0 && a < std::numeric_limits<int64_t>::min() + b)) {
        return false;
    }
    out = a - b;
    return true;
}

inline bool checkedAbs(int64_t a, int64_t& out) {
    if (a == std::numeric_limits<int64_t>::min()) {
        return false;
    }
    out = (a < 0) ? -a : a;
    return true;
}


inline bool checkedCostToInt(int64_t cost, int& out) {
    if (cost < 0 || cost > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(cost);
    return true;
}
// Complete-path arrival: step after the last non-goal position. Terminal waits
// do not contribute. This is NOT a frontier node g or an unfinished path cost.
template<class Path, class AtGoal>
int64_t authoritativeArrivalCost(const Path& path, AtGoal at_goal) {
    if (path.size() > static_cast<size_t>(std::numeric_limits<int64_t>::max()))
        throw std::overflow_error("Path length exceeds int64_t");
    int64_t arrival = 0;
    for (size_t t = 0; t < path.size(); ++t)
        if (!at_goal(path[t])) arrival = static_cast<int64_t>(t) + 1;
    return arrival;
}
template<class Paths, class Cost>
int64_t checkedSumOfCosts(const Paths& paths, Cost cost) {
    int64_t total = 0;
    for (const auto& path : paths) {
        const int64_t c = cost(path);
        if (c < 0 || !checkedAdd(total, c, total))
            throw std::overflow_error("Sum of costs overflow or negative cost");
    }
    return total;
}
} // namespace rbl
