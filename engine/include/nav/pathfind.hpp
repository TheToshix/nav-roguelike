// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <vector>

#include "nav/geometry.hpp"
#include "nav/map.hpp"

namespace nav {

/// A* over the eight-way grid.
///
/// Returns the cells from `start` (excluded) to `goal` (included), or an empty
/// vector when no route exists. `passable` lets the caller exclude cells that
/// are walkable on the map but occupied by another creature.
std::vector<Vec2> find_path(const Map& map, Vec2 start, Vec2 goal,
                            const std::function<bool(Vec2)>& passable,
                            int max_nodes = 4000);

/// Convenience overload: terrain only.
std::vector<Vec2> find_path(const Map& map, Vec2 start, Vec2 goal, int max_nodes = 4000);

/// A Dijkstra ("influence") map: the step distance from every cell to the
/// nearest source. Unreachable cells hold `kUnreachable`.
///
/// Monsters descend it to approach the hero and ascend it to flee, which is
/// far cheaper than running A* per monster per turn.
class DijkstraMap {
public:
    static constexpr int kUnreachable = 1 << 29;

    void build(const Map& map, const std::vector<Vec2>& sources,
               const std::function<bool(Vec2)>& passable);
    void build(const Map& map, const std::vector<Vec2>& sources);

    int at(Vec2 p) const {
        if (p.x < 0 || p.y < 0 || p.x >= w_ || p.y >= h_) return kUnreachable;
        return cost_[static_cast<std::size_t>(p.y) * static_cast<std::size_t>(w_) +
                     static_cast<std::size_t>(p.x)];
    }

    /// Neighbouring cell with the lowest cost (downhill), or `from` if none is
    /// better. `descend = false` walks uphill, which is how monsters flee.
    Vec2 best_step(Vec2 from, const std::function<bool(Vec2)>& passable, bool descend = true) const;

    int width() const { return w_; }
    int height() const { return h_; }

private:
    int w_{0};
    int h_{0};
    std::vector<int> cost_;
};

}  // namespace nav
