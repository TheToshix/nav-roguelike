// SPDX-License-Identifier: MIT
//
// Symmetric shadowcasting.
//
// The first implementation here was the classic recursive shadowcasting most
// roguelikes use. It is fast and looks right, but it is not symmetric: there
// are cells from which the hero is visible while the hero cannot see back. In
// a game where monsters shoot from range that reads as the game cheating, and
// the FOV symmetry test caught it immediately.
//
// This is Albert Ford's symmetric formulation instead. A floor tile is lit
// only when its centre lies inside the visible wedge, which makes visibility a
// symmetric relation between floor tiles. Walls are lit whenever any part of
// them is touched, so walls still look solid rather than gap-toothed.
#include "nav/fov.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace nav {
namespace {

enum Cardinal { kNorth = 0, kSouth, kEast, kWest };

/// Maps (row, col) in a quadrant's local frame onto the map.
Vec2 transform(Vec2 origin, int cardinal, int row, int col) {
    switch (cardinal) {
        case kNorth: return {origin.x + col, origin.y - row};
        case kSouth: return {origin.x + col, origin.y + row};
        case kEast:  return {origin.x + row, origin.y + col};
        default:     return {origin.x - row, origin.y + col};
    }
}

/// The slope of the leading edge of the tile at (row, col).
double edge_slope(int row, int col) {
    return (2.0 * col - 1.0) / (2.0 * row);
}

/// True when the tile's centre is inside the wedge — the condition that makes
/// the relation symmetric.
bool is_symmetric(int row, int col, double start, double end) {
    return col >= row * start && col <= row * end;
}

int round_up(double v) { return static_cast<int>(std::floor(v + 0.5)); }
int round_down(double v) { return static_cast<int>(std::ceil(v - 0.5)); }

struct Caster {
    Vec2 origin;
    int radius;
    int radius_sq;
    const std::function<bool(Vec2)>* transparent;
    const std::function<void(Vec2)>* mark;

    void scan(int cardinal, int row, double start, double end) {
        if (row > radius || start > end) return;

        const int min_col = round_up(row * start);
        const int max_col = round_down(row * end);

        bool have_previous = false;
        bool previous_was_wall = false;

        for (int col = min_col; col <= max_col; ++col) {
            const Vec2 p = transform(origin, cardinal, row, col);
            const bool wall = !(*transparent)(p);

            // Walls are drawn whenever the wedge touches them; floor is only
            // drawn when its centre is inside the wedge.
            if ((wall || is_symmetric(row, col, start, end)) &&
                row * row + col * col <= radius_sq) {
                (*mark)(p);
            }

            if (have_previous && previous_was_wall && !wall) start = edge_slope(row, col);
            if (have_previous && !previous_was_wall && wall)
                scan(cardinal, row + 1, start, edge_slope(row, col));

            have_previous = true;
            previous_was_wall = wall;
        }

        // The row ended on open floor, so the wedge continues unbroken.
        if (have_previous && !previous_was_wall) scan(cardinal, row + 1, start, end);
    }
};

}  // namespace

void compute_fov(Vec2 origin, int radius,
                 const std::function<bool(Vec2)>& is_transparent,
                 const std::function<void(Vec2)>& mark_visible) {
    mark_visible(origin);  // the observer always sees its own cell
    if (radius <= 0) return;

    Caster caster{origin, radius, radius * radius, &is_transparent, &mark_visible};
    for (int cardinal = 0; cardinal < 4; ++cardinal) caster.scan(cardinal, 1, -1.0, 1.0);
}

void compute_fov(Map& map, Vec2 origin, int radius) {
    map.clear_visible();
    compute_fov(origin, radius,
                [&map](Vec2 p) { return map.transparent(p); },
                [&map](Vec2 p) { map.set_visible(p, true); });
}

std::vector<Vec2> line(Vec2 from, Vec2 to) {
    // Bresenham, with the origin excluded and the destination included.
    std::vector<Vec2> out;
    int x = from.x, y = from.y;
    const int dx = std::abs(to.x - x), sx = x < to.x ? 1 : -1;
    const int dy = -std::abs(to.y - y), sy = y < to.y ? 1 : -1;
    int err = dx + dy;

    // Guard against a pathological call producing an unbounded loop.
    const int limit = dx - dy + 2;
    for (int guard = 0; guard <= limit; ++guard) {
        if (x == to.x && y == to.y) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
        out.push_back({x, y});
    }
    return out;
}

bool has_line_of_sight(const Map& map, Vec2 from, Vec2 to, int max_range) {
    if (from == to) return true;
    if (chebyshev(from, to) > max_range) return false;
    for (Vec2 p : line(from, to)) {
        if (p == to) return true;
        if (!map.transparent(p)) return false;
    }
    return true;
}

}  // namespace nav
