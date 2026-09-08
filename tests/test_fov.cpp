// SPDX-License-Identifier: MIT
//
// Field of view is checked against hand-drawn grids, where the expected answer
// can be read off the picture, plus a symmetry property over random maps.
#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "nav/fov.hpp"
#include "nav/map.hpp"
#include "nav/rng.hpp"

using namespace nav;

namespace {

/// Builds a map from ASCII art: '#' is wall, everything else is floor.
Map from_art(const std::vector<std::string>& rows) {
    Map map(static_cast<int>(rows[0].size()), static_cast<int>(rows.size()));
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            map.set({x, y}, rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == '#'
                                ? Tile::Wall
                                : Tile::Floor);
    return map;
}

std::set<std::pair<int, int>> visible_set(Vec2 origin, int radius,
                                          const std::function<bool(Vec2)>& transparent) {
    std::set<std::pair<int, int>> out;
    compute_fov(origin, radius, transparent, [&out](Vec2 p) { out.insert({p.x, p.y}); });
    return out;
}

}  // namespace

TEST(Fov, TheObserverAlwaysSeesItsOwnCell) {
    Map map(5, 5);  // solid wall everywhere
    compute_fov(map, {2, 2}, 6);
    EXPECT_TRUE(map.visible({2, 2}));
}

TEST(Fov, ZeroRadiusSeesOnlyTheObserver) {
    const auto seen = visible_set({3, 3}, 0, [](Vec2) { return true; });
    EXPECT_EQ(seen.size(), 1u);
    EXPECT_TRUE(seen.count({3, 3}));
}

TEST(Fov, NothingOutsideTheRadiusIsLit) {
    const int radius = 5;
    const auto seen = visible_set({20, 20}, radius, [](Vec2) { return true; });
    for (const auto& [x, y] : seen) {
        const int dx = x - 20, dy = y - 20;
        EXPECT_LE(dx * dx + dy * dy, radius * radius)
            << "cell (" << x << "," << y << ") is lit but outside the radius";
    }
}

TEST(Fov, AnOpenRoomIsFullyVisible) {
    Map map = from_art({
        "#########",
        "#.......#",
        "#.......#",
        "#...@...#",
        "#.......#",
        "#.......#",
        "#########",
    });
    compute_fov(map, {4, 3}, 8);
    for (int y = 1; y <= 5; ++y)
        for (int x = 1; x <= 7; ++x)
            EXPECT_TRUE(map.visible({x, y})) << "floor cell (" << x << "," << y << ") stayed dark";
}

TEST(Fov, AWallCastsAShadowBehindIt) {
    //          x: 0123456
    Map map = from_art({
        "#######",
        "#.....#",
        "#.....#",
        "#@#...#",   // the wall at (2,3) must hide (3,3) and beyond
        "#.....#",
        "#.....#",
        "#######",
    });
    compute_fov(map, {1, 3}, 8);

    EXPECT_TRUE(map.visible({2, 3})) << "the blocking wall itself must be seen";
    EXPECT_FALSE(map.visible({3, 3})) << "the cell directly behind a wall must be hidden";
    EXPECT_FALSE(map.visible({4, 3}));
    EXPECT_TRUE(map.visible({1, 2})) << "cells beside the wall stay visible";
    EXPECT_TRUE(map.visible({1, 4}));
}

TEST(Fov, ASealedRoomHidesEverythingOutside) {
    Map map = from_art({
        "#########",
        "#...#...#",
        "#.@.#...#",
        "#...#...#",
        "#########",
    });
    compute_fov(map, {2, 2}, 10);

    for (int y = 1; y <= 3; ++y) {
        EXPECT_TRUE(map.visible({3, y})) << "the near side of the dividing wall";
        for (int x = 5; x <= 7; ++x)
            EXPECT_FALSE(map.visible({x, y})) << "cell (" << x << "," << y << ") is behind a wall";
    }
}

TEST(Fov, LightPassesThroughADoorwayButNotAroundItsFrame) {
    Map map = from_art({
        "#########",
        "#...#...#",
        "#.@.....#",   // the gap at (4,2) is the doorway
        "#...#...#",
        "#########",
    });
    compute_fov(map, {2, 2}, 12);

    EXPECT_TRUE(map.visible({7, 2})) << "the line straight through the doorway must be lit";
    EXPECT_FALSE(map.visible({5, 1}))
        << "the cell immediately behind the door frame must stay dark";
    EXPECT_FALSE(map.visible({5, 3}));
    EXPECT_TRUE(map.visible({7, 1})) << "the far room opens up as the angle widens";
    EXPECT_TRUE(map.visible({7, 3}));
}

TEST(Fov, IsSymmetricOnOpenTerrain) {
    // If A can see B, B must be able to see A. Asymmetry is the classic
    // shadowcasting bug and it makes ranged combat feel unfair.
    Rng rng(1234);
    for (int trial = 0; trial < 25; ++trial) {
        Map map(21, 21);
        for (int y = 1; y < 20; ++y)
            for (int x = 1; x < 20; ++x)
                map.set({x, y}, rng.chance(18) ? Tile::Wall : Tile::Floor);

        const Vec2 a{10, 10};
        map.set(a, Tile::Floor);
        const auto from_a = visible_set(a, 8, [&map](Vec2 p) { return map.transparent(p); });

        for (const auto& [x, y] : from_a) {
            const Vec2 b{x, y};
            if (!map.transparent(b)) continue;  // walls are seen, but do not look back
            const auto from_b = visible_set(b, 8, [&map](Vec2 p) { return map.transparent(p); });
            EXPECT_TRUE(from_b.count({a.x, a.y}))
                << "trial " << trial << ": (" << x << "," << y << ") is visible from the origin "
                << "but cannot see it back";
        }
    }
}

TEST(Fov, IsDeterministic) {
    Rng rng(55);
    Map map(21, 21);
    for (int y = 1; y < 20; ++y)
        for (int x = 1; x < 20; ++x)
            map.set({x, y}, rng.chance(20) ? Tile::Wall : Tile::Floor);
    map.set({10, 10}, Tile::Floor);

    const auto first = visible_set({10, 10}, 7, [&map](Vec2 p) { return map.transparent(p); });
    const auto second = visible_set({10, 10}, 7, [&map](Vec2 p) { return map.transparent(p); });
    EXPECT_EQ(first, second);
}

// --- Bresenham lines and line of sight ------------------------------------

TEST(Line, EndsAtTheTargetAndExcludesTheOrigin) {
    const auto path = line({0, 0}, {5, 3});
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.back(), (Vec2{5, 3}));
    EXPECT_NE(path.front(), (Vec2{0, 0}));
}

TEST(Line, EveryStepIsAdjacentToTheLast) {
    const auto path = line({2, 9}, {17, 1});
    Vec2 previous{2, 9};
    for (Vec2 p : path) {
        EXPECT_LE(chebyshev(previous, p), 1) << "the line jumped a cell";
        previous = p;
    }
}

TEST(Line, HandlesZeroLengthAndCardinalCases) {
    EXPECT_TRUE(line({4, 4}, {4, 4}).empty());
    EXPECT_EQ(line({0, 0}, {0, 3}).size(), 3u);
    EXPECT_EQ(line({0, 0}, {3, 0}).size(), 3u);
}

TEST(LineOfSight, IsBlockedByAWallAndClearWithoutOne) {
    Map map = from_art({
        "#########",
        "#.......#",
        "#.......#",
        "#########",
    });
    EXPECT_TRUE(has_line_of_sight(map, {1, 1}, {7, 1}));

    map.set({4, 1}, Tile::Wall);
    EXPECT_FALSE(has_line_of_sight(map, {1, 1}, {7, 1}));
}

TEST(LineOfSight, RespectsTheMaximumRange) {
    Map map(30, 5);
    for (int x = 1; x < 29; ++x) map.set({x, 2}, Tile::Floor);
    EXPECT_TRUE(has_line_of_sight(map, {1, 2}, {20, 2}, 25));
    EXPECT_FALSE(has_line_of_sight(map, {1, 2}, {20, 2}, 8));
}

TEST(LineOfSight, ACellAlwaysSeesItself) {
    Map map(5, 5);
    EXPECT_TRUE(has_line_of_sight(map, {2, 2}, {2, 2}));
}
