// SPDX-License-Identifier: MIT
//
// The crossroads.
//
// A run begins in a room rather than on a floor. The room is the one part of
// the game nothing generates, so what has to be tested is that it is a real
// place — reachable, walkable, safe — and that its single rule holds: three
// things are offered and exactly one leaves with the hero.
#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "nav/game.hpp"

#include "support.hpp"

using namespace nav;

namespace {

Game fresh(std::uint64_t seed) {
    GameConfig cfg;
    cfg.seed = seed;
    Game g;
    g.start(cfg);
    return g;
}

}  // namespace

TEST(Lobby, ARunBeginsThereAndNotInTheDungeon) {
    Game g = fresh(11);
    EXPECT_EQ(g.depth(), kLobbyDepth);
    EXPECT_TRUE(g.in_lobby());
    EXPECT_EQ(g.zone(), Zone::Rasputye);
}

TEST(Lobby, OnlySoloveiLivesThere) {
    // The crossroads holds exactly one creature, on every seed: Соловей-
    // Разбойник by the road down. Nothing else generates here — it is a place
    // to prepare, plus the one encounter that is met rather than chosen.
    const int solovey = species_index("solovey");
    ASSERT_GE(solovey, 0);
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        Game g = fresh(seed);
        ASSERT_EQ(g.monsters().size(), 1u) << "seed " << seed;
        EXPECT_EQ(g.monsters()[0].species, solovey) << "seed " << seed;
        EXPECT_TRUE(g.monsters()[0].awake) << "he has already seen the hero";
    }
}

TEST(Lobby, ItOffersThreeThingsAndTakesBackTwo) {
    for (std::uint64_t seed = 1; seed <= 25; ++seed) {
        Game g = fresh(seed);
        ASSERT_EQ(g.floor_items().size(), 3u) << "seed " << seed;
        for (const Item& it : g.floor_items())
            EXPECT_TRUE(it.is_gear()) << "the crossroads should offer gear, not supplies";

        const Vec2 target = g.floor_items()[1].pos;
        for (int guard = 0; guard < 60 && g.hero().a.pos != target; ++guard)
            g.perform(Action{ActionType::Move, step_towards(g.hero().a.pos, target), -1, {}});
        ASSERT_EQ(g.hero().a.pos, target) << "seed " << seed << ": could not reach the pedestal";

        const std::size_t before = g.hero().inv.items.size();
        ASSERT_TRUE(g.perform(Action{ActionType::PickUp, {}, -1, {}}));
        EXPECT_EQ(g.hero().inv.items.size(), before + 1);
        EXPECT_TRUE(g.floor_items().empty())
            << "seed " << seed << ": the crossroads let the hero take more than one";
    }
}

TEST(Lobby, TheOfferIsDeterminedByTheSeed) {
    // Same seed, same three choices — the crossroads must not be a way to
    // reroll a run's opening.
    Game a = fresh(4242);
    Game b = fresh(4242);
    ASSERT_EQ(a.floor_items().size(), b.floor_items().size());
    for (std::size_t i = 0; i < a.floor_items().size(); ++i) {
        EXPECT_EQ(a.floor_items()[i].subtype, b.floor_items()[i].subtype);
        EXPECT_EQ(a.floor_items()[i].pos, b.floor_items()[i].pos);
    }
}

TEST(Lobby, EverythingInTheRoomIsReachableAndTheStairsLeadDown) {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        Game g = fresh(seed);
        const Map& map = g.map();
        EXPECT_EQ(map.at(g.level().exit), Tile::StairsDown) << "seed " << seed;

        // Flood from the hero and demand every offered item and the stairs.
        std::vector<Vec2> open{g.hero().a.pos};
        std::set<std::pair<int, int>> seen{{g.hero().a.pos.x, g.hero().a.pos.y}};
        while (!open.empty()) {
            const Vec2 p = open.back();
            open.pop_back();
            for (Vec2 d : directions8()) {
                const Vec2 q = p + d;
                if (!map.in_bounds(q) || !map.walkable(q)) continue;
                if (!seen.insert({q.x, q.y}).second) continue;
                open.push_back(q);
            }
        }
        EXPECT_TRUE(seen.count({g.level().exit.x, g.level().exit.y})) << "seed " << seed;
        for (const Item& it : g.floor_items())
            EXPECT_TRUE(seen.count({it.pos.x, it.pos.y}))
                << "seed " << seed << ": an offer the hero cannot walk to";
    }
}

TEST(Lobby, ClimbingBackFromTheFirstFloorReturnsToIt) {
    Game g = fresh(7);
    leave_crossroads(g);
    ASSERT_EQ(g.depth(), 1);

    g.mutable_level().map.set(g.hero().a.pos, Tile::StairsUp);
    ASSERT_TRUE(g.perform(Action{ActionType::Ascend, {}, -1, {}}));
    EXPECT_EQ(g.depth(), kLobbyDepth);
    // The crossroads is kept as it was left: at most Соловей, and only him —
    // it does not spontaneously grow a garrison while the hero is away.
    EXPECT_LE(g.monsters().size(), 1u) << "the crossroads grew monsters while the hero was away";
    for (const auto& m : g.monsters())
        EXPECT_EQ(m.species, species_index("solovey"));
}

TEST(Lobby, ARunThatStartsThereStillSavesAndLoads) {
    Game g = fresh(31);
    const std::string blob = g.save();
    Game back;
    ASSERT_TRUE(back.load(blob));
    EXPECT_EQ(back.depth(), kLobbyDepth);
    EXPECT_EQ(back.floor_items().size(), g.floor_items().size());
    EXPECT_TRUE(back.in_lobby());
}

// ---------------------------------------------------------------------------
// Соловей-Разбойник — the one thing on the crossroads that is met, not chosen.
// ---------------------------------------------------------------------------

TEST(Lobby, SoloveiWhistlesTheHeroOffTheRoadAndStunsThem) {
    Game g = fresh(3);
    Monster& s = g.mutable_level().monsters[0];
    ASSERT_EQ(s.species, species_index("solovey"));

    // Stand a few cells from him, in plain sight, and just wait.
    g.mutable_hero().a.pos = s.a.pos + Vec2{3, 0};
    g.mutable_hero().a.max_hp = g.mutable_hero().a.hp = 100;
    g.refresh_view();
    const Vec2 stood = g.hero().a.pos;

    bool whistled = false;
    for (int i = 0; i < 8 && !whistled; ++i) {
        g.perform(Action{ActionType::Wait, {}, -1, {}});
        if (g.hero().a.pos != stood || g.hero().a.has(Effect::Sleep)) whistled = true;
    }
    EXPECT_TRUE(whistled) << "Соловей never whistled";
    EXPECT_LT(g.hero().a.hp, 100) << "the whistle did no damage at all";
}

TEST(Lobby, SoloveiNeverLeavesHisOak) {
    Game g = fresh(5);
    const Vec2 perch = g.monsters()[0].a.pos;
    g.mutable_hero().a.pos = perch + Vec2{4, 0};
    g.mutable_hero().a.max_hp = g.mutable_hero().a.hp = 100;
    g.refresh_view();

    for (int i = 0; i < 12; ++i) g.perform(Action{ActionType::Wait, {}, -1, {}});

    ASSERT_FALSE(g.monsters().empty());
    EXPECT_EQ(g.monsters()[0].a.pos, perch) << "Соловей walked off his perch";
}

TEST(Lobby, SoloveiCanBePutDownAndThenTheRoadIsClear) {
    Game g = fresh(9);
    const Vec2 perch = g.monsters()[0].a.pos;
    g.mutable_hero().a.pos = perch + Vec2{-1, 0};   // right beside him
    g.mutable_hero().a.attack = 40;                 // a decisive arm
    g.mutable_hero().a.max_hp = g.mutable_hero().a.hp = 200;
    g.refresh_view();

    for (int i = 0; i < 20 && !g.monsters().empty(); ++i)
        g.perform(Action{ActionType::Move, step_towards(g.hero().a.pos, perch), -1, {}});

    EXPECT_TRUE(g.monsters().empty()) << "Соловей would not go down";
    EXPECT_EQ(g.hero().kills, 1);
}
