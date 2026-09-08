// SPDX-License-Identifier: MIT
//
// Six heroes. Three are stat spreads; three carry a trait that changes how the
// game is played. A trait that cannot be measured is a line in a menu, so each
// one gets a test that compares it against a hero without it.
#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "nav/game.hpp"

using namespace nav;

namespace {

class Bench {
public:
    explicit Bench(HeroClass cls, std::uint64_t seed = 606) {
        GameConfig cfg;
        cfg.seed = seed;
        cfg.hero_class = cls;
        game.start(cfg);

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
    }

    Item gear(const char* key) {
        const auto& table = gear_table();
        for (std::size_t i = 0; i < table.size(); ++i) {
            if (std::strcmp(table[i].key, key) != 0) continue;
            Item it{};
            it.kind = table[i].kind;
            it.subtype = static_cast<int>(i);
            it.power = table[i].power;
            it.identified = true;
            return it;
        }
        ADD_FAILURE() << "unknown gear key: " << key;
        return Item{};
    }

    /// Strips the starting kit and equips exactly one weapon and one armour, so
    /// two classes can be compared on identical gear.
    void equip_only(const char* weapon, const char* armor) {
        game.mutable_hero().inv.items.clear();
        game.mutable_hero().inv.weapon = -1;
        game.mutable_hero().inv.armor = -1;
        game.mutable_hero().inv.amulet = -1;
        game.mutable_hero().inv.add(gear(weapon));
        game.perform(Action{ActionType::EquipItem, {}, 0, {}});
        game.mutable_hero().inv.add(gear(armor));
        game.perform(Action{ActionType::EquipItem, {}, 1, {}});
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

    int use_potion(PotionKind kind) {
        Item it{};
        it.kind = ItemKind::Potion;
        it.subtype = static_cast<int>(kind);
        it.count = 1;
        EXPECT_TRUE(game.mutable_hero().inv.add(it));
        const int before = game.hero().a.hp;
        game.perform(Action{ActionType::UseItem, {},
                            static_cast<int>(game.hero().inv.items.size()) - 1, {}});
        return game.hero().a.hp - before;
    }

    Game game;
};

}  // namespace

// --- The roster ------------------------------------------------------------

TEST(Classes, ThereAreSixAndEachIsDistinct) {
    EXPECT_EQ(class_table().size(), static_cast<std::size_t>(HeroClass::Count));

    std::set<std::string> names, keys;
    for (const auto& c : class_table()) {
        names.insert(c.name.ru);
        keys.insert(hero_class_key(c.cls));
        EXPECT_FALSE(c.blurb.ru.empty()) << c.name.ru << " has no description";
        EXPECT_FALSE(c.blurb.en.empty()) << c.name.en;
    }
    EXPECT_EQ(names.size(), class_table().size()) << "two classes share a name";
    EXPECT_EQ(keys.size(), class_table().size()) << "two classes share a key";
}

TEST(Classes, EveryOneIsPlayableFromTheFirstTurn) {
    for (const auto& c : class_table()) {
        GameConfig cfg;
        cfg.seed = 4040;
        cfg.hero_class = c.cls;
        Game g;
        g.start(cfg);

        EXPECT_EQ(g.hero().cls, c.cls) << c.name.en;
        EXPECT_GT(g.hero().a.max_hp, 0) << c.name.en;
        EXPECT_GT(g.hero_attack(), 0) << c.name.en;
        EXPECT_GE(g.hero().inv.weapon, 0) << c.name.en << " started unarmed";
        EXPECT_GE(g.hero().inv.armor, 0) << c.name.en << " started unarmoured";
        EXPECT_EQ(g.state(), RunState::Playing) << c.name.en;
        EXPECT_TRUE(g.perform(Action{ActionType::Wait, {}, -1, {}})) << c.name.en;
    }
}

TEST(Classes, TraitsAreAssignedToExactlyTheClassesThatAdvertiseThem) {
    EXPECT_TRUE(class_has(HeroClass::Znahar, TraitHerbalist));
    EXPECT_TRUE(class_has(HeroClass::Kuznets, TraitSmith));
    EXPECT_TRUE(class_has(HeroClass::Bogatyr, TraitCleave));

    for (HeroClass cls : {HeroClass::Vityaz, HeroClass::Vedun, HeroClass::Tat}) {
        EXPECT_FALSE(class_has(cls, TraitHerbalist));
        EXPECT_FALSE(class_has(cls, TraitSmith));
        EXPECT_FALSE(class_has(cls, TraitCleave));
    }
    EXPECT_FALSE(class_has(HeroClass::Znahar, TraitCleave)) << "traits are leaking between classes";
}

// --- Знахарь: the herbalist -------------------------------------------------

TEST(Znahar, KnowsEveryPotionFromTheStart) {
    Bench herbalist(HeroClass::Znahar);
    for (int kind = 0; kind < static_cast<int>(PotionKind::Count); ++kind)
        EXPECT_TRUE(herbalist.game.identification().knows(ItemKind::Potion, kind))
            << "potion " << kind << " is a mystery to the herbalist";

    // And nobody else gets that for free.
    Bench warrior(HeroClass::Vityaz);
    int known = 0;
    for (int kind = 0; kind < static_cast<int>(PotionKind::Count); ++kind)
        if (warrior.game.identification().knows(ItemKind::Potion, kind)) ++known;
    EXPECT_EQ(known, 0) << "the warrior started out knowing potions too";
}

TEST(Znahar, StillHasToDiscoverScrolls) {
    Bench herbalist(HeroClass::Znahar);
    int known = 0;
    for (int kind = 0; kind < static_cast<int>(ScrollKind::Count); ++kind)
        if (herbalist.game.identification().knows(ItemKind::Scroll, kind)) ++known;
    EXPECT_EQ(known, 0) << "herbalism is about draughts, not writing";
}

TEST(Znahar, DrawsMoreOutOfTheSamePotion) {
    Bench herbalist(HeroClass::Znahar);
    Bench warrior(HeroClass::Vityaz);
    herbalist.game.mutable_hero().a.max_hp = 500;
    warrior.game.mutable_hero().a.max_hp = 500;
    herbalist.game.mutable_hero().a.hp = 1;
    warrior.game.mutable_hero().a.hp = 1;

    const int healed_by_herbalist = herbalist.use_potion(PotionKind::Heal);
    const int healed_by_warrior = warrior.use_potion(PotionKind::Heal);
    EXPECT_GT(healed_by_herbalist, healed_by_warrior)
        << "the herbalist's draught did no more than anyone else's";
}

TEST(Znahar, ShakesOffABadDraughtFaster) {
    Bench herbalist(HeroClass::Znahar);
    Bench warrior(HeroClass::Vityaz);
    herbalist.use_potion(PotionKind::Confusion);
    warrior.use_potion(PotionKind::Confusion);

    int herbalist_turns = 0, warrior_turns = 0;
    for (const auto& e : herbalist.game.hero().a.effects)
        if (e.kind == Effect::Confusion) herbalist_turns = e.turns;
    for (const auto& e : warrior.game.hero().a.effects)
        if (e.kind == Effect::Confusion) warrior_turns = e.turns;

    EXPECT_GT(warrior_turns, 0);
    EXPECT_LT(herbalist_turns, warrior_turns) << "the herbalist is as helpless as anyone";
}

// --- Кузнец: the smith ------------------------------------------------------

TEST(Kuznets, GetsAnExtraGradeOutOfTheSameGear) {
    Bench smith(HeroClass::Kuznets);
    Bench warrior(HeroClass::Vityaz);
    smith.equip_only("mech", "kolchuga");
    warrior.equip_only("mech", "kolchuga");

    // Base stats differ between the classes, so compare the gear's contribution
    // rather than the totals.
    const int smith_gear = smith.game.hero_attack() - smith.game.hero().a.attack;
    const int warrior_gear = warrior.game.hero_attack() - warrior.game.hero().a.attack;
    EXPECT_EQ(smith_gear, warrior_gear + 1) << "the smith's sword is no better than anyone's";

    const int smith_armour = smith.game.hero_defence() - smith.game.hero().a.defence;
    const int warrior_armour = warrior.game.hero_defence() - warrior.game.hero().a.defence;
    EXPECT_EQ(smith_armour, warrior_armour + 1);
}

TEST(Kuznets, GainsNothingFromEmptyHands) {
    Bench smith(HeroClass::Kuznets);
    smith.game.mutable_hero().inv.items.clear();
    smith.game.mutable_hero().inv.weapon = -1;
    smith.game.mutable_hero().inv.armor = -1;
    smith.game.mutable_hero().inv.amulet = -1;
    EXPECT_EQ(smith.game.hero_attack(), smith.game.hero().a.attack)
        << "the smith bonus applies to gear, not to bare hands";
}

TEST(Kuznets, PaysHalfPriceAtAShrine) {
    auto price_paid = [](HeroClass cls) {
        Bench b(cls);
        b.game.mutable_level().map.set(b.game.hero().a.pos, Tile::Altar);
        b.game.mutable_hero().gold = 10000;
        const int before = b.game.hero().gold;
        EXPECT_TRUE(b.game.perform(Action{ActionType::Pray, {}, -1, {}})) << "the offering failed";
        return before - b.game.hero().gold;
    };

    const int smith = price_paid(HeroClass::Kuznets);
    const int warrior = price_paid(HeroClass::Vityaz);
    EXPECT_GT(warrior, 0);
    EXPECT_EQ(smith, warrior / 2) << "the shrine charged the smith full price";
}

// --- Богатырь: the champion -------------------------------------------------

TEST(Bogatyr, SweepsEveryAdjacentEnemyWithOneBlow) {
    Bench champion(HeroClass::Bogatyr);
    const Vec2 me = champion.game.hero().a.pos;
    champion.spawn("upyr", me + Vec2{1, 0}, 500);   // the target
    champion.spawn("upyr", me + Vec2{-1, 0}, 500);  // caught by the sweep
    champion.spawn("upyr", me + Vec2{0, -1}, 500);

    std::vector<int> before;
    for (const auto& m : champion.game.monsters()) before.push_back(m.a.hp);

    ASSERT_TRUE(champion.game.perform(Action{ActionType::Move, {1, 0}, -1, {}}));

    ASSERT_EQ(champion.game.monsters().size(), before.size());
    for (std::size_t i = 0; i < before.size(); ++i)
        EXPECT_LT(champion.game.monsters()[i].a.hp, before[i])
            << "monster " << i << " was standing next to the swing and took nothing";
}

TEST(Bogatyr, TheSweepHitsForLessThanTheBlowItself) {
    Bench champion(HeroClass::Bogatyr);
    const Vec2 me = champion.game.hero().a.pos;
    Monster& target = champion.spawn("upyr", me + Vec2{1, 0}, 5000);
    Monster& bystander = champion.spawn("upyr", me + Vec2{-1, 0}, 5000);
    const int target_before = target.a.hp, bystander_before = bystander.a.hp;

    champion.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});

    const int target_lost = target_before - champion.game.monsters()[0].a.hp;
    const int bystander_lost = bystander_before - champion.game.monsters()[1].a.hp;
    EXPECT_GT(target_lost, 0);
    EXPECT_GT(bystander_lost, 0);
    EXPECT_LT(bystander_lost, target_lost) << "the sweep should be the weaker half of the swing";
}

TEST(Bogatyr, OtherClassesHitOnlyWhatTheyAimAt) {
    Bench warrior(HeroClass::Vityaz);
    const Vec2 me = warrior.game.hero().a.pos;
    warrior.spawn("upyr", me + Vec2{1, 0}, 500);
    Monster& bystander = warrior.spawn("upyr", me + Vec2{-1, 0}, 500);
    const int bystander_before = bystander.a.hp;

    warrior.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});

    // The bystander may have moved or attacked, but it must not have been cut.
    EXPECT_EQ(warrior.game.monsters()[1].a.hp, bystander_before)
        << "a class without the trait swept anyway";
}

TEST(Bogatyr, IsSlowerThanTheOthersToPayForIt) {
    EXPECT_LT(class_info(HeroClass::Bogatyr).speed, class_info(HeroClass::Vityaz).speed);
    EXPECT_GT(class_info(HeroClass::Bogatyr).hp, class_info(HeroClass::Vityaz).hp);
}

// --- All six, played --------------------------------------------------------

TEST(Classes, EveryOneSurvivesALongRandomisedRun) {
    for (const auto& c : class_table()) {
        GameConfig cfg;
        cfg.seed = 2718;
        cfg.hero_class = c.cls;
        Game g;
        g.start(cfg);

        Rng policy(99);
        for (int i = 0; i < 900 && g.state() == RunState::Playing; ++i) {
            const int roll = policy.below(100);
            if (roll < 70) {
                g.perform(Action{ActionType::Move,
                                 directions8()[static_cast<std::size_t>(policy.below(8))], -1, {}});
            } else if (roll < 80 && !g.hero().inv.items.empty()) {
                g.perform(Action{ActionType::UseItem, {},
                                 policy.below(static_cast<int>(g.hero().inv.items.size())), {}});
            } else if (roll < 88) {
                g.perform(Action{ActionType::PickUp, {}, -1, {}});
            } else if (roll < 95) {
                g.perform(Action{ActionType::Descend, {}, -1, {}});
            } else {
                g.perform(Action{ActionType::Wait, {}, -1, {}});
            }
        }

        // Whatever happened, the run must have ended in a legal state.
        EXPECT_GE(g.hero().a.hp, 0) << c.name.en;
        EXPECT_LE(g.hero().a.hp, g.hero().a.max_hp) << c.name.en;
        EXPECT_GE(g.depth(), 1) << c.name.en;
        EXPECT_LE(g.depth(), kMaxDepth) << c.name.en;

        Game restored;
        EXPECT_TRUE(restored.load(g.save())) << c.name.en << ": the save did not round-trip";
    }
}
