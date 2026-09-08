// SPDX-License-Identifier: MIT
#include "nav/pathfind.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <queue>

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

}  // namespace

std::vector<Vec2> find_path(const Map& map, Vec2 start, Vec2 goal,
                            const std::function<bool(Vec2)>& passable, int max_nodes) {
    std::vector<Vec2> path;
    if (start == goal || !map.in_bounds(goal)) return path;

    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;
    std::map<Vec2, int> g_score;
    std::map<Vec2, Vec2> came_from;

    open.push({chebyshev(start, goal), 0, start});
    g_score[start] = 0;

    int expanded = 0;
    while (!open.empty() && expanded < max_nodes) {
        const OpenNode cur = open.top();
        open.pop();

        // Stale entry: a cheaper route to this cell was found after it was queued.
        const auto known = g_score.find(cur.pos);
        if (known == g_score.end() || cur.g > known->second) continue;
        ++expanded;

        if (cur.pos == goal) {
            for (Vec2 p = goal; p != start; p = came_from[p]) path.push_back(p);
            std::reverse(path.begin(), path.end());
            return path;
        }

        for (Vec2 d : directions8()) {
            const Vec2 nxt = cur.pos + d;
            // The goal itself is always enterable, even when occupied — that is
            // what makes "path to the hero" work while the hero stands there.
            if (nxt != goal && !passable(nxt)) continue;
            if (nxt == goal && !map.walkable(nxt)) continue;

            const int cost = cur.g + 1;
            const auto it = g_score.find(nxt);
            if (it != g_score.end() && cost >= it->second) continue;

            g_score[nxt] = cost;
            came_from[nxt] = cur.pos;
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
