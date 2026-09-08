// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>

namespace nav {

struct Vec2 {
    int x{0};
    int y{0};

    friend bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
    friend bool operator!=(Vec2 a, Vec2 b) { return !(a == b); }
    friend Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
    friend Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
    /// Deterministic ordering so Vec2 can key a std::map.
    friend bool operator<(Vec2 a, Vec2 b) { return a.y != b.y ? a.y < b.y : a.x < b.x; }
};

/// Chebyshev distance — the true step count on a grid with diagonal movement.
inline int chebyshev(Vec2 a, Vec2 b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}

/// Manhattan distance, used where diagonals must cost extra.
inline int manhattan(Vec2 a, Vec2 b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

/// Squared euclidean distance — avoids a sqrt when only comparing distances.
inline int dist_sq(Vec2 a, Vec2 b) {
    const int dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

/// The eight grid neighbours, in a fixed order so iteration is reproducible.
inline const std::array<Vec2, 8>& directions8() {
    static const std::array<Vec2, 8> dirs{{{0, -1}, {1, -1}, {1, 0}, {1, 1},
                                           {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}};
    return dirs;
}

inline const std::array<Vec2, 4>& directions4() {
    static const std::array<Vec2, 4> dirs{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
    return dirs;
}

/// Unit step from `from` towards `to` (each axis clamped to -1/0/+1).
inline Vec2 step_towards(Vec2 from, Vec2 to) {
    return {(to.x > from.x) - (to.x < from.x), (to.y > from.y) - (to.y < from.y)};
}

/// Axis-aligned rectangle. `x`/`y` are inclusive top-left, `w`/`h` are extents.
struct Rect {
    int x{0}, y{0}, w{0}, h{0};

    int left() const { return x; }
    int top() const { return y; }
    int right() const { return x + w - 1; }
    int bottom() const { return y + h - 1; }
    int area() const { return w * h; }

    Vec2 center() const { return {x + w / 2, y + h / 2}; }

    bool contains(Vec2 p) const {
        return p.x >= left() && p.x <= right() && p.y >= top() && p.y <= bottom();
    }

    /// True when the rectangles share at least one cell, after growing both by
    /// `pad`. Rooms are tested with pad = 1 so they never share a wall.
    bool intersects(const Rect& o, int pad = 0) const {
        return left() - pad <= o.right() && right() + pad >= o.left() &&
               top() - pad <= o.bottom() && bottom() + pad >= o.top();
    }

    /// The interior with `inset` cells removed on every side.
    Rect shrunk(int inset) const {
        return {x + inset, y + inset, std::max(0, w - 2 * inset), std::max(0, h - 2 * inset)};
    }
};

}  // namespace nav
