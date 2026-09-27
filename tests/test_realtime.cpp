// SPDX-License-Identifier: MIT
//
// The real-time game, tested the way the turn-based one is: without a screen.
//
// Most of these build a small floor by hand rather than hunting a seed for the
// right room. A test that asks "does a wall stop the hero" should not also be
// asking whatever the generator happened to put next to them on floor one.
#include <gtest/gtest.h>

#include <cmath>

#include "nav/data.hpp"
#include "nav/entity.hpp"
#include "nav/pathfind.hpp"
#include "nav/rt.hpp"

namespace nav {
namespace {

using rt::EventKind;
using rt::Input;
using rt::V;
using rt::World;

/// A floor of solid rock with the given rectangles carved out as floor.
Map carve(int w, int h, std::initializer_list<Rect> open) {
    Map m(w, h);
    for (const Rect& r : open)
        for (int y = r.top(); y <= r.bottom(); ++y)
            for (int x = r.left(); x <= r.right(); ++x) m.set({x, y}, Tile::Floor);
    return m;
}

/// A world with its hero standing in a plain 14×9 room.
World in_open_room(V hero = {7.5f, 4.5f}) {
    World w;
    w.start(1);
    w.load_test_floor(carve(16, 11, {Rect{1, 1, 14, 9}}), hero);
    return w;
}

void run(World& w, const Input& in, int ticks) {
    for (int i = 0; i < ticks; ++i) w.step(in);
}

bool heard(const std::vector<rt::Event>& events, EventKind kind) {
    for (const auto& e : events)
        if (e.kind == kind) return true;
    return false;
}

int upyr() { return species_index("upyr"); }        // walks up and bites
int kikimora() { return species_index("kikimora"); } // keeps away and throws

bool inside_rock(const Map& m, V p) {
    const Vec2 c{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y))};
    if (!m.in_bounds(c)) return true;
    const Tile t = m.at(c);
    return t == Tile::Wall || t == Tile::Chasm || t == Tile::Door;
}

// --- Starting out ------------------------------------------------------------

TEST(Realtime, TheHeroStartsOnSolidGroundWithEverythingFull) {
    World w;
    w.start(42);
    EXPECT_EQ(w.depth(), 1);
    EXPECT_FALSE(inside_rock(w.map(), w.hero().pos));
    EXPECT_EQ(w.hero().hp, w.hero().max_hp);
    EXPECT_EQ(w.hero().shield, w.hero().max_shield);
    EXPECT_GT(w.hero().max_shield, 0);
}

TEST(Realtime, TheRoomTheHeroArrivesInIsEmpty) {
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        World w;
        w.start(seed);
        for (const auto& room : w.rooms()) {
            const Vec2 c{static_cast<int>(w.hero().pos.x), static_cast<int>(w.hero().pos.y)};
            if (!room.interior.contains(c)) continue;
            for (const auto& e : w.enemies())
                EXPECT_FALSE(room.interior.contains({static_cast<int>(e.pos.x), static_cast<int>(e.pos.y)}))
                    << "seed " << seed << ": an enemy is waiting in the arrival room";
        }
    }
}

TEST(Realtime, TheSameSeedBuildsTheSameFirstFloor) {
    World a, b;
    a.start(20260927);
    b.start(20260927);
    EXPECT_EQ(a.map().raw_tiles(), b.map().raw_tiles());
    EXPECT_EQ(a.exit(), b.exit());
    ASSERT_EQ(a.enemies().size(), b.enemies().size());
    for (std::size_t i = 0; i < a.enemies().size(); ++i) {
        EXPECT_EQ(a.enemies()[i].species, b.enemies()[i].species);
        EXPECT_FLOAT_EQ(a.enemies()[i].pos.x, b.enemies()[i].pos.x);
        EXPECT_FLOAT_EQ(a.enemies()[i].pos.y, b.enemies()[i].pos.y);
    }
}

TEST(Realtime, EveryFloorOfEveryBeltHasAWayDown) {
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        World w;
        w.start(seed);
        for (int depth = 1; depth <= rt::kLastDepth; ++depth) {
            w.enter_floor(depth);
            const Map& m = w.map();
            ASSERT_FALSE(inside_rock(m, w.hero().pos)) << "seed " << seed << " depth " << depth;
            ASSERT_TRUE(m.in_bounds(w.exit())) << "seed " << seed << " depth " << depth;
            DijkstraMap flow;
            const Vec2 start{static_cast<int>(w.hero().pos.x), static_cast<int>(w.hero().pos.y)};
            flow.build(m, {start}, [&m](Vec2 p) {
                if (!m.in_bounds(p)) return false;
                const Tile t = m.at(p);
                return t != Tile::Wall && t != Tile::Chasm && t != Tile::Door;
            });
            EXPECT_LT(flow.at(w.exit()), DijkstraMap::kUnreachable)
                << "seed " << seed << " depth " << depth << ": the stairs cannot be walked to";
        }
    }
}

// --- Moving ---------------------------------------------------------------------

TEST(Realtime, TheHeroGoesWhereTheStickPoints) {
    World w = in_open_room({4.5f, 4.5f});
    run(w, Input{1, 0, false}, 30);   // half a second, full tilt, to the right
    EXPECT_NEAR(w.hero().pos.x, 4.5f + rt::kHeroSpeed * 0.5f, 0.05f);
    EXPECT_NEAR(w.hero().pos.y, 4.5f, 1e-4f);
    EXPECT_TRUE(w.hero().moving);
    EXPECT_GT(w.hero().facing.x, 0.9f);
}

TEST(Realtime, AHalfTiltedStickWalksAtHalfSpeed) {
    World w = in_open_room({4.5f, 4.5f});
    run(w, Input{0.5f, 0, false}, 30);
    EXPECT_NEAR(w.hero().pos.x, 4.5f + rt::kHeroSpeed * 0.25f, 0.05f);
}

TEST(Realtime, AWallStopsTheHeroAndTheySlideAlongIt) {
    World w = in_open_room({7.5f, 2.0f});
    // Up and to the right, into the top wall: the upward half is refused, the
    // sideways half carries on. Sticking fast to a wall on contact is the
    // single thing that makes movement in a game like this feel broken.
    run(w, Input{0.707f, -0.707f, false}, 60);
    EXPECT_GE(w.hero().pos.y, 1.0f + rt::kHeroRadius - 1e-3f);
    EXPECT_GT(w.hero().pos.x, 9.5f);
    EXPECT_FALSE(inside_rock(w.map(), w.hero().pos));
}

TEST(Realtime, NobodyEverEndsUpInsideTheRock) {
    // A soak: several floors, the stick swept round in circles with the
    // trigger down, and after every tick nobody may stand inside a wall.
    for (std::uint64_t seed = 3; seed <= 6; ++seed) {
        World w;
        w.start(seed);
        for (int t = 0; t < 1500 && !w.dead(); ++t) {
            const float a = static_cast<float>(t) * 0.013f;
            w.step(Input{std::cos(a), std::sin(a * 1.7f), true});
            ASSERT_FALSE(inside_rock(w.map(), w.hero().pos)) << "seed " << seed << " tick " << t;
            for (const auto& e : w.enemies())
                ASSERT_FALSE(inside_rock(w.map(), e.pos))
                    << "seed " << seed << " tick " << t << ": enemy " << e.id << " is in the wall";
        }
    }
}

// --- Fighting --------------------------------------------------------------------

TEST(Realtime, HeldFireKillsWhatItIsAimedAtAndSaysSo) {
    World w = in_open_room({3.5f, 4.5f});
    w.add_enemy(upyr(), {11.5f, 4.5f}).awake = false;   // asleep: stands still
    bool died = false;
    for (int t = 0; t < 600 && !died; ++t) {
        w.step(Input{0, 0, true});
        died = heard(w.take_events(), EventKind::EnemyDied);
    }
    EXPECT_TRUE(died);
    EXPECT_TRUE(w.enemies().empty());
    EXPECT_EQ(w.hero().kills, 1);
}

TEST(Realtime, TheAimFindsTheNearestEnemyByItself) {
    World w = in_open_room({7.5f, 4.5f});
    w.add_enemy(upyr(), {7.5f, 8.2f});    // below, the nearer
    w.add_enemy(upyr(), {13.5f, 4.5f});   // to the right, further
    w.step(Input{1, 0, true});            // walking right, finger on the trigger
    EXPECT_GT(w.hero().aim.y, 0.9f) << "aimed along the walk instead of at the nearer enemy";
}

TEST(Realtime, AnAwakeEnemyComesForTheHero) {
    World w = in_open_room({2.5f, 4.5f});
    rt::Enemy& e = w.add_enemy(upyr(), {13.0f, 4.5f});
    e.awake = true;
    const float before = w.enemies()[0].pos.x - w.hero().pos.x;
    run(w, Input{}, 60);
    EXPECT_LT(w.enemies()[0].pos.x - w.hero().pos.x, before - 1.5f);
}

TEST(Realtime, AThrowerCirclesAtItsOwnRangeAndThrows) {
    World w = in_open_room({7.5f, 4.5f});
    w.add_enemy(kikimora(), {9.8f, 4.5f}).awake = true;   // starts too close
    bool threw = false;
    for (int t = 0; t < 180; ++t) {
        w.step(Input{});
        threw = threw || heard(w.take_events(), EventKind::EnemyShot);
    }
    EXPECT_TRUE(threw);
    // Distance, not which side it is on: circling the hero is the point, so it
    // is expected to come round. What it must not do is walk up and bite.
    const float d = rt::length(w.enemies()[0].pos - w.hero().pos);
    EXPECT_GT(d, rt::kRangedRange - 1.0f);
    EXPECT_LT(d, rt::kRangedRange + 1.5f);
}

TEST(Realtime, TheShieldTakesTheFirstHitsAndAPileOnIsSpacedOut) {
    World w = in_open_room({5.5f, 4.5f});
    const int hp = w.hero().hp, shield = w.hero().shield;
    rt::Enemy& e = w.add_enemy(upyr(), {6.1f, 4.5f});
    e.awake = true;
    w.step(Input{});
    EXPECT_EQ(w.hero().hp, hp) << "health went before the shield did";
    EXPECT_LT(w.hero().shield, shield);
    // Within the invulnerable window no second blow lands, however many are
    // pressing in.
    w.add_enemy(upyr(), {4.9f, 4.5f}).awake = true;
    const int after_first = w.hero().shield;
    run(w, Input{}, static_cast<int>(rt::kInvulnerable / rt::kDt) - 2);
    EXPECT_EQ(w.hero().shield, after_first);
}

TEST(Realtime, TheShieldComesBackAfterAQuietSpell) {
    // Through a real blow, so the quiet spell is timed from where the rules
    // time it and not from a field a test happened to set.
    World w = in_open_room({5.5f, 4.5f});
    w.add_enemy(upyr(), {6.1f, 4.5f}).awake = true;
    w.step(Input{});
    w.mutable_enemies().clear();
    const int after_blow = w.hero().shield;
    ASSERT_LT(after_blow, w.hero().max_shield);

    run(w, Input{}, static_cast<int>(1.0f / rt::kDt));
    EXPECT_EQ(w.hero().shield, after_blow) << "one second without a hit is not yet quiet";
    run(w, Input{}, static_cast<int>(4.0f / rt::kDt));
    EXPECT_GT(w.hero().shield, after_blow);
}

TEST(Realtime, AHeroWithNothingLeftDies) {
    World w = in_open_room({5.5f, 4.5f});
    w.mutable_hero().hp = 1;
    w.mutable_hero().shield = 0;
    w.add_enemy(upyr(), {6.1f, 4.5f}).awake = true;
    bool died = false;
    for (int t = 0; t < 60 && !died; ++t) {
        w.step(Input{});
        died = heard(w.take_events(), EventKind::HeroDied);
    }
    EXPECT_TRUE(died);
    EXPECT_TRUE(w.dead());
    const float x = w.hero().pos.x;
    w.step(Input{1, 0, true});
    EXPECT_FLOAT_EQ(w.hero().pos.x, x) << "the dead do not walk";
}

// --- Rooms ------------------------------------------------------------------------

/// A corridor running into the west side of one room, a single enemy inside.
World room_with_a_guard() {
    World w;
    w.start(1);
    const Rect room{8, 2, 8, 5};
    w.load_test_floor(carve(19, 9, {Rect{1, 4, 7, 1}, room}), {2.5f, 4.5f}, {room});
    w.add_enemy(upyr(), {14.5f, 4.5f}, 0);
    return w;
}

TEST(Realtime, WalkingIntoAGuardedRoomShutsItsDoors) {
    World w = room_with_a_guard();
    ASSERT_EQ(w.rooms()[0].doorways.size(), 1u);
    const Vec2 door = w.rooms()[0].doorways[0];
    EXPECT_EQ(door, (Vec2{7, 4}));
    bool locked = false;
    for (int t = 0; t < 240 && !locked; ++t) {
        w.step(Input{1, 0, false});
        locked = heard(w.take_events(), EventKind::RoomLocked);
    }
    ASSERT_TRUE(locked);
    EXPECT_EQ(w.map().at(door), Tile::Door);
    EXPECT_TRUE(w.in_locked_room());
    EXPECT_TRUE(w.enemies()[0].awake) << "the guard slept through its own doors slamming";
    // Shut behind the hero, not on them.
    EXPECT_GT(w.hero().pos.x, 8.0f + rt::kHeroRadius);
}

TEST(Realtime, ClearingTheRoomOpensItAgain) {
    World w = room_with_a_guard();
    run(w, Input{1, 0, false}, 120);
    ASSERT_TRUE(w.in_locked_room());
    bool cleared = false;
    for (int t = 0; t < 900 && !cleared; ++t) {
        w.step(Input{0, 0, true});
        cleared = heard(w.take_events(), EventKind::RoomCleared);
    }
    ASSERT_TRUE(cleared);
    EXPECT_FALSE(w.in_locked_room());
    EXPECT_EQ(w.map().at({7, 4}), Tile::Floor) << "the doorway came back as something it was not";
}

TEST(Realtime, AGuardThatChasedTheHeroOutCannotLockThemIn) {
    // The deadlock this guards against: a guard wakes, follows the hero into
    // the corridor, the hero slips past into the room, the doors shut — and
    // the room waits forever on a creature it has locked outside.
    World w = room_with_a_guard();
    rt::Enemy& e = w.mutable_enemies()[0];
    e.awake = true;
    e.pos = {4.5f, 4.5f};   // out in the corridor
    w.step(Input{});
    EXPECT_EQ(w.enemies()[0].room, -1);
}

TEST(Realtime, AnEmptyRoomNeverLocks) {
    World w = room_with_a_guard();
    w.mutable_enemies().clear();
    run(w, Input{1, 0, false}, 200);
    EXPECT_FALSE(w.in_locked_room());
    EXPECT_TRUE(w.rooms()[0].cleared);
}

// --- Going down -------------------------------------------------------------------

TEST(Realtime, TheStairsTakeTheHeroDown) {
    World w;
    w.start(7);
    w.mutable_enemies().clear();   // nothing to lock them into a room on the way
    const Vec2 exit = w.exit();
    w.mutable_hero().pos = {static_cast<float>(exit.x) + 0.5f, static_cast<float>(exit.y) + 0.5f};
    w.step(Input{});
    EXPECT_EQ(w.depth(), 2);
    EXPECT_TRUE(heard(w.take_events(), EventKind::Descended));
    EXPECT_FALSE(inside_rock(w.map(), w.hero().pos));
}

TEST(Realtime, APotionWaitsForAHeroWhoNeedsIt) {
    World w = in_open_room({4.5f, 4.5f});
    w.mutable_pickups().push_back(rt::Pickup{900, rt::PickupKind::Potion, {4.5f, 4.5f}, 0});
    w.step(Input{});
    EXPECT_EQ(w.pickups().size(), 1u) << "a potion was drunk by a hero with nothing to heal";

    w.mutable_hero().hp = 5;
    w.step(Input{});
    EXPECT_TRUE(w.pickups().empty());
    EXPECT_GT(w.hero().hp, 5);
    EXPECT_LE(w.hero().hp, w.hero().max_hp);
}

TEST(Realtime, CoinsDriftToAHeroWhoComesNear) {
    World w = in_open_room({4.5f, 4.5f});
    w.mutable_pickups().push_back(rt::Pickup{901, rt::PickupKind::Coin, {6.2f, 4.5f}, 3});
    run(w, Input{}, 60);   // the hero stands still; the coin comes to them
    EXPECT_EQ(w.hero().coins, 3);
    EXPECT_TRUE(w.pickups().empty());
}

}  // namespace
}  // namespace nav
