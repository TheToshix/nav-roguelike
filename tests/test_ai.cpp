// SPDX-License-Identifier: MIT
//
// Monster behaviour. Each AI flag in the bestiary gets a scenario that isolates
// it: an empty floor, one monster, and a hero who does nothing but wait.
#include <gtest/gtest.h>

#include <cstring>
#include <set>

#include "nav/game.hpp"

#include "support.hpp"

using namespace nav;

namespace {

/// An empty floor with one monster and a passive hero.
class Field {
public:
    explicit Field(std::uint64_t seed = 4004) {
        GameConfig cfg;
        cfg.seed = seed;
        cfg.hero_class = HeroClass::Vityaz;
        game.start(cfg);
        leave_crossroads(game);

        Level& lvl = game.mutable_level();
        lvl.monsters.clear();
        lvl.items.clear();
        for (int y = 1; y < lvl.map.height() - 1; ++y)
            for (int x = 1; x < lvl.map.width() - 1; ++x) {
                const Tile t = lvl.map.at({x, y});
                if (t != Tile::StairsUp && t != Tile::StairsDown) lvl.map.set({x, y}, Tile::Floor);
            }
        game.mutable_hero().a.pos = {20, 15};
        game.refresh_view();
        // Make the hero effectively unkillable: these tests are about what the
        // monster does, not about whether the hero survives it.
        game.mutable_hero().a.max_hp = 100000;
        game.mutable_hero().a.hp = 100000;
        game.mutable_hero().nutrition = 100000;
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
        game.mutable_level().monsters.push_back(m);
        game.refresh_view();
        return game.mutable_level().monsters.back();
    }

    void wait(int turns = 1) {
        for (int i = 0; i < turns; ++i) game.perform(Action{ActionType::Wait, {}, -1, {}});
    }

    Vec2 hero() const { return game.hero().a.pos; }
    bool any_monster() const { return !game.monsters().empty(); }

    Game game;
};

int distance_to_hero(const Game& g) {
    if (g.monsters().empty()) return -1;
    return chebyshev(g.monsters()[0].a.pos, g.hero().a.pos);
}

}  // namespace

TEST(Ai, ASleepingMonsterStaysPutUntilItNoticesTheHero) {
    Field f;
    Monster& m = f.spawn("upyr", {21, 15});
    m.awake = false;
    // Far enough that neither sight nor noise reaches it.
    m.a.pos = {60, 30};
    const Vec2 start = m.a.pos;

    f.wait(6);
    ASSERT_TRUE(f.any_monster());
    EXPECT_EQ(f.game.monsters()[0].a.pos, start) << "a sleeping monster wandered off";
    EXPECT_FALSE(f.game.monsters()[0].awake);
}

TEST(Ai, AMonsterWakesWhenTheHeroWalksIntoView) {
    Field f;
    Monster& m = f.spawn("upyr", {25, 15});  // well inside its sight radius
    m.awake = false;

    f.wait(3);
    ASSERT_TRUE(f.any_monster());
    EXPECT_TRUE(f.game.monsters()[0].awake) << "the monster never noticed the hero";
}

TEST(Ai, AMeleeMonsterClosesTheDistance) {
    Field f;
    Monster& m = f.spawn("upyr", {26, 15});
    m.awake = true;
    const int before = chebyshev(m.a.pos, f.hero());

    f.wait(4);
    ASSERT_TRUE(f.any_monster());
    EXPECT_LT(distance_to_hero(f.game), before) << "the monster made no progress towards the hero";
}

TEST(Ai, AMeleeMonsterEventuallyReachesAndStrikes) {
    Field f;
    f.spawn("upyr", {27, 15}).awake = true;
    const int hp_before = f.game.hero().a.hp;

    f.wait(30);
    EXPECT_LT(f.game.hero().a.hp, hp_before) << "the monster never landed a blow";
}

TEST(Ai, AFrozenHeroLosesTheTurn) {
    // NAV-020: Морозко's freeze says "your legs will not move", and now they do
    // not. Every action the frontend submits while the hero is frozen is dropped
    // and the turn passes.
    Field f;
    const Vec2 start = f.game.hero().a.pos;
    f.game.mutable_hero().a.add_effect(Effect::Freeze, 3, 1);

    for (int i = 0; i < 3; ++i)
        f.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
    EXPECT_EQ(f.game.hero().a.pos, start) << "a frozen hero walked anyway";

    // ... but the freeze does wear off.
    f.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
    EXPECT_EQ(f.game.hero().a.pos, (Vec2{start.x + 1, start.y}))
        << "the freeze never wore off";
}

TEST(Ai, AFrozenMonsterDoesNotMoveOrStrike) {
    Field f;
    Monster& m = f.spawn("upyr", {22, 15});
    m.awake = true;
    m.a.add_effect(Effect::Freeze, 30, 1);
    const Vec2 start = m.a.pos;
    const int hp_before = f.game.hero().a.hp;

    f.wait(8);
    ASSERT_TRUE(f.any_monster());
    EXPECT_EQ(f.game.monsters()[0].a.pos, start) << "a frozen monster moved";
    EXPECT_EQ(f.game.hero().a.hp, hp_before) << "a frozen monster attacked";
}

TEST(Ai, ACowardFleesOnceItIsBadlyHurt) {
    Field f;
    Monster& m = f.spawn("shishiga", {24, 15});
    m.awake = true;
    m.a.hp = 1;  // well under a third
    const int before = chebyshev(m.a.pos, f.hero());

    f.wait(5);
    ASSERT_TRUE(f.any_monster()) << "the coward died instead of running";
    EXPECT_GT(distance_to_hero(f.game), before) << "a wounded coward should back away";
}

TEST(Ai, AHealthyCowardStillComesForTheHero) {
    Field f;
    Monster& m = f.spawn("shishiga", {28, 15});
    m.awake = true;
    const int before = chebyshev(m.a.pos, f.hero());

    f.wait(4);
    ASSERT_TRUE(f.any_monster());
    EXPECT_LT(distance_to_hero(f.game), before);
}

TEST(Ai, ARangedAttackerHurtsTheHeroFromADistance) {
    Field f;
    Monster& m = f.spawn("aspid", {26, 15});
    m.awake = true;
    const int hp_before = f.game.hero().a.hp;

    // Long enough for the 60% firing chance to land several times.
    f.wait(6);
    EXPECT_LT(f.game.hero().a.hp, hp_before) << "the ranged monster never fired";
}

TEST(Ai, ARangedAttackerNeedsLineOfSight) {
    Field f;
    Monster& m = f.spawn("aspid", {26, 15});
    m.awake = true;
    // Seal it inside a small chamber: it can neither see nor reach the hero.
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            if (std::abs(dx) == 2 || std::abs(dy) == 2)
                f.game.mutable_level().map.set({26 + dx, 15 + dy}, Tile::Wall);

    const int hp_before = f.game.hero().a.hp;
    f.wait(12);
    EXPECT_EQ(f.game.hero().a.hp, hp_before) << "a walled-in monster hit the hero anyway";
}

TEST(Ai, ASummonerCallsReinforcements) {
    Field f;
    Monster& m = f.spawn("koldun", {25, 15});
    m.awake = true;

    f.wait(25);
    EXPECT_GT(f.game.monsters().size(), 1u) << "the summoner never called anyone";
}

TEST(Ai, ASummonerNeverCallsSomethingStrongerThanItself) {
    // Otherwise Баба-Яга can fill her room with Кощеи, which is not a fight.
    Field f;
    Monster& m = f.spawn("koldun", {25, 15});
    m.awake = true;
    const int own_xp = bestiary()[static_cast<std::size_t>(m.species)].xp;

    f.wait(60);
    for (const auto& monster : f.game.monsters()) {
        const auto& sp = bestiary()[static_cast<std::size_t>(monster.species)];
        EXPECT_FALSE(sp.ai & AiBoss) << sp.key << " was summoned";
        EXPECT_LE(sp.xp, own_xp) << sp.key << " is stronger than the summoner";
    }
}

TEST(Ai, MonstersNeverEndUpSharingACell) {
    Field f;
    // A crowd funnelling towards one hero is where collision bugs surface.
    for (int i = 0; i < 12; ++i) f.spawn("upyr", {24 + (i % 4), 12 + (i / 4)}).awake = true;

    for (int turn = 0; turn < 40; ++turn) {
        f.wait(1);
        std::set<std::pair<int, int>> seen;
        for (const auto& m : f.game.monsters()) {
            EXPECT_TRUE(seen.insert({m.a.pos.x, m.a.pos.y}).second)
                << "turn " << turn << ": two monsters on (" << m.a.pos.x << "," << m.a.pos.y << ")";
            EXPECT_NE(m.a.pos, f.hero()) << "turn " << turn << ": a monster stood on the hero";
        }
    }
}

TEST(Ai, MonstersPushDoorsOpenOnTheirWayThrough) {
    Field f;
    // A corridor with a door, and the monster on the far side of it.
    Level& lvl = f.game.mutable_level();
    for (int y = 1; y < lvl.map.height() - 1; ++y)
        for (int x = 1; x < lvl.map.width() - 1; ++x)
            if (y != 15) lvl.map.set({x, y}, Tile::Wall);
    lvl.map.set({24, 15}, Tile::Door);
    Monster& walker = f.spawn("upyr", {27, 15});
    walker.awake = true;
    // A closed door blocks sight, so the monster is heading for the hero's last
    // known position rather than following one it can see.
    walker.last_seen = f.hero();
    walker.search_turns = 12;

    f.wait(20);
    EXPECT_EQ(f.game.map().at({24, 15}), Tile::OpenDoor) << "the door was never opened";
}

TEST(Ai, AConfusedMonsterLosesTrackOfTheHero) {
    // Averaged over many trials a confused monster should close far less
    // reliably than a clear-headed one.
    int confused_closed = 0, clear_closed = 0;
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        {
            Field f(seed + 10);
            Monster& m = f.spawn("upyr", {26, 15});
            m.awake = true;
            m.a.add_effect(Effect::Confusion, 200, 1);
            const int before = chebyshev(m.a.pos, f.hero());
            f.wait(6);
            if (!f.game.monsters().empty() && distance_to_hero(f.game) < before) ++confused_closed;
        }
        {
            Field f(seed + 10);
            f.spawn("upyr", {26, 15}).awake = true;
            const int before = chebyshev(f.game.monsters()[0].a.pos, f.hero());
            f.wait(6);
            if (!f.game.monsters().empty() && distance_to_hero(f.game) < before) ++clear_closed;
        }
    }
    EXPECT_EQ(clear_closed, 20) << "a clear-headed monster should always close the distance";
    EXPECT_LT(confused_closed, 20) << "confusion had no effect on movement at all";
}

TEST(Ai, AnErraticMonsterDoesNotBeelineForTheHero) {
    // Анчутка moves randomly about half the time, so over a short window it
    // should sometimes fail to make progress where a plain chaser never does.
    int wandered = 0;
    for (std::uint64_t seed = 0; seed < 25; ++seed) {
        Field f(seed + 200);
        Monster& m = f.spawn("anchutka", {30, 15});
        m.awake = true;
        const int before = chebyshev(m.a.pos, f.hero());
        f.wait(3);
        if (f.game.monsters().empty()) continue;
        if (distance_to_hero(f.game) >= before) ++wandered;
    }
    EXPECT_GT(wandered, 0) << "an erratic monster behaved exactly like a chaser";
}

TEST(Ai, BossesAreAwakeFromTheStart) {
    GameConfig cfg;
    cfg.seed = 8081;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    for (int depth = 1; depth < 4; ++depth) {
        g.mutable_hero().a.pos = g.level().exit;
        ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
    ASSERT_EQ(g.depth(), 4);

    bool found = false;
    for (const auto& m : g.monsters()) {
        const auto& sp = bestiary()[static_cast<std::size_t>(m.species)];
        if (!(sp.ai & AiBoss)) continue;
        found = true;
        EXPECT_TRUE(m.awake) << sp.key << " was asleep on its own floor";
    }
    EXPECT_TRUE(found) << "no boss on floor 4";
}

TEST(Ai, MonstersDoNotFollowTheHeroBetweenFloors) {
    Field f;
    f.spawn("upyr", {22, 15}).awake = true;
    ASSERT_EQ(f.game.monsters().size(), 1u);
    const Vec2 left_behind = f.game.monsters()[0].a.pos;

    f.game.mutable_hero().a.pos = f.game.level().exit;
    f.game.refresh_view();
    ASSERT_TRUE(f.game.perform(Action{ActionType::Descend, {}, -1, {}}));
    ASSERT_EQ(f.game.depth(), 2);

    // Floor two has its own population; the pursuer stayed upstairs.
    EXPECT_GT(f.game.monsters().size(), 0u);
    bool same_spot = false;
    for (const auto& m : f.game.monsters())
        if (m.a.pos == left_behind) same_spot = true;
    EXPECT_FALSE(same_spot) << "the monster appears to have followed the hero down";

    // And it is still there when the hero climbs back.
    f.game.mutable_hero().a.pos = f.game.level().entrance;
    f.game.refresh_view();
    ASSERT_TRUE(f.game.perform(Action{ActionType::Ascend, {}, -1, {}}));
    ASSERT_EQ(f.game.depth(), 1);
    EXPECT_EQ(f.game.monsters().size(), 1u) << "the floor was repopulated instead of restored";
}

TEST(Ai, KillingEverythingLeavesAQuietFloor) {
    Field f;
    for (int i = 0; i < 5; ++i) f.spawn("anchutka", {22 + i, 15}).a.hp = 1;

    // Hunt: step towards the nearest monster and bump it. Анчутка moves
    // erratically, so simply swinging in one direction is not enough.
    for (int turn = 0; turn < 400 && !f.game.monsters().empty(); ++turn) {
        const Vec2 me = f.game.hero().a.pos;
        Vec2 target = f.game.monsters()[0].a.pos;
        int best = dist_sq(me, target);
        for (const auto& m : f.game.monsters()) {
            const int d = dist_sq(me, m.a.pos);
            if (d < best) { best = d; target = m.a.pos; }
        }
        if (!f.game.perform(Action{ActionType::Move, step_towards(me, target), -1, {}}))
            f.game.perform(Action{ActionType::Wait, {}, -1, {}});
    }

    EXPECT_TRUE(f.game.monsters().empty()) << "some monsters survived a long hunt";
    EXPECT_EQ(f.game.hero().kills, 5);
}

// ---------------------------------------------------------------------------
// Огневик — Пекло's tactical device: it leaves the floor burning behind it,
// the way venom is Чернотопь's answer and the freeze is Кощеево царство's.
// ---------------------------------------------------------------------------

namespace {
int ember_count(const Game& g) { return static_cast<int>(g.level().embers.size()); }
}  // namespace

TEST(Ai, TheCinderlingLeavesABurningTrailAsItMoves) {
    Field f;
    Monster& m = f.spawn("ognevik", {28, 15});
    m.awake = true;

    // Long enough to have moved several cells, short enough that the earliest
    // embers have not yet burned out: what we are checking is that more than one
    // cell is alight at once, i.e. that it trails fire rather than lighting a
    // single spot.
    f.wait(3);

    EXPECT_GE(ember_count(f.game), 2)
        << "the Огневик walked without setting the floor alight behind it";
}

TEST(Ai, CinderlingFireBurnsWhoeverStandsInIt) {
    Field f;
    Hero& h = f.game.mutable_hero();
    h.a.max_hp = h.a.hp = 200;   // the Field hero is otherwise unkillable

    f.game.ignite(f.game.hero().a.pos, 3);
    const int before = f.game.hero().a.hp;
    f.wait(1);

    EXPECT_LT(f.game.hero().a.hp, before) << "standing in fire did no damage";
}

TEST(Ai, CinderlingFireBurnsItselfOut) {
    Field f;
    // Somewhere the hero is not standing, so only the countdown ends it.
    const Vec2 spot{30, 15};
    f.game.ignite(spot, 3);
    ASSERT_GT(f.game.ember_at(spot), 0);

    f.wait(5);
    EXPECT_EQ(f.game.ember_at(spot), 0) << "the fire never went out";
    EXPECT_EQ(ember_count(f.game), 0);
}

TEST(Ai, TheCinderlingWalksItsOwnFireUnharmed) {
    Field f;
    Monster& m = f.spawn("ognevik", {24, 15});
    m.awake = true;
    // Pin it in place so it sits in the fire it makes rather than wandering off.
    m.a.add_effect(Effect::Freeze, 20, 1);
    const int hp = m.a.hp;
    f.game.ignite(m.a.pos, 3);

    f.wait(4);

    ASSERT_TRUE(f.any_monster()) << "the Огневик burned itself to death";
    EXPECT_EQ(f.game.monsters()[0].a.hp, hp) << "its own fire hurt it";
}

TEST(Ai, CinderlingFireBurnsOtherCreaturesLedIntoIt) {
    Field f;
    Monster& other = f.spawn("upyr", {30, 15});
    other.a.add_effect(Effect::Freeze, 20, 1);   // hold it on the spot
    const int hp = other.a.hp;

    f.game.ignite(other.a.pos, 3);
    f.wait(2);

    ASSERT_TRUE(f.any_monster());
    EXPECT_LT(f.game.monsters()[0].a.hp, hp)
        << "an ordinary creature standing in the trail took no fire damage";
}

// ---------------------------------------------------------------------------
// Домовой — the first creature that is not automatically an enemy.
// ---------------------------------------------------------------------------

TEST(Ai, TheDomovoyNeverStrikesFirst) {
    Field f;
    Hero& h = f.game.mutable_hero();
    h.a.max_hp = h.a.hp = 200;
    f.spawn("domovoy", {21, 15});   // right next to the hero

    const int before = f.game.hero().a.hp;
    f.wait(3);   // fewer turns than its patience: it should just stand there

    EXPECT_EQ(f.game.hero().a.hp, before) << "the Домовой attacked an idle hero";
}

TEST(Ai, TheDomovoyStaysWhereItLives) {
    Field f;
    Monster& m = f.spawn("domovoy", {40, 26});   // out of the hero's way
    const Vec2 home = m.a.pos;

    f.wait(12);

    ASSERT_TRUE(f.any_monster());
    EXPECT_EQ(f.game.monsters()[0].a.pos, home) << "the Домовой wandered off";
}

TEST(Ai, PassingTheDomovoyWithoutTroubleEarnsABoon) {
    Field f;
    f.spawn("domovoy", {23, 15});   // a few cells away, in plain sight

    for (int i = 0; i < 10 && f.any_monster(); ++i)
        f.game.perform(Action{ActionType::Wait, {}, -1, {}});

    EXPECT_FALSE(f.any_monster()) << "the Домовой never left";
    EXPECT_EQ(f.game.hero().kills, 0) << "a parting gift is not a kill";
    EXPECT_TRUE(f.game.hero().a.has(Effect::Haste) || f.game.hero().a.has(Effect::Shield))
        << "the Домовой left without a blessing";
}

TEST(Ai, StrikingTheDomovoyTurnsItHostile) {
    Field f;
    Hero& h = f.game.mutable_hero();
    h.a.max_hp = h.a.hp = 300;
    Monster& m = f.spawn("domovoy", {21, 15});
    m.a.hp = m.a.max_hp = 200;   // it has to outlast the blow that provokes it

    // One deliberate blow.
    f.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
    ASSERT_EQ(f.game.monsters().size(), 1u);
    EXPECT_EQ(f.game.monsters()[0].revives, 1) << "the blow did not provoke it";

    const int before = f.game.hero().a.hp;
    f.wait(6);
    EXPECT_LT(f.game.hero().a.hp, before) << "a provoked Домовой still would not fight back";
    EXPECT_FALSE(f.game.hero().a.has(Effect::Haste));
    EXPECT_FALSE(f.game.hero().a.has(Effect::Shield));
}

// ---------------------------------------------------------------------------
// Кот Баюн — the optional mini-boss in the hidden room, and his song.
// ---------------------------------------------------------------------------

TEST(Ai, ASleepingHeroLosesEveryTurn) {
    // Sleep is a full input lock, distinct from confusion (which randomises a
    // move) and from freeze (which is another cause of the same lock).
    Field f;
    const Vec2 start = f.game.hero().a.pos;
    f.game.mutable_hero().a.add_effect(Effect::Sleep, 3, 1);

    for (int i = 0; i < 3; ++i)
        f.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
    EXPECT_EQ(f.game.hero().a.pos, start) << "a sleeping hero walked anyway";

    f.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
    EXPECT_EQ(f.game.hero().a.pos, (Vec2{start.x + 1, start.y})) << "the sleep never wore off";
}

TEST(Ai, KotBayunSingsTheHeroToSleepFromAcrossTheRoom) {
    Field f;
    Hero& h = f.game.mutable_hero();
    h.a.max_hp = h.a.hp = 1000;
    f.spawn("kot_bayun", {27, 15});   // in sight, out of melee for the first turns

    bool slept = false;
    for (int i = 0; i < 12 && !slept; ++i) {
        f.game.perform(Action{ActionType::Wait, {}, -1, {}});
        if (f.game.hero().a.has(Effect::Sleep)) slept = true;
    }
    EXPECT_TRUE(slept) << "the cat never got the song off";
}

TEST(Ai, TheCatsEyeCharmKeepsTheSongOut) {
    Field f;
    Hero& h = f.game.mutable_hero();
    h.a.max_hp = h.a.hp = 400;

    // Wear the charm straight into the slot.
    const auto& gear = gear_table();
    int ci = -1;
    for (std::size_t i = 0; i < gear.size(); ++i)
        if (std::string(gear[i].key) == "koshkin_glaz") ci = static_cast<int>(i);
    ASSERT_GE(ci, 0);
    Item charm{};
    charm.kind = ItemKind::Amulet;
    charm.subtype = ci;
    charm.power = gear[static_cast<std::size_t>(ci)].power;
    charm.identified = true;
    ASSERT_TRUE(h.inv.add(charm));
    h.inv.amulet = static_cast<int>(h.inv.items.size()) - 1;
    ASSERT_TRUE(f.game.hero_has(GpNoSleep));

    f.spawn("kot_bayun", {27, 15});
    for (int i = 0; i < 20; ++i) f.game.perform(Action{ActionType::Wait, {}, -1, {}});

    EXPECT_FALSE(f.game.hero().a.has(Effect::Sleep)) << "the charm let the song in";
}

// ---------------------------------------------------------------------------
// Жар-птица — not a fight. Catch it, do not swing at it.
// ---------------------------------------------------------------------------

TEST(Ai, TheFirebirdFleesAndNeverAttacks) {
    Field f;
    Hero& h = f.game.mutable_hero();
    h.a.max_hp = h.a.hp = 200;
    Monster& m = f.spawn("zharptica", {24, 15});
    const int gap_before = chebyshev(m.a.pos, f.game.hero().a.pos);

    f.wait(4);

    ASSERT_TRUE(f.any_monster()) << "it left without being caught or struck";
    EXPECT_GT(chebyshev(f.game.monsters()[0].a.pos, f.game.hero().a.pos), gap_before)
        << "the Firebird did not back away";
    EXPECT_EQ(f.game.hero().a.hp, 200) << "the Firebird attacked";
}

TEST(Ai, CatchingUpToTheFirebirdEarnsAFeather) {
    Field f;
    f.spawn("zharptica", {22, 15});   // two cells ahead of the hero

    // One step closes the gap to one; on its next turn the bird gives up.
    f.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});

    EXPECT_FALSE(f.any_monster()) << "the Firebird stayed after being caught";
    bool feather = false;
    for (const Item& it : f.game.floor_items())
        if (it.kind == ItemKind::Feather) feather = true;
    EXPECT_TRUE(feather) << "catching it left no feather";
    EXPECT_EQ(f.game.hero().kills, 0) << "catching it is not a kill";
}

TEST(Ai, SwingingAtTheFirebirdScaresItOffWithNothing) {
    Field f;
    f.spawn("zharptica", {21, 15});   // right next to the hero
    f.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});   // a blow, not a catch

    EXPECT_FALSE(f.any_monster());
    for (const Item& it : f.game.floor_items())
        EXPECT_NE(it.kind, ItemKind::Feather) << "a struck Firebird still dropped a feather";
    EXPECT_EQ(f.game.hero().kills, 0);
}

// ---------------------------------------------------------------------------
// The codex — a bestiary row unlocks the first time the creature is in sight.
// ---------------------------------------------------------------------------

TEST(Ai, TheCodexUnlocksARowOnFirstSight) {
    Field f;
    const int upyr = species_index("upyr");
    const int aspid = species_index("aspid");
    ASSERT_FALSE(f.game.codex_knows(upyr)) << "the row was open before the creature was seen";

    f.spawn("upyr", {24, 15});   // spawn() calls refresh_view()
    EXPECT_TRUE(f.game.codex_knows(upyr)) << "seeing an upyr did not unlock its row";
    EXPECT_FALSE(f.game.codex_knows(aspid)) << "an unrelated row opened itself";
}

TEST(Ai, TheCodexIgnoresCreaturesOutOfSight) {
    Field f;
    const int volkolak = species_index("volkolak");
    // Spawned far across the floor, well outside the hero's sight radius.
    f.spawn("volkolak", {66, 31});
    EXPECT_FALSE(f.game.codex_knows(volkolak))
        << "a creature the hero cannot see unlocked its row";
}
