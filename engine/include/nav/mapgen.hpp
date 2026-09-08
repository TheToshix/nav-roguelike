// SPDX-License-Identifier: MIT
#pragma once
#include <vector>

#include "nav/geometry.hpp"
#include "nav/map.hpp"
#include "nav/rng.hpp"

namespace nav {

struct MapGenConfig {
    int width{72};
    int height{34};
    int min_room{6};      ///< Smallest room side, walls included.
    int max_depth{5};     ///< BSP recursion depth; ~2^depth leaves.
    int door_chance{55};  ///< Percent chance a room/corridor junction gets a door.
    int water_chance{35}; ///< Percent chance the level has water at all.
    int chasm_chance{25};
    /// Carve the level as a cellular-automaton cave instead of BSP rooms.
    /// Caves have no doors and no straight corridors, which is what makes the
    /// middle belt of the dungeon feel like somewhere else entirely.
    bool caves{false};
    int cave_fill{48};    ///< Initial wall percentage before smoothing.
    int cave_passes{4};   ///< Smoothing iterations.
    bool place_altar{false};
    bool place_stairs_up{true};
};

/// The generated level plus the metadata the spawner needs.
struct GeneratedLevel {
    Map map;
    std::vector<Rect> rooms;   ///< Interior rectangles, in BSP order.
    Vec2 entrance{-1, -1};     ///< Where the hero arrives (StairsUp, if present).
    Vec2 exit{-1, -1};         ///< StairsDown.
};

/// Builds one dungeon level.
///
/// The result is guaranteed to satisfy two invariants, both asserted by the
/// test suite for thousands of seeds:
///   1. every walkable cell is reachable from `entrance`;
///   2. `entrance` and `exit` exist, are walkable, and are distinct.
/// Connectivity is not left to chance — a repair pass tunnels to any region
/// the corridor phase happened to strand.
GeneratedLevel generate_level(Rng& rng, const MapGenConfig& cfg, int depth);

/// Number of walkable cells reachable from `start` by 4-way movement.
int count_reachable(const Map& map, Vec2 start);

/// True when every walkable cell on the map is reachable from `start`.
bool is_fully_connected(const Map& map, Vec2 start);

}  // namespace nav
