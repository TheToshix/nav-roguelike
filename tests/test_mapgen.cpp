// SPDX-License-Identifier: MIT
//
// The generator's contract is checked as a property over many seeds rather
// than against one golden level: "every floor tile is reachable" has to hold
// for every dungeon a player can ever be given, not for one example.
#include <gtest/gtest.h>

#include <set>

#include "nav/mapgen.hpp"

using namespace nav;

namespace {

MapGenConfig standard_config() {
    MapGenConfig cfg;
    cfg.width = 72;
    cfg.height = 34;
    return cfg;
}

int count_tiles(const Map& map, Tile t) {
    int n = 0;
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            if (map.at({x, y}) == t) ++n;
    return n;
}

}  // namespace

TEST(MapGen, EveryWalkableCellIsReachableFromTheEntrance) {
    // The core invariant. A single unreachable pocket can hide the staircase
    // and strand the player, so this runs over a wide sweep of seeds.
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        Rng rng(seed);
        const int depth = 1 + static_cast<int>(seed % 12);
        MapGenConfig cfg = standard_config();
        cfg.place_altar = (depth % 3 == 0);
        cfg.place_stairs_up = depth > 1;

        const GeneratedLevel level = generate_level(rng, cfg, depth);
        ASSERT_TRUE(is_fully_connected(level.map, level.entrance))
            << "seed " << seed << ", depth " << depth << " produced an unreachable region";
    }
}

TEST(MapGen, StairsAlwaysExistAreWalkableAndDistinct) {
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        Rng rng(seed ^ 0xF00DULL);
        MapGenConfig cfg = standard_config();
        const GeneratedLevel level = generate_level(rng, cfg, 3);

        ASSERT_NE(level.entrance, level.exit) << "seed " << seed;
        ASSERT_TRUE(level.map.walkable(level.entrance)) << "seed " << seed;
        ASSERT_TRUE(level.map.walkable(level.exit)) << "seed " << seed;
        ASSERT_EQ(level.map.at(level.exit), Tile::StairsDown) << "seed " << seed;
        ASSERT_EQ(level.map.at(level.entrance), Tile::StairsUp) << "seed " << seed;
        ASSERT_EQ(count_tiles(level.map, Tile::StairsDown), 1) << "seed " << seed;
    }
}

TEST(MapGen, FirstFloorHasNoWayBackUp) {
    Rng rng(4242);
    MapGenConfig cfg = standard_config();
    cfg.place_stairs_up = false;
    const GeneratedLevel level = generate_level(rng, cfg, 1);
    EXPECT_EQ(count_tiles(level.map, Tile::StairsUp), 0);
    EXPECT_TRUE(level.map.walkable(level.entrance));
}

TEST(MapGen, SameSeedProducesAnIdenticalLevel) {
    MapGenConfig cfg = standard_config();
    Rng a(777), b(777);
    const GeneratedLevel first = generate_level(a, cfg, 5);
    const GeneratedLevel second = generate_level(b, cfg, 5);

    ASSERT_EQ(first.map.raw_tiles(), second.map.raw_tiles());
    EXPECT_EQ(first.entrance, second.entrance);
    EXPECT_EQ(first.exit, second.exit);
    EXPECT_EQ(first.rooms.size(), second.rooms.size());
}

TEST(MapGen, DifferentSeedsProduceDifferentLevels) {
    MapGenConfig cfg = standard_config();
    Rng a(1), b(2);
    const GeneratedLevel first = generate_level(a, cfg, 5);
    const GeneratedLevel second = generate_level(b, cfg, 5);
    EXPECT_NE(first.map.raw_tiles(), second.map.raw_tiles());
}

TEST(MapGen, RespectsTheRequestedDimensionsAndKeepsABorder) {
    MapGenConfig cfg = standard_config();
    cfg.width = 40;
    cfg.height = 24;
    Rng rng(9);
    const GeneratedLevel level = generate_level(rng, cfg, 2);

    ASSERT_EQ(level.map.width(), 40);
    ASSERT_EQ(level.map.height(), 24);

    // The outer ring must stay solid, or the player can walk off the map.
    for (int x = 0; x < level.map.width(); ++x) {
        EXPECT_FALSE(level.map.walkable({x, 0}));
        EXPECT_FALSE(level.map.walkable({x, level.map.height() - 1}));
    }
    for (int y = 0; y < level.map.height(); ++y) {
        EXPECT_FALSE(level.map.walkable({0, y}));
        EXPECT_FALSE(level.map.walkable({level.map.width() - 1, y}));
    }
}

TEST(MapGen, ProducesAPlayableAmountOfOpenSpace) {
    MapGenConfig cfg = standard_config();
    for (std::uint64_t seed = 0; seed < 60; ++seed) {
        Rng rng(seed + 500);
        const GeneratedLevel level = generate_level(rng, cfg, 4);
        const int open = static_cast<int>(level.map.walkable_cells().size());
        const int total = level.map.width() * level.map.height();
        // Too few cells means a level of corridors; too many means one big hall.
        EXPECT_GT(open, total / 12) << "seed " << seed << " produced a cramped level";
        EXPECT_LT(open, total * 3 / 4) << "seed " << seed << " produced an empty level";
    }
}

TEST(MapGen, GeneratesAtLeastTwoRooms) {
    MapGenConfig cfg = standard_config();
    for (std::uint64_t seed = 0; seed < 60; ++seed) {
        Rng rng(seed + 900);
        const GeneratedLevel level = generate_level(rng, cfg, 1);
        EXPECT_GE(level.rooms.size(), 2u) << "seed " << seed;
    }
}

TEST(MapGen, AltarIsPlacedOnlyWhenRequested) {
    MapGenConfig cfg = standard_config();
    cfg.place_altar = false;
    Rng off(31);
    EXPECT_EQ(count_tiles(generate_level(off, cfg, 3).map, Tile::Altar), 0);

    cfg.place_altar = true;
    int with_altar = 0;
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        Rng rng(seed + 60);
        if (count_tiles(generate_level(rng, cfg, 3).map, Tile::Altar) > 0) ++with_altar;
    }
    EXPECT_GT(with_altar, 20) << "altars were requested but hardly ever appeared";
}

TEST(MapGen, ChasmsNeverStrandPartOfTheLevel) {
    // Chasms are impassable, so this is the case most likely to break the
    // connectivity invariant. Deep floors get the most of them.
    MapGenConfig cfg = standard_config();
    cfg.chasm_chance = 100;
    for (std::uint64_t seed = 0; seed < 120; ++seed) {
        Rng rng(seed + 1200);
        const GeneratedLevel level = generate_level(rng, cfg, 12);
        ASSERT_TRUE(is_fully_connected(level.map, level.entrance)) << "seed " << seed;
    }
}

TEST(MapGen, CountReachableAgreesWithTheWalkableCellCount) {
    MapGenConfig cfg = standard_config();
    Rng rng(2468);
    const GeneratedLevel level = generate_level(rng, cfg, 6);
    EXPECT_EQ(count_reachable(level.map, level.entrance),
              static_cast<int>(level.map.walkable_cells().size()));
}

TEST(MapGen, CountReachableIsZeroFromInsideAWall) {
    Map map(10, 10);  // all wall
    EXPECT_EQ(count_reachable(map, {5, 5}), 0);
}

// --- Map primitives --------------------------------------------------------

TEST(MapBasics, OutOfBoundsReadsAsSolidWall) {
    Map map(8, 8);
    map.set({3, 3}, Tile::Floor);
    EXPECT_FALSE(map.walkable({-1, 3}));
    EXPECT_FALSE(map.walkable({3, 99}));
    EXPECT_FALSE(map.transparent({-5, -5}));
    EXPECT_EQ(map.at({100, 100}), Tile::Wall);
}

TEST(MapBasics, ClosedDoorsBlockSightButNotMovement) {
    Map map(8, 8);
    map.set({4, 4}, Tile::Door);
    EXPECT_TRUE(map.walkable({4, 4})) << "a closed door must be enterable, which opens it";
    EXPECT_FALSE(map.transparent({4, 4}));
}

TEST(MapBasics, ChasmsBlockMovementButNotSight) {
    Map map(8, 8);
    map.set({2, 2}, Tile::Chasm);
    EXPECT_FALSE(map.walkable({2, 2}));
    EXPECT_TRUE(map.transparent({2, 2}));
}

TEST(MapBasics, SeeingACellMarksItExploredForever) {
    Map map(8, 8);
    map.set({1, 1}, Tile::Floor);
    EXPECT_FALSE(map.explored({1, 1}));

    map.set_visible({1, 1}, true);
    EXPECT_TRUE(map.visible({1, 1}));
    EXPECT_TRUE(map.explored({1, 1}));

    map.clear_visible();
    EXPECT_FALSE(map.visible({1, 1}));
    EXPECT_TRUE(map.explored({1, 1})) << "memory of a cell must survive losing sight of it";
}

TEST(MapBasics, RevealAllMarksTheWholeLevelExplored) {
    Map map(6, 6);
    map.reveal_all();
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 6; ++x) EXPECT_TRUE(map.explored({x, y}));
}
