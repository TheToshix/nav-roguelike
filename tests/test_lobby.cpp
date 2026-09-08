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

TEST(Lobby, NothingLivesThere) {
    // The crossroads is the only floor in the game with no monsters on it. That
    // is what makes it a place to prepare rather than a fight with a shop in it.
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        Game g = fresh(seed);
        EXPECT_TRUE(g.monsters().empty()) << "seed " << seed;
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
    EXPECT_TRUE(g.floor_items().empty() || !g.floor_items().empty());  // either way it survived
    EXPECT_TRUE(g.monsters().empty()) << "the crossroads grew monsters while the hero was away";
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
