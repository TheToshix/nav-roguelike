// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <vector>

#include "nav/geometry.hpp"
#include "nav/map.hpp"

namespace nav {

/// Recursive shadowcasting over the eight octants.
///
/// `is_transparent` decides what blocks light and `mark_visible` receives every
/// lit cell. Taking both as callbacks keeps the algorithm independent of Map,
/// which is what lets the tests drive it with hand-drawn grids.
void compute_fov(Vec2 origin, int radius,
                 const std::function<bool(Vec2)>& is_transparent,
                 const std::function<void(Vec2)>& mark_visible);

/// Convenience overload: recomputes `map`'s visible set around `origin`.
void compute_fov(Map& map, Vec2 origin, int radius);

/// True when an unobstructed straight line connects the two cells. Used for
/// ranged attacks and spell targeting, where the FOV set is too coarse.
bool has_line_of_sight(const Map& map, Vec2 from, Vec2 to, int max_range = 64);

/// The cells a Bresenham line passes through, `from` excluded, `to` included.
std::vector<Vec2> line(Vec2 from, Vec2 to);

}  // namespace nav
