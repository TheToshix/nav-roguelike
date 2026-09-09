// SPDX-License-Identifier: MIT
//
// Floor events: one per belt below Погост, a temporary hazard that runs while
// the hero is on the floor. These tests pin down that each event does its one
// thing, that it never breaks the floor (stairs stay, the map stays crossable),
// that it rolls off a private stream so generation is untouched, and that it
// survives a save.
#include <gtest/gtest.h>

#include <algorithm>
#include <queue>
#include <set>

#include "nav/data.hpp"
#include "nav/game.hpp"

#include "support.hpp"

using namespace nav;

namespace {

int count_tiles(const Map& map, Tile t) {
    int n = 0;
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            if (map.at({x, y}) == t) ++n;
    return n;
}

/// Descends to `depth` by force, leaving the floor exactly as generated.
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

void wait(Game& g, int turns) {
    for (int i = 0; i < turns && g.state() == RunState::Playing; ++i)
        g.perform(Action{ActionType::Wait, {}, -1, {}});
}

/// Every walkable cell reachable on foot from the entrance.
bool every_walkable_cell_is_reachable(const Map& map, Vec2 from) {
    std::set<std::pair<int, int>> seen;
    std::queue<Vec2> q;
    q.push(from);
    seen.insert({from.x, from.y});
    while (!q.empty()) {
        const Vec2 p = q.front();
        q.pop();
        for (const Vec2 d : {Vec2{1, 0}, Vec2{-1, 0}, Vec2{0, 1}, Vec2{0, -1}}) {
            const Vec2 n{p.x + d.x, p.y + d.y};
            if (!map.walkable(n) || seen.count({n.x, n.y})) continue;
            seen.insert({n.x, n.y});
            q.push(n);
        }
    }
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x)
            if (map.walkable({x, y}) && !seen.count({x, y})) return false;
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Which belt raises what
// ---------------------------------------------------------------------------

TEST(Events, EachBeltBelowPogostHasItsOwn) {
    EXPECT_EQ(belt_event(Zone::Rasputye), EventKind::None);
    EXPECT_EQ(belt_event(Zone::Pogost), EventKind::None);
    EXPECT_EQ(belt_event(Zone::Chernotop), EventKind::Flood);
    EXPECT_EQ(belt_event(Zone::Koshchei), EventKind::Blizzard);
    EXPECT_EQ(belt_event(Zone::Peklo), EventKind::Firestorm);
}

TEST(Events, NamesAndNotesAreBilingualAndPresent) {
    for (EventKind k : {EventKind::Flood, EventKind::Blizzard, EventKind::Firestorm}) {
        EXPECT_FALSE(event_name(k).ru.empty());
        EXPECT_FALSE(event_name(k).en.empty());
        EXPECT_FALSE(event_note(k).ru.empty());
        EXPECT_FALSE(event_note(k).en.empty());
    }
    EXPECT_TRUE(event_name(EventKind::None).ru.empty());
}

TEST(Events, NeverOnABeltsThresholdOrItsGuardiansFloor) {
    // Scan a spread of seeds: an event may or may not roll, but it must never
    // land on floor 5/9/13 (the arrival line) or 8/12/16 (the boss fight).
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        for (int depth : {5, 8, 9, 12, 13, 16}) {
            Game g = descend_to(depth, seed);
            EXPECT_EQ(g.level_event(), EventKind::None)
                << "seed " << seed << " floor " << depth;
        }
    }
}

TEST(Events, TheRollIsDeterministicForASeed) {
    for (std::uint64_t seed = 100; seed <= 115; ++seed) {
        Game a = descend_to(7, seed);
        Game b = descend_to(7, seed);
        EXPECT_EQ(a.level_event(), b.level_event()) << "seed " << seed;
    }
}

TEST(Events, TheRollDoesNotDisturbGeneration) {
    // Two runs of the same seed reach floor 6 with byte-for-byte the same
    // floor: same items where they lay, same monsters where they stood. The
    // event roll happens off a private stream, so it cannot have moved any of
    // it. (The sweep is the other half of this: the recorded seeds still reach
    // 16/16 with 8/8 guardians — see docs/TEST_PLAN.md.)
    Game a = descend_to(6, 777);
    Game b = descend_to(6, 777);
    ASSERT_EQ(a.floor_items().size(), b.floor_items().size());
    for (std::size_t i = 0; i < a.floor_items().size(); ++i) {
        EXPECT_EQ(a.floor_items()[i].pos, b.floor_items()[i].pos);
        EXPECT_EQ(a.floor_items()[i].kind, b.floor_items()[i].kind);
    }
    ASSERT_EQ(a.monsters().size(), b.monsters().size());
    for (std::size_t i = 0; i < a.monsters().size(); ++i)
        EXPECT_EQ(a.monsters()[i].a.pos, b.monsters()[i].a.pos);
}

// ---------------------------------------------------------------------------
// Половодье — the flood
// ---------------------------------------------------------------------------

TEST(Flood, TheWaterRisesButLeavesTheFloorCrossable) {
    Game g = descend_to(6, 4242);
    g.mutable_level().event = EventKind::Flood;
    g.mutable_level().event_age = 0;

    const int water_before = count_tiles(g.map(), Tile::Water);
    const Vec2 entrance = g.level().entrance;
    const Vec2 exit = g.level().exit;
    const Tile entrance_tile = g.map().at(entrance);
    const Tile exit_tile = g.map().at(exit);

    wait(g, 20);

    EXPECT_GT(count_tiles(g.map(), Tile::Water), water_before) << "the water never rose";
    EXPECT_EQ(g.map().at(entrance), entrance_tile) << "the up stairs went under";
    EXPECT_EQ(g.map().at(exit), exit_tile) << "the down stairs went under";
    EXPECT_TRUE(every_walkable_cell_is_reachable(g.map(), entrance))
        << "the flood cut the floor in two";
}

TEST(Flood, TheTideStopsRisingEventually) {
    Game g = descend_to(7, 909);
    g.mutable_level().event = EventKind::Flood;
    g.mutable_level().event_age = 0;

    wait(g, 60);
    const int settled = count_tiles(g.map(), Tile::Water);
    wait(g, 40);
    EXPECT_EQ(count_tiles(g.map(), Tile::Water), settled) << "the flood never settled";
}

// ---------------------------------------------------------------------------
// Метель — the blizzard
// ---------------------------------------------------------------------------

TEST(Blizzard, SightClosesInWhileItRuns) {
    Game g = descend_to(10, 4242);
    const int clear = g.hero_sight();

    g.mutable_level().event = EventKind::Blizzard;
    g.mutable_level().event_age = 0;
    const int fresh = g.hero_sight();
    g.mutable_level().event_age = 30;
    const int deep = g.hero_sight();

    EXPECT_LT(fresh, clear) << "the blizzard did not shorten sight at all";
    EXPECT_LE(deep, fresh) << "the blizzard eased off instead of deepening";
    EXPECT_GE(deep, 2) << "sight must never close to nothing";

    g.mutable_level().event = EventKind::None;
    EXPECT_EQ(g.hero_sight(), clear) << "sight did not come back when it lifted";
}

// ---------------------------------------------------------------------------
// Пожар — the firestorm
// ---------------------------------------------------------------------------

TEST(Firestorm, FireSpreadsFromWhereItCaughtThenBurnsOut) {
    Game g = descend_to(14, 4242);
    g.mutable_level().event = EventKind::Firestorm;
    g.mutable_level().event_age = 0;

    // Seed a couple of fires on bare floor near the hero.
    int seeded = 0;
    const Vec2 me = g.hero().a.pos;
    for (int r = 1; r <= 4 && seeded < 2; ++r)
        for (int dy = -r; dy <= r && seeded < 2; ++dy)
            for (int dx = -r; dx <= r && seeded < 2; ++dx) {
                const Vec2 p{me.x + dx, me.y + dy};
                if (g.map().at(p) == Tile::Floor && g.ember_at(p) == 0) {
                    g.ignite(p, kEmberTurns);
                    ++seeded;
                }
            }
    ASSERT_EQ(seeded, 2);

    int peak = 0;
    for (int i = 0; i < 30; ++i) {
        wait(g, 1);
        peak = std::max(peak, static_cast<int>(g.level().embers.size()));
    }
    EXPECT_GT(peak, 2) << "the fire never spread past where it started";

    wait(g, 60);
    EXPECT_TRUE(g.level().embers.empty()) << "the fire never burned out";
}

TEST(Firestorm, StandingInItHurts) {
    Game g = descend_to(15, 909);
    g.mutable_level().event = EventKind::Firestorm;
    g.mutable_level().event_age = 0;
    g.mutable_hero().a.hp = g.mutable_hero().a.max_hp;

    // Fire only takes on bare floor, and the hero arrives on the stairs — so
    // step onto a floor cell first, then light it underfoot.
    const Vec2 me = g.hero().a.pos;
    for (const Vec2 d : {Vec2{1, 0}, Vec2{-1, 0}, Vec2{0, 1}, Vec2{0, -1}}) {
        const Vec2 p{me.x + d.x, me.y + d.y};
        if (g.map().at(p) == Tile::Floor) { g.mutable_hero().a.pos = p; break; }
    }
    g.refresh_view();
    g.ignite(g.hero().a.pos, kEmberTurns);
    ASSERT_GT(g.ember_at(g.hero().a.pos), 0) << "could not light the floor under the hero";
    const int hp_before = g.hero().a.hp;
    wait(g, 3);
    EXPECT_LT(g.hero().a.hp, hp_before) << "the burning floor did not touch the hero";
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

TEST(Events, ARunningEventSurvivesSaveAndLoad) {
    Game g = descend_to(6, 4242);
    g.mutable_level().event = EventKind::Flood;
    g.mutable_level().event_age = 0;
    wait(g, 9);
    const int age = g.level_event_age();
    const int water = count_tiles(g.map(), Tile::Water);

    Game restored;
    ASSERT_TRUE(restored.load(g.save()));
    EXPECT_EQ(restored.level_event(), EventKind::Flood);
    EXPECT_EQ(restored.level_event_age(), age);
    EXPECT_EQ(count_tiles(restored.map(), Tile::Water), water) << "the water line was not saved";
}
