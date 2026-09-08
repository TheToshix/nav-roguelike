// SPDX-License-Identifier: MIT
//
// The guardians' halls.
//
// An arena is two promises at once — the guardian cannot be pulled out into a
// corridor, and the hero cannot walk away mid-fight — and a promise that holds
// for one side and not the other is worse than no promise. Add to that a
// rectangle of walls carved into a level that was already finished, and the
// failure mode is a floor cut in two. So the tests here are mostly about the
// carve being safe and the seal being symmetric.
#include <gtest/gtest.h>

#include <cstring>

#include "nav/game.hpp"
#include "nav/pathfind.hpp"

#include "support.hpp"

namespace nav {
namespace {

/// Walks a run down to `depth`, generating every floor on the way.
Game descend_to(int depth, std::uint64_t seed) {
    GameConfig cfg;
    cfg.seed = seed;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    for (int d = 1; d < depth; ++d) {
        g.mutable_hero().a.pos = g.level().exit;
        g.refresh_view();
        EXPECT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
    return g;
}

/// Every walkable cell reachable from `from`, by terrain alone.
int reachable_from(const Map& map, Vec2 from) {
    DijkstraMap flow;
    flow.build(map, {from});
    int n = 0;
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            if (map.walkable({x, y}) && flow.at({x, y}) < DijkstraMap::kUnreachable) ++n;
    return n;
}

int walkable_cells(const Map& map) {
    int n = 0;
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            if (map.walkable({x, y})) ++n;
    return n;
}

/// The floors that end a belt — the ones with a master rather than a lesser
/// guardian.
const int kMasterFloors[] = {4, 8, 12, 16};

}  // namespace

TEST(ArenaHall, EveryBeltMasterGetsAHallOnEverySeed) {
    // Not "usually": the carve is allowed to fail and roll back, so this test
    // is what says the fallback is rare rather than the normal case.
    int halls = 0, floors = 0;
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        Game g = descend_to(16, seed * 7919);
        for (int depth : kMasterFloors) {
            ++floors;
            if (g.level_at(depth).arena.exists) ++halls;
        }
    }
    EXPECT_EQ(halls, floors) << "a belt master was left standing in the open on some seed";
}

TEST(ArenaHall, LesserGuardiansGetNoHall) {
    // The difference between a master and a lesser guardian is that the lesser
    // one is a fight you may walk away from.
    Game g = descend_to(16, 4242);
    for (int depth : {3, 7, 11, 15}) {
        EXPECT_FALSE(g.level_at(depth).arena.exists) << "floor " << depth << " walled off a mini-boss";
    }
}

TEST(ArenaHall, CarvingAHallNeverCutsTheFloorInTwo) {
    // The whole floor must still be reachable from the way in. This is the
    // failure the rollback exists for, and the reason the rollback is not
    // simply trusted.
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
        Game g = descend_to(16, seed * 104729);
        for (int depth : kMasterFloors) {
            const Level& lvl = g.level_at(depth);
            ASSERT_TRUE(lvl.generated);
            EXPECT_EQ(reachable_from(lvl.map, lvl.entrance), walkable_cells(lvl.map))
                << "seed " << seed << ", floor " << depth << ": part of the floor is walled off";
        }
    }
}

TEST(ArenaHall, TheWayDownIsInsideTheHall) {
    // A guardian guards something. If the stairs were outside its hall the
    // fight would be optional, and an optional boss is a monster in a room.
    Game g = descend_to(16, 20260908);
    for (int depth : kMasterFloors) {
        const Level& lvl = g.level_at(depth);
        if (!lvl.arena.exists) continue;
        EXPECT_TRUE(lvl.arena.contains(lvl.exit))
            << "floor " << depth << ": the stairs are outside the guardian's hall";
    }
}

TEST(ArenaHall, TheGuardianStartsInsideItsOwnHall) {
    Game g = descend_to(16, 555777);
    for (int depth : kMasterFloors) {
        const Level& lvl = g.level_at(depth);
        if (!lvl.arena.exists) continue;
        const char* key = boss_for_depth(depth);
        ASSERT_NE(key, nullptr);
        const int index = species_index(key);
        bool found = false;
        for (const Monster& m : lvl.monsters)
            if (m.species == index) {
                found = true;
                EXPECT_TRUE(lvl.arena.contains(m.a.pos))
                    << "floor " << depth << ": the guardian is not in its own hall";
            }
        EXPECT_TRUE(found) << "floor " << depth << " has no guardian at all";
    }
}

TEST(ArenaHall, ThereIsExactlyOneWayIn) {
    Game g = descend_to(16, 31337);
    for (int depth : kMasterFloors) {
        const Level& lvl = g.level_at(depth);
        if (!lvl.arena.exists) continue;
        const Arena& a = lvl.arena;

        int gaps = 0;
        for (int x = a.min.x - 1; x <= a.max.x + 1; ++x) {
            if (lvl.map.walkable({x, a.min.y - 1})) ++gaps;
            if (lvl.map.walkable({x, a.max.y + 1})) ++gaps;
        }
        for (int y = a.min.y; y <= a.max.y; ++y) {
            if (lvl.map.walkable({a.min.x - 1, y})) ++gaps;
            if (lvl.map.walkable({a.max.x + 1, y})) ++gaps;
        }
        EXPECT_EQ(gaps, 1) << "floor " << depth << ": a hall with " << gaps << " ways in";
        EXPECT_TRUE(lvl.map.walkable(a.door)) << "the one way in is not walkable";
    }
}

TEST(ArenaHall, TheDoorsCloseOnlyOnceTheHeroIsInside) {
    Game g = descend_to(4, 99991);
    const Arena before = g.arena();
    ASSERT_TRUE(before.exists);
    EXPECT_FALSE(before.sealed) << "the hall was sealed before anyone entered it";

    // Walk in the honest way: along a real path to the middle of the hall,
    // through the doorway like a player would.
    const Vec2 middle{(g.arena().min.x + g.arena().max.x) / 2,
                      (g.arena().min.y + g.arena().max.y) / 2};
    g.mutable_hero().a.max_hp = 100000;
    g.mutable_hero().a.hp = 100000;
    for (int guard = 0; guard < 600 && !g.arena().sealed; ++guard) {
        const Vec2 me = g.hero().a.pos;
        if (me == middle) break;
        const auto path = find_path(g.map(), me, middle, 8000);
        if (path.empty()) break;
        if (!g.perform(Action{ActionType::Move, path.front() - me, -1, {}}))
            g.perform(Action{ActionType::Wait, {}, -1, {}});
    }

    ASSERT_TRUE(g.arena().sealed) << "the hero got inside and the doors stayed open";
    EXPECT_TRUE(g.arena().contains(g.hero().a.pos));
}

TEST(ArenaHall, ASealedHallHoldsBothSidesOfTheFight) {
    // The rule is symmetric by construction — one function answers for the hero
    // and for every monster — and this is the test that says so out loud.
    Game g = descend_to(4, 606060);
    ASSERT_TRUE(g.arena().exists);
    Arena& a = g.mutable_level().arena;
    a.sealed = true;

    const Vec2 inside{(a.min.x + a.max.x) / 2, (a.min.y + a.max.y) / 2};
    const Vec2 outside{a.door.x, a.door.y};   // the doorway sits on the wall

    EXPECT_TRUE(g.arena_allows(inside, Vec2{inside.x + 1, inside.y})) << "moving inside was refused";
    EXPECT_FALSE(g.arena_allows(inside, outside)) << "the hall let someone out";
    EXPECT_FALSE(g.arena_allows(outside, inside)) << "the hall let someone in";
}

TEST(ArenaHall, TheHeroCannotWalkOutOfASealedHall) {
    Game g = descend_to(4, 121212);
    ASSERT_TRUE(g.arena().exists);
    const Arena& a = g.arena();

    // Put the hero on the inside edge next to the door and try to leave.
    Vec2 spot{a.door.x, a.door.y};
    if (spot.x == a.min.x - 1) spot.x = a.min.x;
    else if (spot.x == a.max.x + 1) spot.x = a.max.x;
    else if (spot.y == a.min.y - 1) spot.y = a.min.y;
    else spot.y = a.max.y;

    g.mutable_hero().a.pos = spot;
    g.mutable_level().arena.sealed = true;
    g.refresh_view();

    const int turn_before = g.turn();
    const Vec2 out = step_towards(spot, a.door);
    EXPECT_FALSE(g.perform(Action{ActionType::Move, out, -1, {}}))
        << "the hero walked out of a sealed hall";
    EXPECT_EQ(g.hero().a.pos, spot);
    EXPECT_EQ(g.turn(), turn_before) << "a refused step cost a turn";
}

TEST(ArenaHall, KoscheiKeepsHisDoorsOpen) {
    // His death is on a needle somewhere else on the floor. A sealed hall would
    // be a room the player cannot win in, which is the one thing an arena must
    // never be.
    Game g = descend_to(12, 246810);
    ASSERT_TRUE(g.arena().exists) << "Кощей should still get a hall";
    EXPECT_FALSE(g.arena().seals) << "Кощей's hall must not seal";

    g.mutable_hero().a.pos = {(g.arena().min.x + g.arena().max.x) / 2,
                              (g.arena().min.y + g.arena().max.y) / 2};
    g.refresh_view();
    g.perform(Action{ActionType::Wait, {}, -1, {}});
    EXPECT_FALSE(g.arena().sealed) << "the doors closed on a fight that needs the whole floor";
}

TEST(ArenaHall, KillingTheGuardianOpensTheDoors) {
    Game g = descend_to(4, 777333);
    ASSERT_TRUE(g.arena().exists);
    g.mutable_hero().a.pos = {(g.arena().min.x + g.arena().max.x) / 2,
                              (g.arena().min.y + g.arena().max.y) / 2};
    g.refresh_view();
    g.mutable_hero().a.max_hp = 100000;
    g.mutable_hero().a.hp = 100000;
    g.perform(Action{ActionType::Wait, {}, -1, {}});
    ASSERT_TRUE(g.arena().sealed);

    for (Monster& m : g.mutable_level().monsters) m.a.alive = false;
    g.perform(Action{ActionType::Wait, {}, -1, {}});

    EXPECT_FALSE(g.arena().sealed) << "the guardian is dead and the hall is still shut";
    const Vec2 me = g.hero().a.pos;
    EXPECT_TRUE(g.arena_allows(me, Vec2{me.x + 1, me.y}));
}

TEST(ArenaHall, TheHallSurvivesASaveAndLoad) {
    // A sealed fight that reopens on load is a fight the player can walk away
    // from by pressing two keys.
    Game g = descend_to(4, 858585);
    ASSERT_TRUE(g.arena().exists);
    g.mutable_hero().a.pos = {(g.arena().min.x + g.arena().max.x) / 2,
                              (g.arena().min.y + g.arena().max.y) / 2};
    g.refresh_view();
    g.perform(Action{ActionType::Wait, {}, -1, {}});
    ASSERT_TRUE(g.arena().sealed);

    const std::string blob = g.save();
    Game loaded;
    ASSERT_TRUE(loaded.load(blob));

    const Arena& a = loaded.arena();
    EXPECT_TRUE(a.exists);
    EXPECT_TRUE(a.sealed) << "the doors opened themselves over a save";
    EXPECT_EQ(a.min.x, g.arena().min.x);
    EXPECT_EQ(a.max.y, g.arena().max.y);
    EXPECT_EQ(a.door.x, g.arena().door.x);
    EXPECT_EQ(a.seals, g.arena().seals);
}

TEST(ArenaHall, ASaveWithAnImpossibleHallIsRefused) {
    // The loader's job is to disbelieve the file. A hall with its corners the
    // wrong way round would be a rectangle the hero can never satisfy.
    Game g = descend_to(4, 191919);
    ASSERT_TRUE(g.arena().exists);
    std::string blob = g.save();

    // The arena is written as "1 minx miny maxx maxy ...". Turning a corner
    // negative is enough to make it nonsense.
    const std::size_t at = blob.find(" 1 " + std::to_string(g.arena().min.x) + " " +
                                     std::to_string(g.arena().min.y));
    if (at == std::string::npos) GTEST_SKIP() << "could not find the hall in the save text";
    blob.replace(at, 3, " 1 -");

    Game other;
    other.start(GameConfig{});
    const int depth_before = other.depth();
    EXPECT_FALSE(other.load(blob)) << "a nonsensical hall was loaded anyway";
    EXPECT_EQ(other.depth(), depth_before) << "a failed load damaged the running game";
}

}  // namespace nav
