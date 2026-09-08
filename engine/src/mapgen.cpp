// SPDX-License-Identifier: MIT
#include "nav/mapgen.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>

namespace nav {
namespace {

/// One node of the binary space partition.
struct Node {
    Rect area;
    int left{-1};
    int right{-1};
    int room{-1};  ///< Index into GeneratedLevel::rooms, leaves only.
};

/// Splits `area` in two along its longer axis. Returns false when the area is
/// too small for both halves to still hold a room.
bool split(Rng& rng, const Rect& area, int min_size, Rect& a, Rect& b) {
    const bool horizontal = area.h > area.w ? true
                          : area.w > area.h ? false
                          : rng.chance(50);
    const int extent = horizontal ? area.h : area.w;
    if (extent < min_size * 2) return false;

    const int cut = rng.range(min_size, extent - min_size);
    if (horizontal) {
        a = {area.x, area.y, area.w, cut};
        b = {area.x, area.y + cut, area.w, area.h - cut};
    } else {
        a = {area.x, area.y, cut, area.h};
        b = {area.x + cut, area.y, area.w - cut, area.h};
    }
    return true;
}

void carve_room(Map& map, const Rect& r) {
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x)
            map.set({x, y}, Tile::Floor);
}

/// True for the tiles a corridor is allowed to dig through.
///
/// Chasms are included deliberately. The repair pass below tunnels to stranded
/// regions, and a tunnel that stops at the first chasm does not actually
/// connect anything — which is exactly how a chasm-heavy floor could strand a
/// staircase (see docs/BUG_REPORTS.md, NAV-002).
bool diggable(Tile t) { return t == Tile::Wall || t == Tile::Chasm; }

/// Digs a straight run, leaving stairs and rooms that are already there alone.
void carve_h(Map& map, int x1, int x2, int y) {
    for (int x = std::min(x1, x2); x <= std::max(x1, x2); ++x)
        if (diggable(map.at({x, y}))) map.set({x, y}, Tile::Floor);
}

void carve_v(Map& map, int y1, int y2, int x) {
    for (int y = std::min(y1, y2); y <= std::max(y1, y2); ++y)
        if (diggable(map.at({x, y}))) map.set({x, y}, Tile::Floor);
}

/// L-shaped corridor between two points, with the elbow on a random side.
void carve_corridor(Rng& rng, Map& map, Vec2 a, Vec2 b) {
    if (rng.chance(50)) {
        carve_h(map, a.x, b.x, a.y);
        carve_v(map, a.y, b.y, b.x);
    } else {
        carve_v(map, a.y, b.y, a.x);
        carve_h(map, a.x, b.x, b.y);
    }
}

/// Flood fill from `start`, returning the reached cells as a boolean grid.
std::vector<std::uint8_t> flood(const Map& map, Vec2 start) {
    std::vector<std::uint8_t> seen(
        static_cast<std::size_t>(map.width()) * static_cast<std::size_t>(map.height()), 0);
    if (!map.walkable(start)) return seen;

    auto index = [&map](Vec2 p) {
        return static_cast<std::size_t>(p.y) * static_cast<std::size_t>(map.width()) +
               static_cast<std::size_t>(p.x);
    };

    std::vector<Vec2> stack{start};
    seen[index(start)] = 1;
    while (!stack.empty()) {
        const Vec2 cur = stack.back();
        stack.pop_back();
        for (Vec2 d : directions4()) {
            const Vec2 nxt = cur + d;
            if (!map.walkable(nxt) || seen[index(nxt)]) continue;
            seen[index(nxt)] = 1;
            stack.push_back(nxt);
        }
    }
    return seen;
}

/// Connects every stranded walkable region back to the main one.
///
/// The corridor phase can strand a region when a later room overwrites the
/// only tunnel into an earlier one. Rather than regenerating the level (which
/// makes generation time unbounded), each stranded region is tunnelled
/// straight to its nearest reached cell. The loop is bounded by the number of
/// regions, so it always terminates.
void repair_connectivity(Rng& rng, Map& map, Vec2 start) {
    for (int guard = 0; guard < 64; ++guard) {
        const auto seen = flood(map, start);
        auto index = [&map](Vec2 p) {
            return static_cast<std::size_t>(p.y) * static_cast<std::size_t>(map.width()) +
                   static_cast<std::size_t>(p.x);
        };

        Vec2 orphan{-1, -1};
        for (int y = 0; y < map.height() && orphan.x < 0; ++y)
            for (int x = 0; x < map.width(); ++x) {
                const Vec2 p{x, y};
                if (map.walkable(p) && !seen[index(p)]) { orphan = p; break; }
            }
        if (orphan.x < 0) return;  // fully connected

        // Nearest cell that is already connected.
        Vec2 anchor{-1, -1};
        int best = 1 << 30;
        for (int y = 0; y < map.height(); ++y)
            for (int x = 0; x < map.width(); ++x) {
                const Vec2 p{x, y};
                if (!seen[index(p)]) continue;
                const int d = manhattan(p, orphan);
                if (d < best) { best = d; anchor = p; }
            }
        if (anchor.x < 0) return;  // nothing connected at all; nothing to do

        carve_corridor(rng, map, orphan, anchor);
    }
}

/// Sprinkles a hazard over cells that are surrounded by open floor, then
/// repairs any connectivity it broke.
void scatter(Rng& rng, Map& map, Tile tile, int attempts, Vec2 keep_reachable) {
    for (int i = 0; i < attempts; ++i) {
        const Vec2 p{rng.range(1, map.width() - 2), rng.range(1, map.height() - 2)};
        if (map.at(p) != Tile::Floor) continue;

        int open = 0;
        for (Vec2 d : directions4()) if (map.at(p + d) == Tile::Floor) ++open;
        if (open < 3) continue;  // avoid plugging a one-tile corridor

        map.set(p, tile);
    }
    if (tile == Tile::Chasm) repair_connectivity(rng, map, keep_reachable);
}

/// Places doors where a corridor meets a room: a floor cell with walls on one
/// axis and floor on the other.
void place_doors(Rng& rng, Map& map, int chance) {
    for (int y = 1; y < map.height() - 1; ++y) {
        for (int x = 1; x < map.width() - 1; ++x) {
            const Vec2 p{x, y};
            if (map.at(p) != Tile::Floor) continue;

            const bool vert = map.at({x, y - 1}) == Tile::Floor && map.at({x, y + 1}) == Tile::Floor &&
                              map.at({x - 1, y}) == Tile::Wall && map.at({x + 1, y}) == Tile::Wall;
            const bool horiz = map.at({x - 1, y}) == Tile::Floor && map.at({x + 1, y}) == Tile::Floor &&
                               map.at({x, y - 1}) == Tile::Wall && map.at({x, y + 1}) == Tile::Wall;
            if ((vert || horiz) && rng.chance(chance)) map.set(p, Tile::Door);
        }
    }
}

/// Counts wall neighbours in the 3x3 block around `p`, treating out-of-bounds
/// as wall so the cave never opens onto the edge of the map.
int wall_neighbours(const Map& map, Vec2 p) {
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const Vec2 q{p.x + dx, p.y + dy};
            if (!map.in_bounds(q) || map.at(q) == Tile::Wall) ++count;
        }
    return count;
}

/// Cellular-automaton cave: random fill, then smoothing.
///
/// Unlike the BSP generator this produces many disconnected pockets, and
/// tunnelling to each of them with the repair pass would leave the ruler-straight
/// corridors a cave is supposed to be free of. Instead the largest region is
/// kept and everything else is filled back in — the result is one organic cave,
/// connected by construction.
void carve_caves(Rng& rng, Map& map, const MapGenConfig& cfg) {
    for (int y = 1; y < map.height() - 1; ++y)
        for (int x = 1; x < map.width() - 1; ++x)
            map.set({x, y}, rng.chance(cfg.cave_fill) ? Tile::Wall : Tile::Floor);

    for (int pass = 0; pass < cfg.cave_passes; ++pass) {
        // The first passes also fill cells that have almost no wall around
        // them. Without that rule the automaton smooths the whole level into
        // one open cavern; with it, pillars and alcoves survive and the cave
        // has somewhere to hide.
        const bool seed_pillars = pass < 2;
        Map next = map;
        for (int y = 1; y < map.height() - 1; ++y)
            for (int x = 1; x < map.width() - 1; ++x) {
                const int neighbours = wall_neighbours(map, {x, y});
                const bool wall = neighbours >= 5 || (seed_pillars && neighbours <= 1);
                next.set({x, y}, wall ? Tile::Wall : Tile::Floor);
            }
        map = std::move(next);
    }
}

/// Fills in every region except the largest, and returns one cell of the one
/// that survived.
Vec2 keep_largest_region(Map& map) {
    std::vector<std::uint8_t> visited(
        static_cast<std::size_t>(map.width()) * static_cast<std::size_t>(map.height()), 0);
    auto index = [&map](Vec2 p) {
        return static_cast<std::size_t>(p.y) * static_cast<std::size_t>(map.width()) +
               static_cast<std::size_t>(p.x);
    };

    std::vector<Vec2> best;
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            const Vec2 origin{x, y};
            if (!map.walkable(origin) || visited[index(origin)]) continue;

            std::vector<Vec2> region{origin};
            std::vector<Vec2> stack{origin};
            visited[index(origin)] = 1;
            while (!stack.empty()) {
                const Vec2 cur = stack.back();
                stack.pop_back();
                for (Vec2 d : directions4()) {
                    const Vec2 nxt = cur + d;
                    if (!map.walkable(nxt) || visited[index(nxt)]) continue;
                    visited[index(nxt)] = 1;
                    region.push_back(nxt);
                    stack.push_back(nxt);
                }
            }
            if (region.size() > best.size()) best = std::move(region);
        }
    }

    if (best.empty()) return {-1, -1};

    std::vector<std::uint8_t> keep(visited.size(), 0);
    for (Vec2 p : best) keep[index(p)] = 1;
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            if (map.walkable({x, y}) && !keep[index({x, y})]) map.set({x, y}, Tile::Wall);

    return best.front();
}

/// Furthest walkable cell from `start`, by step count. Two sweeps of this find
/// the two ends of a cave, which is where the staircases belong.
Vec2 furthest_from(const Map& map, Vec2 start) {
    std::vector<int> dist(
        static_cast<std::size_t>(map.width()) * static_cast<std::size_t>(map.height()), -1);
    auto index = [&map](Vec2 p) {
        return static_cast<std::size_t>(p.y) * static_cast<std::size_t>(map.width()) +
               static_cast<std::size_t>(p.x);
    };

    std::deque<Vec2> frontier{start};
    dist[index(start)] = 0;
    Vec2 best = start;
    while (!frontier.empty()) {
        const Vec2 cur = frontier.front();
        frontier.pop_front();
        if (dist[index(cur)] > dist[index(best)]) best = cur;
        for (Vec2 d : directions4()) {
            const Vec2 nxt = cur + d;
            if (!map.walkable(nxt) || dist[index(nxt)] >= 0) continue;
            dist[index(nxt)] = dist[index(cur)] + 1;
            frontier.push_back(nxt);
        }
    }
    return best;
}

}  // namespace

int count_reachable(const Map& map, Vec2 start) {
    const auto seen = flood(map, start);
    return static_cast<int>(std::count(seen.begin(), seen.end(), std::uint8_t{1}));
}

bool is_fully_connected(const Map& map, Vec2 start) {
    const auto seen = flood(map, start);
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x) {
            const Vec2 p{x, y};
            const std::size_t i =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(map.width()) +
                static_cast<std::size_t>(x);
            if (map.walkable(p) && !seen[i]) return false;
        }
    return true;
}

GeneratedLevel generate_level(Rng& rng, const MapGenConfig& cfg, int depth) {
    GeneratedLevel out;
    out.map.resize(cfg.width, cfg.height);

    // --- Caves: a different world, carved a different way -----------------
    if (cfg.caves) {
        carve_caves(rng, out.map, cfg);
        const Vec2 seed_cell = keep_largest_region(out.map);
        if (seed_cell.x >= 0) {
            // Two sweeps of "furthest cell" land on the two ends of the cave,
            // which is where the staircases belong.
            out.entrance = furthest_from(out.map, seed_cell);
            out.exit = furthest_from(out.map, out.entrance);

            // The spawner and the altar placement both want rooms; a cave has
            // none, so a handful of open cells stand in for them.
            const auto cells = out.map.walkable_cells();
            for (int i = 0; i < 8 && !cells.empty(); ++i) {
                const Vec2 c = cells[static_cast<std::size_t>(rng.below(static_cast<int>(cells.size())))];
                out.rooms.push_back(Rect{c.x, c.y, 1, 1});
            }
        }
        // A pathological smoothing result can leave almost nothing open; fall
        // back to the room generator rather than hand back an unplayable level.
        if (out.map.walkable_cells().size() < 80) {
            out.map.resize(cfg.width, cfg.height);
            out.rooms.clear();
            MapGenConfig fallback = cfg;
            fallback.caves = false;
            return generate_level(rng, fallback, depth);
        }
    }

    if (!cfg.caves) {
    // --- 1. Partition the level -------------------------------------------
    std::vector<Node> nodes;
    nodes.push_back(Node{Rect{1, 1, cfg.width - 2, cfg.height - 2}, -1, -1, -1});

    std::vector<int> frontier{0};
    for (int level = 0; level < cfg.max_depth; ++level) {
        std::vector<int> next;
        for (int id : frontier) {
            Rect a{}, b{};
            if (!split(rng, nodes[id].area, cfg.min_room + 1, a, b)) continue;
            nodes[id].left = static_cast<int>(nodes.size());
            nodes.push_back(Node{a, -1, -1, -1});
            nodes[id].right = static_cast<int>(nodes.size());
            nodes.push_back(Node{b, -1, -1, -1});
            next.push_back(nodes[id].left);
            next.push_back(nodes[id].right);
        }
        if (next.empty()) break;
        frontier = std::move(next);
    }

    // --- 2. Carve a room inside every leaf --------------------------------
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].left != -1) continue;  // internal node
        const Rect& area = nodes[i].area;
        const int w = rng.range(std::min(cfg.min_room - 2, area.w - 2), std::max(cfg.min_room - 2, area.w - 2));
        const int h = rng.range(std::min(cfg.min_room - 2, area.h - 2), std::max(cfg.min_room - 2, area.h - 2));
        if (w < 3 || h < 3) continue;

        const Rect room{area.x + rng.below(area.w - w), area.y + rng.below(area.h - h), w, h};
        nodes[i].room = static_cast<int>(out.rooms.size());
        out.rooms.push_back(room);
        carve_room(out.map, room);
    }

    if (out.rooms.empty()) {  // degenerate configuration: fall back to one big room
        const Rect room{1, 1, cfg.width - 2, cfg.height - 2};
        out.rooms.push_back(room);
        carve_room(out.map, room);
    }

    // --- 3. Connect sibling subtrees --------------------------------------
    // Each internal node joins one room from its left subtree to one from its
    // right, which yields a spanning tree over the rooms by construction.
    std::function<int(int)> any_room = [&](int id) -> int {
        if (id < 0) return -1;
        if (nodes[id].room >= 0) return nodes[id].room;
        const int l = any_room(nodes[id].left);
        if (l >= 0) return l;
        return any_room(nodes[id].right);
    };

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].left == -1) continue;
        const int a = any_room(nodes[i].left);
        const int b = any_room(nodes[i].right);
        if (a < 0 || b < 0) continue;
        carve_corridor(rng, out.map, out.rooms[static_cast<std::size_t>(a)].center(),
                       out.rooms[static_cast<std::size_t>(b)].center());
    }

    // A few extra loops so the level is not a pure tree — dead ends everywhere
    // make a roguelike tedious and predictable.
    const int extra = 1 + static_cast<int>(out.rooms.size()) / 6;
    for (int i = 0; i < extra && out.rooms.size() > 1; ++i) {
        const int a = rng.below(static_cast<int>(out.rooms.size()));
        const int b = rng.below(static_cast<int>(out.rooms.size()));
        if (a != b) carve_corridor(rng, out.map, out.rooms[static_cast<std::size_t>(a)].center(),
                                   out.rooms[static_cast<std::size_t>(b)].center());
    }

    // --- 4. Stairs --------------------------------------------------------
    // Entrance and exit go in the two rooms furthest apart, so a level is never
    // finished in three steps.
    std::size_t best_a = 0, best_b = out.rooms.size() > 1 ? 1 : 0;
    int best_d = -1;
    for (std::size_t i = 0; i < out.rooms.size(); ++i)
        for (std::size_t j = i + 1; j < out.rooms.size(); ++j) {
            const int d = dist_sq(out.rooms[i].center(), out.rooms[j].center());
            if (d > best_d) { best_d = d; best_a = i; best_b = j; }
        }

    out.entrance = out.rooms[best_a].center();
    out.exit = out.rooms[best_b].center();
    if (out.entrance == out.exit) out.exit = out.exit + Vec2{1, 0};

    }

    repair_connectivity(rng, out.map, out.entrance);

    // --- 5. Decoration ----------------------------------------------------
    if (rng.chance(cfg.water_chance))
        scatter(rng, out.map, Tile::Water, 8 + depth * 2, out.entrance);
    if (depth >= 3 && rng.chance(cfg.chasm_chance))
        scatter(rng, out.map, Tile::Chasm, 6 + depth, out.entrance);

    if (cfg.door_chance > 0) place_doors(rng, out.map, cfg.door_chance);

    if (cfg.place_altar && out.rooms.size() > 2) {
        const Rect& r = out.rooms[static_cast<std::size_t>(rng.below(static_cast<int>(out.rooms.size())))];
        const Vec2 spot = r.center();
        if (spot != out.entrance && spot != out.exit) out.map.set(spot, Tile::Altar);
    }

    // Stairs are written last so nothing can overwrite them.
    out.map.set(out.exit, Tile::StairsDown);
    if (cfg.place_stairs_up) out.map.set(out.entrance, Tile::StairsUp);
    else out.map.set(out.entrance, Tile::Floor);

    // Doors and hazards can only ever remove connectivity, so verify once more.
    repair_connectivity(rng, out.map, out.entrance);
    return out;
}

}  // namespace nav
