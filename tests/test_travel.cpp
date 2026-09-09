// SPDX-License-Identifier: MIT
//
// Running and auto-exploring.
//
// A travel command is only as good as its stop rules, so that is what these
// tests are about. Every rule gets its own scenario built by hand: a corridor
// with a branch in it, a hero who takes damage mid-run, a monster stepping into
// view. The happy path — "it walks several squares" — is the least interesting
// thing here and the easiest to get right.
#include <gtest/gtest.h>

#include "nav/game.hpp"

#include "support.hpp"

namespace nav {
namespace {

/// A floor made of nothing but wall, so a test can carve exactly the shape it
/// wants to reason about. Building the map by hand is the only way to write a
/// test about corridors that does not depend on what the generator felt like
/// producing that day.
class Carved {
public:
    explicit Carved(std::uint64_t seed = 909) {
        GameConfig cfg;
        cfg.seed = seed;
        game.start(cfg);
        leave_crossroads(game);

        Level& lvl = game.mutable_level();
        lvl.monsters.clear();
        lvl.items.clear();
        for (int y = 0; y < lvl.map.height(); ++y)
            for (int x = 0; x < lvl.map.width(); ++x) lvl.map.set({x, y}, Tile::Wall);
        // The stairs have to go somewhere the tests do not walk over.
        lvl.exit = {1, 1};
        lvl.map.set(lvl.exit, Tile::StairsDown);
        game.mutable_hero().nutrition = 100000;
    }

    /// Carves a horizontal corridor, inclusive of both ends.
    void row(int y, int x0, int x1) {
        for (int x = x0; x <= x1; ++x) game.mutable_level().map.set({x, y}, Tile::Floor);
    }
    void col(int x, int y0, int y1) {
        for (int y = y0; y <= y1; ++y) game.mutable_level().map.set({x, y}, Tile::Floor);
    }
    void box(int x0, int y0, int x1, int y1) {
        for (int y = y0; y <= y1; ++y) row(y, x0, x1);
    }

    void put_hero(Vec2 p) {
        game.mutable_hero().a.pos = p;
        game.refresh_view();
    }

    Monster& spawn(const char* key, Vec2 pos) {
        const int index = species_index(key);
        EXPECT_GE(index, 0) << key;
        const Species& sp = bestiary()[static_cast<std::size_t>(index)];
        Monster m{};
        m.species = index;
        m.a.pos = pos;
        m.a.hp = m.a.max_hp = sp.hp;
        m.a.attack = sp.attack;
        m.a.defence = sp.defence;
        m.a.speed = sp.speed;
        m.awake = true;
        game.mutable_level().monsters.push_back(m);
        game.refresh_view();
        return game.mutable_level().monsters.back();
    }

    bool run(Vec2 dir) { return game.perform(Action{ActionType::Run, dir, -1, {}}); }
    bool explore() { return game.perform(Action{ActionType::Explore, {}, -1, {}}); }
    Vec2 where() const { return game.hero().a.pos; }

    Game game;
};

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

TEST(Travel, ARunCrossesAWholeCorridorInOneKeypress) {
    Carved c;
    c.row(10, 5, 25);
    c.put_hero({5, 10});

    EXPECT_TRUE(c.run({1, 0}));
    EXPECT_EQ(c.where().x, 25) << "the run stopped somewhere in the middle of a plain corridor";
    EXPECT_EQ(c.where().y, 10);
}

TEST(Travel, ARunCostsTheTurnsItTakes) {
    // The whole feature is a convenience for the player's finger, never for the
    // hero: walking twenty squares takes twenty turns of hunger and of monster
    // movement whether one key did it or twenty did.
    Carved c;
    c.row(10, 5, 25);
    c.put_hero({5, 10});
    const int before = c.game.turn();

    ASSERT_TRUE(c.run({1, 0}));
    EXPECT_GE(c.game.turn() - before, 20) << "twenty squares came free";
}

TEST(Travel, ARunStopsWhereTheCorridorBranches) {
    Carved c;
    c.row(10, 5, 25);
    c.col(15, 5, 10);   // a side passage going up, at x = 15
    c.put_hero({5, 10});

    ASSERT_TRUE(c.run({1, 0}));
    EXPECT_EQ(c.where().x, 15) << "the run walked straight past a junction";
}

TEST(Travel, ARunStopsWhenSomethingComesIntoView) {
    Carved c;
    c.row(10, 5, 40);
    c.put_hero({5, 10});
    c.spawn("anchutka", {30, 10});

    ASSERT_TRUE(c.run({1, 0}));
    EXPECT_LT(c.where().x, 30) << "the hero ran into the monster's arms";
}

TEST(Travel, ARunStopsOnAnItem) {
    Carved c;
    c.row(10, 5, 25);
    Item gold{};
    gold.kind = ItemKind::Gold;
    gold.count = 30;
    gold.identified = true;
    gold.pos = {12, 10};
    c.game.mutable_level().items.push_back(gold);
    c.put_hero({5, 10});

    ASSERT_TRUE(c.run({1, 0}));
    EXPECT_EQ(c.where().x, 12) << "the run walked over something worth picking up";
}

TEST(Travel, ARunStopsOnTheStairs) {
    Carved c;
    c.row(10, 5, 25);
    c.game.mutable_level().map.set({14, 10}, Tile::StairsDown);
    c.put_hero({5, 10});

    ASSERT_TRUE(c.run({1, 0}));
    EXPECT_EQ(c.where().x, 14);
}

TEST(Travel, ARunStopsWhenTheHeroIsHurt) {
    // The hero is poisoned before setting off. Nothing is in sight, the
    // corridor is straight — the only thing that changes is the health bar, and
    // that alone has to be enough to stop the run.
    Carved c;
    c.row(10, 5, 40);
    c.put_hero({5, 10});
    c.game.mutable_hero().a.effects.push_back(ActiveEffect{Effect::Poison, 40, 2});

    ASSERT_TRUE(c.run({1, 0}));
    EXPECT_LT(c.where().x, 40) << "the hero ran the corridor out while being poisoned";
}

TEST(Travel, RunningIntoAFoeIsJustAnAttack) {
    // With a monster already in view a run degrades to a single step, so that
    // Shift and a direction still does the obvious thing rather than nothing.
    Carved c;
    c.row(10, 5, 25);
    c.put_hero({5, 10});
    Monster& m = c.spawn("anchutka", {6, 10});
    // The point of the test is that the bump *is* an attack, so the foe has to
    // outlast one blow — an anchutka's six health does not, and a corpse is
    // reaped before the assertion below can read it, leaving `monsters()[0]` a
    // read past the end of the vector (see docs/BUG_REPORTS.md, NAV-018).
    m.a.hp = m.a.max_hp = 60;
    const int hp = m.a.hp;

    EXPECT_TRUE(c.run({1, 0}));
    EXPECT_EQ(c.where().x, 5) << "the hero walked through the monster instead of hitting it";
    ASSERT_EQ(c.game.monsters().size(), 1u) << "the foe should have survived a single blow";
    EXPECT_LT(c.game.monsters()[0].a.hp, hp) << "the bump did no damage";
}

TEST(Travel, ARunIntoAWallGoesNowhereAndCostsNothing) {
    Carved c;
    c.row(10, 5, 25);
    c.put_hero({5, 10});
    const int before = c.game.turn();

    EXPECT_FALSE(c.run({-1, 0}));
    EXPECT_EQ(c.where().x, 5);
    EXPECT_EQ(c.game.turn(), before) << "walking into a wall took a turn";
}

TEST(Travel, ARunInAnOpenRoomCrossesIt) {
    // In a room every cell has neighbours in every direction, so the corridor
    // rule would stop the run on its first step. A room run should behave the
    // way a player expects: straight on until the far wall.
    Carved c;
    c.box(5, 5, 25, 20);
    c.put_hero({6, 12});

    ASSERT_TRUE(c.run({1, 0}));
    EXPECT_EQ(c.where().x, 25) << "the run stalled inside an open room";
}

// ---------------------------------------------------------------------------
// Auto-explore
// ---------------------------------------------------------------------------

TEST(Travel, ExploringWalksTowardsWhatHasNotBeenSeen) {
    Carved c;
    c.row(10, 5, 40);
    c.put_hero({5, 10});
    const int seen_before = [&] {
        int n = 0;
        for (int x = 0; x < c.game.map().width(); ++x)
            for (int y = 0; y < c.game.map().height(); ++y)
                if (c.game.map().explored({x, y})) ++n;
        return n;
    }();

    ASSERT_TRUE(c.explore());
    int seen_after = 0;
    for (int x = 0; x < c.game.map().width(); ++x)
        for (int y = 0; y < c.game.map().height(); ++y)
            if (c.game.map().explored({x, y})) ++seen_after;
    EXPECT_GT(seen_after, seen_before) << "exploring revealed nothing";
}

TEST(Travel, ExploringRefusesWhileSomethingIsWatching) {
    Carved c;
    c.row(10, 5, 40);
    c.put_hero({5, 10});
    c.spawn("anchutka", {8, 10});
    const Vec2 before = c.where();

    EXPECT_FALSE(c.explore());
    EXPECT_EQ(c.where().x, before.x) << "auto-explore strolled off with a monster in the room";
    EXPECT_EQ(c.where().y, before.y);
}

TEST(Travel, ExploringAFullySeenFloorHeadsForTheStairs) {
    Carved c;
    c.row(10, 5, 25);
    c.col(5, 2, 10);
    c.game.mutable_level().map.set({5, 2}, Tile::StairsDown);
    c.game.mutable_level().exit = {5, 2};
    c.put_hero({25, 10});
    c.game.mutable_level().map.reveal_all();

    ASSERT_TRUE(c.explore());
    EXPECT_EQ(c.where().x, 5);
    EXPECT_EQ(c.where().y, 2) << "with nothing left to find, exploring should end on the stairs";
}

TEST(Travel, ExploringStandingOnTheStairsOfASeenFloorDoesNothing) {
    Carved c;
    c.row(10, 5, 25);
    c.game.mutable_level().map.set({5, 10}, Tile::StairsDown);
    c.game.mutable_level().exit = {5, 10};
    c.put_hero({5, 10});
    c.game.mutable_level().map.reveal_all();

    EXPECT_FALSE(c.explore()) << "there was nowhere left to go, and it went anyway";
}

// ---------------------------------------------------------------------------
// The stop rule itself
// ---------------------------------------------------------------------------
//
// `situation()` and `situation_changed()` are read by three callers: the
// engine's own run, a key held down in the terminal, and a key held down in the
// browser. That makes the rule worth testing directly rather than only through
// the behaviour of one of its callers.

TEST(Travel, TheSituationNoticesEachThingSeparately) {
    Carved c;
    c.row(10, 5, 30);
    c.put_hero({10, 10});
    const Game::Situation calm = c.game.situation();
    EXPECT_FALSE(calm.foes);
    EXPECT_FALSE(calm.underfoot);
    EXPECT_FALSE(c.game.situation_changed(calm)) << "nothing happened, and it says otherwise";

    // A wound.
    {
        Carved d;
        d.row(10, 5, 30);
        d.put_hero({10, 10});
        const Game::Situation before = d.game.situation();
        d.game.mutable_hero().a.hp -= 1;
        EXPECT_TRUE(d.game.situation_changed(before)) << "losing health went unnoticed";
    }
    // Someone in view.
    {
        Carved d;
        d.row(10, 5, 30);
        d.put_hero({10, 10});
        const Game::Situation before = d.game.situation();
        d.spawn("anchutka", {14, 10});
        EXPECT_TRUE(d.game.situation_changed(before)) << "a monster appeared and nobody noticed";
        EXPECT_TRUE(d.game.situation().foes);
    }
    // A new effect.
    {
        Carved d;
        d.row(10, 5, 30);
        d.put_hero({10, 10});
        const Game::Situation before = d.game.situation();
        d.game.mutable_hero().a.effects.push_back(ActiveEffect{Effect::Poison, 10, 2});
        EXPECT_TRUE(d.game.situation_changed(before)) << "poison took hold quietly";
    }
    // Something underfoot.
    {
        Carved d;
        d.row(10, 5, 30);
        d.put_hero({10, 10});
        const Game::Situation before = d.game.situation();
        d.game.mutable_level().map.set({10, 10}, Tile::StairsDown);
        EXPECT_TRUE(d.game.situation_changed(before)) << "the stairs went unnoticed underfoot";
    }
}

TEST(Travel, TheSituationDoesNotFireOnGoodNewsThatChangesNothing) {
    // Healing is not a reason to stop walking, and neither is an effect wearing
    // off. Stopping on everything is the same failure as stopping on nothing:
    // the rule stops meaning anything.
    Carved c;
    c.row(10, 5, 30);
    c.put_hero({10, 10});
    c.game.mutable_hero().a.hp = 5;
    const Game::Situation before = c.game.situation();
    c.game.mutable_hero().a.heal(5);
    EXPECT_FALSE(c.game.situation_changed(before)) << "getting better stopped the walk";
}

TEST(Travel, TravelNeverBreaksTheInvariants) {
    // The same per-turn checks the long randomised run makes, applied to a
    // hundred travel commands on real generated floors. A travel command is a
    // loop over the ordinary rules, and this is what says so out loud.
    GameConfig cfg;
    cfg.seed = 20260908;
    Game g;
    g.start(cfg);
    leave_crossroads(g);

    Rng rng(4242);
    for (int i = 0; i < 100 && g.state() == RunState::Playing; ++i) {
        const int dirs[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,-1},{1,-1},{-1,1}};
        const int pick = rng.below(9);
        if (pick == 8) g.perform(Action{ActionType::Explore, {}, -1, {}});
        else g.perform(Action{ActionType::Run, Vec2{dirs[pick][0], dirs[pick][1]}, -1, {}});

        const Hero& h = g.hero();
        ASSERT_GE(h.a.hp, 0);
        ASSERT_LE(h.a.hp, h.a.max_hp);
        ASSERT_TRUE(g.map().walkable(h.a.pos)) << "the hero ended a run inside a wall";
        for (const Monster& m : g.monsters()) {
            ASSERT_TRUE(m.a.alive) << "a corpse survived a travel command";
            ASSERT_FALSE(m.a.pos == h.a.pos) << "a monster is standing on the hero";
        }
    }
}

}  // namespace
}  // namespace nav
