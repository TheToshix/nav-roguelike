// SPDX-License-Identifier: MIT
//
// Gear that does something, and the sets it belongs to.
//
// A piece of equipment whose only property is a bigger number needs no tests.
// These are for the twelve that carry a mechanic, and for the rule that makes
// three of them into a fourth thing.
#include <gtest/gtest.h>

#include <cstring>

#include "nav/game.hpp"

#include "support.hpp"

using namespace nav;

namespace {

int gear_index(const char* key) {
    const auto& gear = gear_table();
    for (std::size_t i = 0; i < gear.size(); ++i)
        if (std::strcmp(gear[i].key, key) == 0) return static_cast<int>(i);
    return -1;
}

/// A cleared floor with a hero who can be dressed piece by piece.
class Wardrobe {
public:
    explicit Wardrobe(std::uint64_t seed = 909) {
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
        game.mutable_hero().nutrition = 100000;
        game.refresh_view();
    }

    /// Puts `key` straight into the matching slot.
    void wear(const char* key) {
        const int idx = gear_index(key);
        ASSERT_GE(idx, 0) << key;
        const GearTemplate& g = gear_table()[static_cast<std::size_t>(idx)];
        Item it{};
        it.kind = g.kind;
        it.subtype = idx;
        it.power = g.power;
        it.identified = true;
        Inventory& inv = game.mutable_hero().inv;
        ASSERT_TRUE(inv.add(it));
        const int at = static_cast<int>(inv.items.size()) - 1;
        switch (g.kind) {
            case ItemKind::Weapon: inv.weapon = at; break;
            case ItemKind::Armor:  inv.armor = at; break;
            case ItemKind::Amulet: inv.amulet = at; break;
            default: FAIL() << key << " is not something you can wear";
        }
    }

    Monster& spawn(const char* key, Vec2 pos, int hp = -1) {
        const int index = species_index(key);
        EXPECT_GE(index, 0) << key;
        const Species& sp = bestiary()[static_cast<std::size_t>(index)];
        Monster m{};
        m.species = index;
        m.a.pos = pos;
        m.a.hp = m.a.max_hp = hp > 0 ? hp : sp.hp;
        m.a.attack = sp.attack;
        m.a.defence = sp.defence;
        m.a.speed = sp.speed;
        m.awake = true;
        game.mutable_level().monsters.push_back(m);
        game.refresh_view();
        return game.mutable_level().monsters.back();
    }

    Game game;
};

}  // namespace

// ---------------------------------------------------------------------------
// The tables themselves
// ---------------------------------------------------------------------------

TEST(Gear, EverySetIsExactlyOneWeaponOneArmourAndOneAmulet) {
    // Three slots and three pieces is the whole design: completing a set costs
    // the hero every slot they have. A set with two amulets in it could never
    // be worn, and a set with four pieces would be a lie in the inventory.
    for (int s = 1; s < static_cast<int>(GearSet::Count); ++s) {
        const GearSet set = static_cast<GearSet>(s);
        int weapons = 0, armours = 0, amulets = 0;
        for (const GearTemplate& g : gear_table()) {
            if (g.set != set) continue;
            if (g.kind == ItemKind::Weapon) ++weapons;
            if (g.kind == ItemKind::Armor) ++armours;
            if (g.kind == ItemKind::Amulet) ++amulets;
        }
        const Text& name = gear_set_info(set).name;
        EXPECT_EQ(weapons, 1) << name.ru;
        EXPECT_EQ(armours, 1) << name.ru;
        EXPECT_EQ(amulets, 1) << name.ru;
    }
}

TEST(Gear, EverySetIsNamedAndDescribedInBothLanguages) {
    for (int s = 1; s < static_cast<int>(GearSet::Count); ++s) {
        const GearSetInfo& info = gear_set_info(static_cast<GearSet>(s));
        EXPECT_FALSE(info.name.ru.empty());
        EXPECT_FALSE(info.name.en.empty());
        EXPECT_FALSE(info.note.ru.empty()) << info.name.ru << " promises nothing";
        EXPECT_FALSE(info.note.en.empty()) << info.name.ru;
    }
}

TEST(Gear, EveryPieceOfASetIsWorthWearingOnItsOwn) {
    // A set that is worthless until finished is a trap rather than a choice, so
    // each piece has to carry a mechanic of its own.
    for (const GearTemplate& g : gear_table()) {
        if (g.set == GearSet::None) continue;
        EXPECT_NE(g.powers, GpNone) << g.key << " does nothing until the set is complete";
        EXPECT_FALSE(g.note.ru.empty()) << g.key;
    }
}

TEST(Gear, NoSetPieceIsSoEarlyThatItSkipsTheFirstBelt) {
    for (const GearTemplate& g : gear_table())
        if (g.set != GearSet::None)
            EXPECT_GE(g.min_depth, 2) << g.key << " can be found before the game has begun";
}

// ---------------------------------------------------------------------------
// What the pieces do
// ---------------------------------------------------------------------------

TEST(Gear, TheWardingShirtTurnsOneBlowAsideAndThenNeedsAFloor) {
    Wardrobe w;
    w.wear("sorochka");
    ASSERT_TRUE(w.game.hero_has(GpWard));

    Hero& h = w.game.mutable_hero();
    h.a.max_hp = h.a.hp = 200;

    // First blow: nothing gets through.
    w.game.damage_hero(30, Text{"проверка", "the test"});
    EXPECT_EQ(h.a.hp, 200) << "the shirt did not turn the first blow aside";

    // Second: it is spent.
    w.game.damage_hero(30, Text{"проверка", "the test"});
    EXPECT_LT(w.game.hero().a.hp, 200) << "the shirt stopped a second blow on the same floor";
}

TEST(Gear, TheBoarSpearHitsGuardiansHarderThanItHitsAnythingElse) {
    // Averaged, because damage is a roll: a single swing proves nothing about a
    // fifty-percent bonus.
    auto average = [](const char* weapon, const char* target) {
        long long total = 0;
        const int runs = 60;
        for (int i = 0; i < runs; ++i) {
            Wardrobe w(static_cast<std::uint64_t>(1000 + i));
            w.wear(weapon);
            Monster& m = w.spawn(target, {21, 15}, 100000);
            const int before = m.a.hp;
            w.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
            const Monster* after = nullptr;
            for (const auto& x : w.game.monsters())
                if (x.a.pos == Vec2{21, 15}) after = &x;
            if (after) total += before - after->a.hp;
        }
        return static_cast<double>(total) / runs;
    };

    const double spear_on_boss = average("rogatina", "viy");
    const double sword_on_boss = average("mech", "viy");
    // The spear is a stronger weapon to begin with, so the claim under test is
    // the *ratio* against a guardian versus against an ordinary monster.
    const double spear_on_beast = average("rogatina", "upyr");
    const double sword_on_beast = average("mech", "upyr");

    ASSERT_GT(sword_on_boss, 0.0);
    ASSERT_GT(sword_on_beast, 0.0);
    EXPECT_GT(spear_on_boss / sword_on_boss, spear_on_beast / sword_on_beast * 1.2)
        << "the boar spear is no better against the big ones than against anything else";
}

TEST(Gear, TheScaleCuirassSendsPartOfTheBlowBack) {
    Wardrobe w;
    w.wear("bahterets");
    ASSERT_TRUE(w.game.hero_has(GpThorns));

    Monster& m = w.spawn("upyr", {21, 15}, 500);
    const int before = m.a.hp;
    w.game.mutable_hero().a.max_hp = w.game.mutable_hero().a.hp = 5000;

    for (int i = 0; i < 40; ++i) w.game.perform(Action{ActionType::Wait, {}, -1, {}});

    const Monster* after = nullptr;
    for (const auto& x : w.game.monsters())
        if (x.species == m.species) after = &x;
    ASSERT_NE(after, nullptr);
    EXPECT_LT(after->a.hp, before) << "the upyr hit the hero forty times and took nothing back";
}

TEST(Gear, TheTravellersKitMakesWadingFree) {
    Wardrobe dressed;
    dressed.wear("klyuka");
    dressed.wear("lapti");
    dressed.wear("svecha");
    ASSERT_EQ(dressed.game.hero_set(), GearSet::Hodovoy);

    Wardrobe plain;
    plain.wear("mech");

    // Wading costs energy, and energy is what the turn scheduler spends. So the
    // honest measure is not a number inside the hero but how much of the
    // dungeon's time eight steps through water take.
    auto turns_to_wade = [](Wardrobe& w) {
        Level& lvl = w.game.mutable_level();
        for (int i = 1; i <= 10; ++i) lvl.map.set(w.game.hero().a.pos + Vec2{i, 0}, Tile::Water);
        w.game.refresh_view();
        const int before = w.game.turn();
        for (int i = 0; i < 8; ++i)
            w.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
        return w.game.turn() - before;
    };
    EXPECT_LT(turns_to_wade(dressed), turns_to_wade(plain))
        << "the traveller's kit did not make the mire any cheaper to cross";
}

TEST(Gear, ThePactWithTheDeadPaysOutOnAKill) {
    Wardrobe w;
    w.wear("naviy_nozh");
    w.wear("savan");
    w.wear("zerkaltse");
    ASSERT_EQ(w.game.hero_set(), GearSet::Naviy);

    Hero& h = w.game.mutable_hero();
    h.a.max_hp = 200;
    h.a.hp = 100;
    h.max_mana = 20;
    h.mana = 0;
    h.a.attack = 500;                       // one blow, one corpse

    w.spawn("anchutka", {21, 15}, 1);
    w.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});

    EXPECT_GT(w.game.hero().a.hp, 100) << "the kill returned no health";
    EXPECT_GT(w.game.hero().mana, 0) << "the kill returned no power";
}

TEST(Gear, AnIncompleteSetGrantsNothingExtra) {
    Wardrobe w;
    w.wear("naviy_nozh");
    w.wear("savan");
    EXPECT_EQ(w.game.hero_set(), GearSet::None)
        << "two thirds of a set should not be a set";
}

TEST(Gear, MixingTwoSetsIsNoSetAtAll) {
    Wardrobe w;
    w.wear("naviy_nozh");
    w.wear("lapti");
    w.wear("zerkaltse");
    EXPECT_EQ(w.game.hero_set(), GearSet::None);
}

TEST(Gear, TheKnotCharmKeepsVenomAndFireOut) {
    Wardrobe w;
    w.wear("nauzy");
    EXPECT_TRUE(w.game.hero_has(GpNoPoison));
    EXPECT_TRUE(w.game.hero_has(GpNoBurn));

    // Two hundred bites from a venomous thing and not one takes hold.
    w.spawn("anchutka", {21, 15}, 100000);
    w.game.mutable_hero().a.max_hp = w.game.mutable_hero().a.hp = 500000;
    for (int i = 0; i < 200; ++i) w.game.perform(Action{ActionType::Wait, {}, -1, {}});
    EXPECT_FALSE(w.game.hero().a.has(Effect::Poison)) << "the knot charm let the venom in";
}

TEST(Gear, TheLittleMirrorMakesSpellsCheaper) {
    Wardrobe plain;
    Wardrobe mirrored;
    mirrored.wear("zerkaltse");
    ASSERT_TRUE(mirrored.game.hero_has(GpCheapSpell));

    for (Wardrobe* w : {&plain, &mirrored}) {
        Hero& h = w->game.mutable_hero();
        h.learn(Spell::Heal);
        h.max_mana = 40;
        h.mana = 40;
        h.a.hp = 1;
        h.a.max_hp = 200;
    }
    plain.game.perform(Action{ActionType::CastSpell, {}, static_cast<int>(Spell::Heal), {}});
    mirrored.game.perform(Action{ActionType::CastSpell, {}, static_cast<int>(Spell::Heal), {}});

    // Compared against each other rather than against a number: natural mana
    // recovery ticks during the turn, and a test that pins the absolute value
    // would be measuring the upkeep instead of the mirror.
    EXPECT_LT(plain.game.hero().mana, 40) << "the plain hero cast nothing at all";
    EXPECT_GT(mirrored.game.hero().mana, plain.game.hero().mana)
        << "the mirror charged full price";
}

TEST(Gear, DroppingAWornCharmOfLifeFoldsItsBonusBackOut) {
    // NAV-019. Putting the Charm of Life on recomputes the derived maxima, and
    // so does taking it off from the pack — but dropping it on the floor used
    // to skip that step, leaving the hero carrying twelve phantom points of
    // maximum health (and a staff or robe, phantom mana) for the rest of the run.
    Wardrobe w;
    const int base_max_hp = w.game.hero().a.max_hp;

    const int ci = gear_index("ob_zhizni");
    ASSERT_GE(ci, 0);
    const GearTemplate& g = gear_table()[static_cast<std::size_t>(ci)];
    Item charm{};
    charm.kind = g.kind;
    charm.subtype = ci;
    charm.power = g.power;
    charm.identified = true;
    ASSERT_TRUE(w.game.mutable_hero().inv.add(charm));
    const int idx = static_cast<int>(w.game.hero().inv.items.size()) - 1;

    ASSERT_TRUE(w.game.perform(Action{ActionType::EquipItem, {}, idx, {}}));
    EXPECT_EQ(w.game.hero().a.max_hp, base_max_hp + 12) << "the charm did not raise max health";

    ASSERT_TRUE(w.game.perform(Action{ActionType::DropItem, {}, idx, {}}));
    EXPECT_EQ(w.game.hero().inv.amulet, -1) << "the slot should be empty after the drop";
    EXPECT_EQ(w.game.hero().a.max_hp, base_max_hp) << "max health kept the dropped charm's bonus";
    EXPECT_LE(w.game.hero().a.hp, w.game.hero().a.max_hp)
        << "current health left standing above the new maximum";
}
