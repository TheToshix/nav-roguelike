// SPDX-License-Identifier: MIT
#include "nav/pathfind.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <queue>
#include <vector>

namespace nav {
namespace {

struct OpenNode {
    int f{0};
    int g{0};
    Vec2 pos{};
    /// Ties are broken by position, never by pointer or insertion order, so
    /// the same inputs always yield the same path on every platform.
    bool operator>(const OpenNode& o) const {
        if (f != o.f) return f > o.f;
        if (g != o.g) return g < o.g;   // prefer the node that is further along
        return o.pos < pos;
    }
};

constexpr int kNoCell = -1;
constexpr int kInfinity = std::numeric_limits<int>::max();

}  // namespace

std::vector<Vec2> find_path(const Map& map, Vec2 start, Vec2 goal,
                            const std::function<bool(Vec2)>& passable, int max_nodes) {
    std::vector<Vec2> path;
    if (start == goal || !map.in_bounds(goal) || !map.in_bounds(start)) return path;

    // Both bookkeeping tables are flat arrays over the grid rather than
    // `std::map<Vec2, …>`. A search that expands a few thousand cells did a
    // red-black tree insert and an allocation per neighbour, which put this
    // function at the top of the profile on the lower floors. The grid is
    // dense and its size is known, so the natural index is the cell itself.
    const int w = map.width(), h = map.height();
    const auto index = [w](Vec2 p) { return static_cast<std::size_t>(p.y) * static_cast<std::size_t>(w) +
                                            static_cast<std::size_t>(p.x); };
    const std::size_t cells = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    std::vector<int> g_score(cells, kInfinity);
    std::vector<int> came_from(cells, kNoCell);

    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;
    open.push({chebyshev(start, goal), 0, start});
    g_score[index(start)] = 0;

    int expanded = 0;
    while (!open.empty() && expanded < max_nodes) {
        const OpenNode cur = open.top();
        open.pop();

        // Stale entry: a cheaper route to this cell was found after it was queued.
        if (cur.g > g_score[index(cur.pos)]) continue;
        ++expanded;

        if (cur.pos == goal) {
            for (Vec2 p = goal; p != start;) {
                path.push_back(p);
                const int from = came_from[index(p)];
                // Cannot happen while the search is correct; bailing out beats
                // walking off the end of a chain that was supposed to lead home.
                if (from == kNoCell) return {};
                p = Vec2{from % w, from / w};
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        for (Vec2 d : directions8()) {
            const Vec2 nxt = cur.pos + d;
            // Bounds first: `passable` is supplied by the caller and is not
            // obliged to be a bounds check, but the tables below are indexed
            // by cell and very much are.
            if (!map.in_bounds(nxt)) continue;
            // The goal itself is always enterable, even when occupied — that is
            // what makes "path to the hero" work while the hero stands there.
            if (nxt != goal && !passable(nxt)) continue;
            if (nxt == goal && !map.walkable(nxt)) continue;

            const int cost = cur.g + 1;
            if (cost >= g_score[index(nxt)]) continue;

            g_score[index(nxt)] = cost;
            came_from[index(nxt)] = static_cast<int>(index(cur.pos));
            open.push({cost + chebyshev(nxt, goal), cost, nxt});
        }
    }
    return path;  // empty: unreachable, or the node budget ran out
}

std::vector<Vec2> find_path(const Map& map, Vec2 start, Vec2 goal, int max_nodes) {
    return find_path(map, start, goal, [&map](Vec2 p) { return map.walkable(p); }, max_nodes);
}

void DijkstraMap::build(const Map& map, const std::vector<Vec2>& sources,
                        const std::function<bool(Vec2)>& passable) {
    w_ = map.width();
    h_ = map.height();
    cost_.assign(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_), kUnreachable);

    auto index = [this](Vec2 p) {
        return static_cast<std::size_t>(p.y) * static_cast<std::size_t>(w_) +
               static_cast<std::size_t>(p.x);
    };

    // Uniform step cost, so a plain BFS gives the exact distances.
    std::deque<Vec2> frontier;
    for (Vec2 s : sources) {
        if (!map.in_bounds(s)) continue;
        cost_[index(s)] = 0;
        frontier.push_back(s);
    }

    while (!frontier.empty()) {
        const Vec2 cur = frontier.front();
        frontier.pop_front();
        const int next_cost = cost_[index(cur)] + 1;

        for (Vec2 d : directions8()) {
            const Vec2 nxt = cur + d;
            if (!map.in_bounds(nxt) || !passable(nxt)) continue;
            if (cost_[index(nxt)] <= next_cost) continue;
            cost_[index(nxt)] = next_cost;
            frontier.push_back(nxt);
        }
    }
}

void DijkstraMap::build(const Map& map, const std::vector<Vec2>& sources) {
    build(map, sources, [&map](Vec2 p) { return map.walkable(p); });
}

Vec2 DijkstraMap::best_step(Vec2 from, const std::function<bool(Vec2)>& passable,
                            bool descend) const {
    Vec2 best = from;
    int best_cost = at(from);
    if (best_cost >= kUnreachable) return from;

    for (Vec2 d : directions8()) {
        const Vec2 nxt = from + d;
        if (!passable(nxt)) continue;
        const int c = at(nxt);
        if (c >= kUnreachable) continue;
        if (descend ? (c < best_cost) : (c > best_cost)) { best_cost = c; best = nxt; }
    }
    return best;
}

}  // namespace nav
