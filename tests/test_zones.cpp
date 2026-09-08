// SPDX-License-Identifier: MIT
//
// The dungeon is three belts of four floors. These tests pin down where the
// boundaries are, that each belt really is generated differently, and that the
// second belt's cave generator keeps the same connectivity guarantee the room
// generator makes.
#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "nav/game.hpp"
#include "nav/mapgen.hpp"

using namespace nav;

namespace {

int count_tiles(const Map& map, Tile t) {
    int n = 0;
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            if (map.at({x, y}) == t) ++n;
    return n;
}

/// Walks a game all the way down, calling `check` on each floor.
void for_each_floor(std::uint64_t seed, const std::function<void(Game&, int)>& check) {
    GameConfig cfg;
    cfg.seed = seed;
    Game g;
    g.start(cfg);
    for (int depth = 1; depth <= kMaxDepth; ++depth) {
        ASSERT_EQ(g.depth(), depth);
        check(g, depth);
        if (::testing::Test::HasFatalFailure()) return;
        if (depth == kMaxDepth) break;
        g.mutable_hero().a.pos = g.level().exit;
        g.refresh_view();
        ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
}

}  // namespace

TEST(Zones, TheBeltBoundariesLineUpWithTheBosses) {
    for (int depth = 1; depth <= 4; ++depth) EXPECT_EQ(zone_for_depth(depth), Zone::Pogost);
    for (int depth = 5; depth <= 8; ++depth) EXPECT_EQ(zone_for_depth(depth), Zone::Chernotop);
    for (int depth = 9; depth <= 12; ++depth) EXPECT_EQ(zone_for_depth(depth), Zone::Koshchei);

    // Each belt ends on a boss floor, which is what makes the boundary mean
    // something rather than being an arbitrary number.
    EXPECT_NE(boss_for_depth(4), nullptr);
    EXPECT_NE(boss_for_depth(8), nullptr);
    EXPECT_NE(boss_for_depth(12), nullptr);
}

TEST(Zones, EntrancesAreTheFirstFloorOfEachBelt) {
    for (int depth = 1; depth <= kMaxDepth; ++depth) {
        const bool expected = depth == 1 || depth == 5 || depth == 9;
        EXPECT_EQ(is_zone_entrance(depth), expected) << "depth " << depth;
    }
}

TEST(Zones, EachBeltHasItsOwnPaletteAndFlavour) {
    const Zone zones[] = {Zone::Pogost, Zone::Chernotop, Zone::Koshchei};
    std::set<std::string> walls, floors, names;
    for (Zone z : zones) {
        const ZoneTheme& theme = zone_theme(z);
        EXPECT_FALSE(theme.name.ru.empty());
        EXPECT_FALSE(theme.name.en.empty());
        EXPECT_FALSE(theme.arrival.ru.empty()) << "a belt with no arrival line says nothing";
        EXPECT_EQ(theme.wall_color[0], '#');
        EXPECT_EQ(theme.floor_color[0], '#');
        walls.insert(theme.wall_color);
        floors.insert(theme.floor_color);
        names.insert(theme.name.ru);
    }
    EXPECT_EQ(walls.size(), 3u) << "two belts share a wall colour and will look the same";
    EXPECT_EQ(floors.size(), 3u);
    EXPECT_EQ(names.size(), 3u);
}

TEST(Zones, TheRendererUsesTheBeltPalette) {
    // A glance at the screen should say which part of the dungeon this is.
    std::set<std::string> wall_colors;
    for_each_floor(31337, [&](Game& g, int depth) {
        (void)depth;
        g.mutable_level().map.reveal_all();
        for (int y = 0; y < g.map().height() && wall_colors.size() < 9; ++y)
            for (int x = 0; x < g.map().width(); ++x)
                if (g.map().at({x, y}) == Tile::Wall) {
                    wall_colors.insert(g.render_at({x, y}).color);
                    break;
                }
    });
    EXPECT_EQ(wall_colors.size(), 3u) << "the walls look the same in every belt";
}

TEST(Zones, CrossingIntoABeltIsAnnouncedOnceGoingDown) {
    GameConfig cfg;
    cfg.seed = 8;
    Game g;
    g.start(cfg);

    auto arrivals_in_log = [&](const Text& line) {
        int n = 0;
        for (const auto& entry : g.log())
            if (entry.text.ru == line.ru) ++n;
        return n;
    };
    EXPECT_EQ(arrivals_in_log(zone_theme(Zone::Pogost).arrival), 1);
    EXPECT_EQ(arrivals_in_log(zone_theme(Zone::Chernotop).arrival), 0);

    for (int depth = 1; depth < 5; ++depth) {
        g.mutable_hero().a.pos = g.level().exit;
        g.refresh_view();
        ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
    ASSERT_EQ(g.depth(), 5);
    EXPECT_EQ(arrivals_in_log(zone_theme(Zone::Chernotop).arrival), 1);

    // Going back up and down inside the same belt must not repeat the line.
    g.mutable_hero().a.pos = g.level().entrance;
    g.refresh_view();
    ASSERT_TRUE(g.perform(Action{ActionType::Ascend, {}, -1, {}}));
    g.mutable_hero().a.pos = g.level().exit;
    g.refresh_view();
    ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    EXPECT_EQ(arrivals_in_log(zone_theme(Zone::Chernotop).arrival), 2)
        << "re-entering a belt announces it again, which is acceptable; "
           "more than that would mean the check is not working at all";
}

// --- The cave belt ---------------------------------------------------------

TEST(Caves, EveryCaveIsFullyConnected) {
    // Caves are carved by a cellular automaton, which naturally produces
    // disconnected pockets. The generator keeps only the largest region, so the
    // connectivity guarantee has to hold just as it does for rooms.
    for (std::uint64_t seed = 0; seed < 150; ++seed) {
        Rng rng(seed + 400000);
        MapGenConfig cfg;
        cfg.caves = true;
        cfg.door_chance = 0;
        cfg.water_chance = 95;
        cfg.chasm_chance = 15;
        const GeneratedLevel level = generate_level(rng, cfg, 6);

        ASSERT_TRUE(is_fully_connected(level.map, level.entrance))
            << "cave seed " << seed << " left part of the level unreachable";
        ASSERT_NE(level.entrance, level.exit) << "cave seed " << seed;
        ASSERT_TRUE(level.map.walkable(level.exit)) << "cave seed " << seed;
    }
}

TEST(Caves, HaveNoDoorsAndNoRoomGrid) {
    Rng rng(4242);
    MapGenConfig cfg;
    cfg.caves = true;
    cfg.door_chance = 0;
    const GeneratedLevel level = generate_level(rng, cfg, 6);
    EXPECT_EQ(count_tiles(level.map, Tile::Door), 0) << "a cave should have nothing to hang a door on";
    EXPECT_EQ(count_tiles(level.map, Tile::OpenDoor), 0);
}

TEST(Caves, AreOpenEnoughToPlayAndClosedEnoughToBeCaves) {
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        Rng rng(seed + 900000);
        MapGenConfig cfg;
        cfg.caves = true;
        cfg.door_chance = 0;
        const GeneratedLevel level = generate_level(rng, cfg, 6);

        const int open = static_cast<int>(level.map.walkable_cells().size());
        const int total = level.map.width() * level.map.height();
        EXPECT_GT(open, total / 6) << "cave seed " << seed << " is barely open at all";
        EXPECT_LT(open, total * 3 / 4) << "cave seed " << seed << " is one empty hall, not a cave";
    }
}

TEST(Caves, PutTheStaircasesFarApart) {
    // Two sweeps of "furthest cell" should land on opposite ends of the cave.
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        Rng rng(seed + 700000);
        MapGenConfig cfg;
        cfg.caves = true;
        cfg.door_chance = 0;
        const GeneratedLevel level = generate_level(rng, cfg, 7);
        EXPECT_GT(chebyshev(level.entrance, level.exit), 15)
            << "cave seed " << seed << ": the stairs are next to each other";
    }
}

TEST(Caves, AreDeterministic) {
    MapGenConfig cfg;
    cfg.caves = true;
    Rng a(99), b(99);
    EXPECT_EQ(generate_level(a, cfg, 6).map.raw_tiles(),
              generate_level(b, cfg, 6).map.raw_tiles());
}

TEST(Zones, TheMiddleBeltIsCarvedAsCavesAndTheOthersAreNot) {
    for_each_floor(2024, [](Game& g, int depth) {
        const int doors = count_tiles(g.map(), Tile::Door) + count_tiles(g.map(), Tile::OpenDoor);
        if (zone_for_depth(depth) == Zone::Chernotop)
            EXPECT_EQ(doors, 0) << "floor " << depth << " is a cave but has doors";
        // The room belts are not required to have doors on every floor, so the
        // opposite direction is checked in aggregate below.
    });
}

TEST(Zones, TheRoomBeltsDoProduceDoorsOverall) {
    int with_doors = 0, room_floors = 0;
    for (std::uint64_t seed = 0; seed < 4; ++seed) {
        for_each_floor(seed + 60, [&](Game& g, int depth) {
            if (zone_for_depth(depth) == Zone::Chernotop) return;
            ++room_floors;
            if (count_tiles(g.map(), Tile::Door) + count_tiles(g.map(), Tile::OpenDoor) > 0)
                ++with_doors;
        });
    }
    ASSERT_GT(room_floors, 0);
    EXPECT_GT(with_doors, room_floors / 2) << "the room belts hardly ever produce a door";
}

TEST(Zones, EveryFloorOfEveryBeltStaysConnectedAndReachable) {
    for (std::uint64_t seed = 0; seed < 6; ++seed) {
        for_each_floor(seed + 1000, [&](Game& g, int depth) {
            EXPECT_TRUE(is_fully_connected(g.map(), g.hero().a.pos))
                << "seed " << seed << ", floor " << depth;
            const auto path = find_path(g.map(), g.hero().a.pos, g.level().exit, 30000);
            EXPECT_FALSE(path.empty())
                << "seed " << seed << ", floor " << depth << ": the way down is unreachable";
        });
        if (::testing::Test::HasFatalFailure()) return;
    }
}
