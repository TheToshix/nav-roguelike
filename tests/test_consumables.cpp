// SPDX-License-Identifier: MIT
//
// Every potion and every scroll, exercised once. Consumables are where a
// content table and the rules code meet, and an entry that is never drunk in a
// test is an entry that can be quietly broken by a refactor.
#include <gtest/gtest.h>

#include <cstring>

#include "nav/game.hpp"

#include "support.hpp"

using namespace nav;

namespace {

class Lab {
public:
    Lab(HeroClass cls = HeroClass::Vityaz, std::uint64_t seed = 777) {
        GameConfig cfg;
        cfg.seed = seed;
        cfg.hero_class = cls;
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
        game.mutable_hero().inv.items.clear();
        game.mutable_hero().inv.weapon = -1;
        game.mutable_hero().inv.armor = -1;
        game.mutable_hero().inv.amulet = -1;
    }

    /// Adds one consumable and uses it. Returns false if the action was refused.
    bool consume(ItemKind kind, int subtype) {
        Item it{};
        it.kind = kind;
        it.subtype = subtype;
        it.count = 1;
        EXPECT_TRUE(game.mutable_hero().inv.add(it));
        const int index = static_cast<int>(game.hero().inv.items.size()) - 1;
        return game.perform(Action{ActionType::UseItem, {}, index, {}});
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
        // Area effects only touch what the hero can see, so the view has to be
        // in step with a monster that was placed directly.
        game.refresh_view();
        return game.mutable_level().monsters.back();
    }

    Game game;
};

}  // namespace

// --- Potions ---------------------------------------------------------------

TEST(Potions, EveryKindCanBeDrunkAndIdentifiesItself) {
    for (int kind = 0; kind < static_cast<int>(PotionKind::Count); ++kind) {
        Lab lab;
        lab.game.mutable_hero().a.hp = lab.game.hero().a.max_hp / 2;
        EXPECT_TRUE(lab.consume(ItemKind::Potion, kind)) << "potion " << kind << " was refused";
        EXPECT_TRUE(lab.game.identification().knows(ItemKind::Potion, kind))
            << "potion " << kind << " stayed unidentified after being drunk";
        EXPECT_TRUE(lab.game.hero().inv.items.empty()) << "potion " << kind << " was not consumed";
    }
}

TEST(Potions, HealingRestoresHealth) {
    Lab lab;
    lab.game.mutable_hero().a.hp = 1;
    lab.consume(ItemKind::Potion, static_cast<int>(PotionKind::Heal));
    EXPECT_GT(lab.game.hero().a.hp, 1);
    EXPECT_LE(lab.game.hero().a.hp, lab.game.hero().a.max_hp);
}

TEST(Potions, GreaterHealingAlsoClearsPoisonAndBurning) {
    Lab lab;
    lab.game.mutable_hero().a.hp = 1;
    lab.game.mutable_hero().a.add_effect(Effect::Poison, 20, 3);
    lab.game.mutable_hero().a.add_effect(Effect::Burn, 20, 3);

    lab.consume(ItemKind::Potion, static_cast<int>(PotionKind::GreaterHeal));
    EXPECT_FALSE(lab.game.hero().a.has(Effect::Poison));
    EXPECT_FALSE(lab.game.hero().a.has(Effect::Burn));
    EXPECT_GT(lab.game.hero().a.hp, 1);
}

TEST(Potions, ManaRefillsTheReserveOfPower) {
    Lab lab(HeroClass::Vedun);
    lab.game.mutable_hero().mana = 0;
    lab.consume(ItemKind::Potion, static_cast<int>(PotionKind::Mana));
    EXPECT_EQ(lab.game.hero().mana, lab.game.hero().max_mana);
}

TEST(Potions, MightHasteAndRegenApplyTheirEffects) {
    struct Case { PotionKind kind; Effect effect; };
    const Case cases[] = {
        {PotionKind::Might, Effect::Might},
        {PotionKind::Haste, Effect::Haste},
        {PotionKind::Regen, Effect::Regen},
    };
    for (const auto& c : cases) {
        Lab lab;
        lab.consume(ItemKind::Potion, static_cast<int>(c.kind));
        EXPECT_TRUE(lab.game.hero().a.has(c.effect))
            << "potion " << static_cast<int>(c.kind) << " applied no effect";
    }
}

TEST(Potions, VenomPoisonsTheDrinker) {
    Lab lab;
    lab.consume(ItemKind::Potion, static_cast<int>(PotionKind::Poison));
    EXPECT_TRUE(lab.game.hero().a.has(Effect::Poison));
}

TEST(Potions, TheAntivenomCharmBlocksTheVenomPotion) {
    Lab lab;
    const auto& gear = gear_table();
    for (std::size_t i = 0; i < gear.size(); ++i) {
        if (std::strcmp(gear[i].key, "ob_yada") != 0) continue;
        Item charm{};
        charm.kind = gear[i].kind;
        charm.subtype = static_cast<int>(i);
        charm.power = gear[i].power;
        charm.identified = true;
        lab.game.mutable_hero().inv.add(charm);
        lab.game.perform(Action{ActionType::EquipItem, {},
                                static_cast<int>(lab.game.hero().inv.items.size()) - 1, {}});
        break;
    }
    ASSERT_GE(lab.game.hero().inv.amulet, 0);

    lab.consume(ItemKind::Potion, static_cast<int>(PotionKind::Poison));
    EXPECT_FALSE(lab.game.hero().a.has(Effect::Poison)) << "the charm did not stop the venom";
}

TEST(Potions, BewildermentConfusesTheDrinker) {
    Lab lab;
    lab.consume(ItemKind::Potion, static_cast<int>(PotionKind::Confusion));
    EXPECT_TRUE(lab.game.hero().a.has(Effect::Confusion));
}

// --- Scrolls ---------------------------------------------------------------

TEST(Scrolls, EveryKindCanBeReadAndIdentifiesItself) {
    for (int kind = 0; kind < static_cast<int>(ScrollKind::Count); ++kind) {
        Lab lab;
        lab.spawn("upyr", {22, 15}, 300);  // give the offensive scrolls a target
        EXPECT_TRUE(lab.consume(ItemKind::Scroll, kind)) << "scroll " << kind << " was refused";
        EXPECT_TRUE(lab.game.identification().knows(ItemKind::Scroll, kind))
            << "scroll " << kind << " stayed unidentified after being read";
    }
}

TEST(Scrolls, FireballAndLightningDamageNearbyCreatures) {
    const ScrollKind kinds[] = {ScrollKind::Fireball, ScrollKind::Lightning};
    for (ScrollKind kind : kinds) {
        Lab lab;
        Monster& m = lab.spawn("upyr", {22, 15}, 400);
        const int before = m.a.hp;
        lab.consume(ItemKind::Scroll, static_cast<int>(kind));
        ASSERT_FALSE(lab.game.monsters().empty());
        EXPECT_LT(lab.game.monsters()[0].a.hp, before)
            << "scroll " << static_cast<int>(kind) << " dealt no damage";
    }
}

TEST(Scrolls, FrostFreezesAndBlindingBlindsWhatIsNearby) {
    struct Case { ScrollKind kind; Effect effect; };
    const Case cases[] = {
        {ScrollKind::Frost, Effect::Freeze},
        {ScrollKind::Blind, Effect::Blind},
    };
    for (const auto& c : cases) {
        Lab lab;
        lab.spawn("upyr", {22, 15}, 400);
        lab.consume(ItemKind::Scroll, static_cast<int>(c.kind));
        ASSERT_FALSE(lab.game.monsters().empty());
        EXPECT_TRUE(lab.game.monsters()[0].a.has(c.effect))
            << "scroll " << static_cast<int>(c.kind) << " applied no effect";
    }
}

TEST(Scrolls, TeleportationMovesTheHeroSomewhereElse) {
    Lab lab;
    const Vec2 before = lab.game.hero().a.pos;
    lab.consume(ItemKind::Scroll, static_cast<int>(ScrollKind::Teleport));
    EXPECT_NE(lab.game.hero().a.pos, before);
    EXPECT_TRUE(lab.game.map().walkable(lab.game.hero().a.pos))
        << "teleportation dropped the hero inside a wall";
}

TEST(Scrolls, SummoningAddsMonsters) {
    Lab lab;
    const std::size_t before = lab.game.monsters().size();
    lab.consume(ItemKind::Scroll, static_cast<int>(ScrollKind::Summon));
    EXPECT_GT(lab.game.monsters().size(), before) << "the summoning scroll called nobody";
}

// --- Food ------------------------------------------------------------------

TEST(Food, EatingPushesBackHunger) {
    Lab lab;
    lab.game.mutable_hero().nutrition = 10;
    Item bread{};
    bread.kind = ItemKind::Food;
    bread.count = 1;
    bread.identified = true;
    lab.game.mutable_hero().inv.add(bread);

    EXPECT_TRUE(lab.game.perform(Action{ActionType::UseItem, {}, 0, {}}));
    EXPECT_GT(lab.game.hero().nutrition, 100);
    EXPECT_TRUE(lab.game.hero().inv.items.empty());
}

TEST(Food, NutritionIsCapped) {
    Lab lab;
    lab.game.mutable_hero().nutrition = 1500;
    Item bread{};
    bread.kind = ItemKind::Food;
    bread.count = 3;
    lab.game.mutable_hero().inv.add(bread);
    for (int i = 0; i < 3 && !lab.game.hero().inv.items.empty(); ++i)
        lab.game.perform(Action{ActionType::UseItem, {}, 0, {}});
    EXPECT_LE(lab.game.hero().nutrition, 1600) << "eating past full has no limit";
}

// --- Gear ------------------------------------------------------------------

TEST(Gear, EveryTemplateCanBeEquippedAndRemoved) {
    const auto& gear = gear_table();
    for (std::size_t i = 0; i < gear.size(); ++i) {
        Lab lab;
        Item it{};
        it.kind = gear[i].kind;
        it.subtype = static_cast<int>(i);
        it.power = gear[i].power;
        it.identified = true;
        ASSERT_TRUE(lab.game.mutable_hero().inv.add(it));

        EXPECT_TRUE(lab.game.perform(Action{ActionType::EquipItem, {}, 0, {}}))
            << gear[i].key << " could not be equipped";
        EXPECT_TRUE(lab.game.hero().inv.is_equipped(0)) << gear[i].key;

        EXPECT_TRUE(lab.game.perform(Action{ActionType::EquipItem, {}, 0, {}}))
            << gear[i].key << " could not be taken off";
        EXPECT_FALSE(lab.game.hero().inv.is_equipped(0)) << gear[i].key;

        // Whatever the item did to the hero's numbers, nothing may end up broken.
        EXPECT_GT(lab.game.hero().a.max_hp, 0) << gear[i].key;
        EXPECT_LE(lab.game.hero().a.hp, lab.game.hero().a.max_hp) << gear[i].key;
        EXPECT_GE(lab.game.hero().mana, 0) << gear[i].key;
        EXPECT_LE(lab.game.hero().mana, lab.game.hero().max_mana) << gear[i].key;
    }
}

TEST(Gear, TheStaffAndRobeBothWidenTheReserveOfPower) {
    const char* keys[] = {"posokh", "mantiya"};
    for (const char* key : keys) {
        Lab lab(HeroClass::Vedun);
        const int before = lab.game.hero().max_mana;

        const auto& gear = gear_table();
        for (std::size_t i = 0; i < gear.size(); ++i) {
            if (std::strcmp(gear[i].key, key) != 0) continue;
            Item it{};
            it.kind = gear[i].kind;
            it.subtype = static_cast<int>(i);
            it.power = gear[i].power;
            it.identified = true;
            lab.game.mutable_hero().inv.add(it);
            break;
        }
        ASSERT_FALSE(lab.game.hero().inv.items.empty()) << key;
        lab.game.perform(Action{ActionType::EquipItem, {}, 0, {}});
        EXPECT_GT(lab.game.hero().max_mana, before) << key << " did not widen the reserve";
    }
}

TEST(Gear, DroppingSomethingWornTakesItOffFirst) {
    Lab lab;
    const auto& gear = gear_table();
    Item sword{};
    sword.kind = gear[1].kind;
    sword.subtype = 1;
    sword.power = gear[1].power;
    sword.identified = true;
    lab.game.mutable_hero().inv.add(sword);
    lab.game.perform(Action{ActionType::EquipItem, {}, 0, {}});
    ASSERT_EQ(lab.game.hero().inv.weapon, 0);

    ASSERT_TRUE(lab.game.perform(Action{ActionType::DropItem, {}, 0, {}}));
    EXPECT_EQ(lab.game.hero().inv.weapon, -1) << "the weapon slot still points at a dropped item";
    EXPECT_EQ(lab.game.floor_items().size(), 1u);
}
