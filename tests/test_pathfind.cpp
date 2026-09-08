// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "nav/mapgen.hpp"
#include "nav/pathfind.hpp"
#include "nav/rng.hpp"

using namespace nav;

namespace {

Map from_art(const std::vector<std::string>& rows) {
    Map map(static_cast<int>(rows[0].size()), static_cast<int>(rows.size()));
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            map.set({x, y}, rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == '#'
                                ? Tile::Wall
                                : Tile::Floor);
    return map;
}

/// A path is valid when it starts next to `from`, never jumps, stays walkable
/// and finishes on `to`.
void expect_valid_path(const Map& map, const std::vector<Vec2>& path, Vec2 from, Vec2 to) {
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.back(), to);
    Vec2 previous = from;
    for (Vec2 p : path) {
        EXPECT_EQ(chebyshev(previous, p), 1) << "the path skipped from (" << previous.x << ","
                                             << previous.y << ") to (" << p.x << "," << p.y << ")";
        EXPECT_TRUE(map.walkable(p)) << "the path crossed a solid cell";
        previous = p;
    }
}

}  // namespace

TEST(AStar, FindsTheShortestRouteAcrossAnOpenRoom) {
    Map map = from_art({
        "##########",
        "#........#",
        "#........#",
        "#........#",
        "##########",
    });
    const auto path = find_path(map, {1, 1}, {8, 3});
    expect_valid_path(map, path, {1, 1}, {8, 3});
    // Diagonals cost the same as cardinals, so the answer is the Chebyshev
    // distance exactly.
    EXPECT_EQ(path.size(), static_cast<std::size_t>(chebyshev({1, 1}, {8, 3})));
}

TEST(AStar, RoutesAroundAnObstacle) {
    Map map = from_art({
        "#########",
        "#...#...#",
        "#...#...#",
        "#.......#",   // the only gap is on the bottom row
        "#########",
    });
    const auto path = find_path(map, {1, 1}, {7, 1});
    expect_valid_path(map, path, {1, 1}, {7, 1});
    // With diagonal movement the detour can still cost the Chebyshev distance;
    // what matters is that the route never crosses the dividing wall.
    EXPECT_GE(path.size(), static_cast<std::size_t>(chebyshev({1, 1}, {7, 1})));
    for (Vec2 p : path) {
        EXPECT_FALSE(p.x == 4 && p.y <= 2) << "the path walked through the dividing wall";
    }
}

TEST(AStar, ReturnsNothingWhenTheGoalIsWalledOff) {
    Map map = from_art({
        "#########",
        "#...#...#",
        "#...#...#",
        "#...#...#",
        "#########",
    });
    EXPECT_TRUE(find_path(map, {1, 2}, {7, 2}).empty());
}

TEST(AStar, ReturnsNothingForAGoalInsideAWall) {
    Map map = from_art({
        "#####",
        "#...#",
        "#####",
    });
    EXPECT_TRUE(find_path(map, {1, 1}, {0, 0}).empty());
}

TEST(AStar, ReturnsAnEmptyPathWhenAlreadyAtTheGoal) {
    Map map = from_art({"#####", "#...#", "#####"});
    EXPECT_TRUE(find_path(map, {2, 1}, {2, 1}).empty());
}

TEST(AStar, ReachesAGoalOccupiedByAnotherCreature) {
    // Monsters path *to* the hero, whose cell is impassable to them. The goal
    // must stay enterable or nothing would ever reach melee range.
    Map map = from_art({"#######", "#.....#", "#######"});
    const Vec2 goal{5, 1};
    const auto path = find_path(map, {1, 1}, goal,
                                [&](Vec2 p) { return map.walkable(p) && p != goal; });
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.back(), goal);
}

TEST(AStar, IsDeterministic) {
    Rng rng(4321);
    MapGenConfig cfg;
    const GeneratedLevel level = generate_level(rng, cfg, 5);
    const auto first = find_path(level.map, level.entrance, level.exit);
    const auto second = find_path(level.map, level.entrance, level.exit);
    EXPECT_EQ(first, second) << "the same query returned two different paths";
}

TEST(AStar, ConnectsTheStaircasesOnGeneratedLevels) {
    for (std::uint64_t seed = 0; seed < 60; ++seed) {
        Rng rng(seed + 3000);
        MapGenConfig cfg;
        const GeneratedLevel level = generate_level(rng, cfg, 1 + static_cast<int>(seed % 12));
        const auto path = find_path(level.map, level.entrance, level.exit, 20000);
        ASSERT_FALSE(path.empty()) << "seed " << seed << ": no route between the staircases";
        EXPECT_EQ(path.back(), level.exit);
    }
}

TEST(AStar, RespectsTheNodeBudget) {
    // A tiny budget on a large maze must fail cleanly rather than run forever.
    Rng rng(11);
    MapGenConfig cfg;
    const GeneratedLevel level = generate_level(rng, cfg, 8);
    const auto path = find_path(level.map, level.entrance, level.exit, 5);
    EXPECT_TRUE(path.empty());
}

// --- Dijkstra flow field ---------------------------------------------------

TEST(DijkstraMap, MeasuresStepDistanceFromTheSource) {
    Map map = from_art({
        "##########",
        "#........#",
        "#........#",
        "#........#",
        "##########",
    });
    DijkstraMap flow;
    flow.build(map, {{1, 1}});

    EXPECT_EQ(flow.at({1, 1}), 0);
    EXPECT_EQ(flow.at({2, 1}), 1);
    EXPECT_EQ(flow.at({2, 2}), 1) << "diagonal movement costs one step";
    EXPECT_EQ(flow.at({8, 3}), chebyshev({1, 1}, {8, 3}));
}

TEST(DijkstraMap, MarksWallsAndSealedRegionsUnreachable) {
    Map map = from_art({
        "#########",
        "#...#...#",
        "#...#...#",
        "#########",
    });
    DijkstraMap flow;
    flow.build(map, {{1, 1}});

    EXPECT_EQ(flow.at({4, 1}), DijkstraMap::kUnreachable) << "a wall cell";
    EXPECT_EQ(flow.at({6, 1}), DijkstraMap::kUnreachable) << "the sealed room";
    EXPECT_EQ(flow.at({-1, -1}), DijkstraMap::kUnreachable) << "outside the map";
}

TEST(DijkstraMap, SupportsSeveralSources) {
    Map map = from_art({"###########", "#.........#", "###########"});
    DijkstraMap flow;
    flow.build(map, {{1, 1}, {9, 1}});
    EXPECT_EQ(flow.at({1, 1}), 0);
    EXPECT_EQ(flow.at({9, 1}), 0);
    EXPECT_EQ(flow.at({5, 1}), 4);
}

TEST(DijkstraMap, BestStepWalksDownhillTowardsTheSource) {
    Map map = from_art({"###########", "#.........#", "###########"});
    DijkstraMap flow;
    flow.build(map, {{1, 1}});

    Vec2 here{9, 1};
    for (int i = 0; i < 20 && here != Vec2{1, 1}; ++i)
        here = flow.best_step(here, [&map](Vec2 p) { return map.walkable(p); }, true);
    EXPECT_EQ(here, (Vec2{1, 1})) << "descending the flow field did not reach the source";
}

TEST(DijkstraMap, BestStepWalksUphillWhenFleeing) {
    Map map = from_art({"###########", "#.........#", "###########"});
    DijkstraMap flow;
    flow.build(map, {{1, 1}});

    const Vec2 start{5, 1};
    const Vec2 away = flow.best_step(start, [&map](Vec2 p) { return map.walkable(p); }, false);
    EXPECT_GT(flow.at(away), flow.at(start)) << "fleeing moved towards the threat";
}

TEST(DijkstraMap, BestStepStaysPutWhenBoxedIn) {
    Map map = from_art({"#####", "#...#", "#####"});
    DijkstraMap flow;
    flow.build(map, {{1, 1}});
    // Nothing is passable, so there is nowhere better to go.
    const Vec2 here{2, 1};
    EXPECT_EQ(flow.best_step(here, [](Vec2) { return false; }, true), here);
}

TEST(DijkstraMap, IsUnreachableEverywhereWhenGivenNoSources) {
    Map map = from_art({"#####", "#...#", "#####"});
    DijkstraMap flow;
    flow.build(map, {});
    EXPECT_EQ(flow.at({2, 1}), DijkstraMap::kUnreachable);
}
