// SPDX-License-Identifier: MIT
//
// Rules tests driven through the public Game interface — the same calls the
// terminal and the browser make. Scenarios are set up by clearing the
// generated level and placing exactly what the test needs.
#include <gtest/gtest.h>

#include <cstring>

#include "nav/game.hpp"

#include "support.hpp"

using namespace nav;

namespace {

/// A game on a blank, monster-free floor with the hero in a known spot.
class Arena {
public:
    explicit Arena(HeroClass cls = HeroClass::Vityaz, std::uint64_t seed = 2026) {
        GameConfig cfg;
        cfg.seed = seed;
        cfg.hero_class = cls;
        game.start(cfg);
        leave_crossroads(game);
        clear_floor();
    }

    /// Removes every monster and item, and carves a wide open room around the
    /// hero so movement tests are not tripped up by generated walls. Staircases
    /// are left in place — several tests need to travel between floors.
    void clear_floor() {
        Level& lvl = game.mutable_level();
        lvl.monsters.clear();
        lvl.items.clear();
        for (int y = 1; y < lvl.map.height() - 1; ++y)
            for (int x = 1; x < lvl.map.width() - 1; ++x) {
                const Tile t = lvl.map.at({x, y});
                if (t == Tile::StairsUp || t == Tile::StairsDown) continue;
                lvl.map.set({x, y}, Tile::Floor);
            }
        game.mutable_hero().a.pos = {10, 10};
        if (lvl.map.at({10, 10}) != Tile::StairsDown && lvl.map.at({10, 10}) != Tile::StairsUp)
            lvl.map.set({10, 10}, Tile::Floor);
    }

    /// Puts the first monster back beside the hero. Combat tests care about the
    /// damage model, not about chasing a monster that walked away between
    /// swings.
    void place_beside_hero() {
        ASSERT_FALSE(game.mutable_level().monsters.empty());
        game.mutable_level().monsters[0].a.pos = game.hero().a.pos + Vec2{1, 0};
    }

    /// Places a monster of the named species at `pos` and returns its index.
    std::size_t spawn(const char* species_key, Vec2 pos, int hp = -1) {
        const int index = species_index(species_key);
        EXPECT_GE(index, 0) << species_key;
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
        return game.mutable_level().monsters.size() - 1;
    }

    Item make_gear(const char* key) {
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

    bool move(Vec2 dir) { return game.perform(Action{ActionType::Move, dir, -1, {}}); }
    bool wait_turn() { return game.perform(Action{ActionType::Wait, {}, -1, {}}); }

    Game game;
};

}  // namespace

// --- Movement --------------------------------------------------------------

TEST(Movement, WalkingIntoOpenFloorMovesTheHero) {
    Arena a;
    const Vec2 before = a.game.hero().a.pos;
    EXPECT_TRUE(a.move({1, 0}));
    EXPECT_EQ(a.game.hero().a.pos, (before + Vec2{1, 0}));
}

TEST(Movement, BumpingAWallCostsNoTurn) {
    // Free movement into walls would let a player scout the layout for nothing;
    // worse, it made hunger and poison advance while standing still.
    Arena a;
    a.game.mutable_level().map.set({11, 10}, Tile::Wall);
    const int turn_before = a.game.turn();
    const Vec2 pos_before = a.game.hero().a.pos;

    EXPECT_FALSE(a.move({1, 0}));
    EXPECT_EQ(a.game.hero().a.pos, pos_before);
    EXPECT_EQ(a.game.turn(), turn_before) << "a refused move must not advance the clock";
}

TEST(Movement, WalkingIntoAClosedDoorOpensIt) {
    Arena a;
    a.game.mutable_level().map.set({11, 10}, Tile::Door);
    const Vec2 pos_before = a.game.hero().a.pos;

    EXPECT_TRUE(a.move({1, 0}));
    EXPECT_EQ(a.game.map().at({11, 10}), Tile::OpenDoor);
    EXPECT_EQ(a.game.hero().a.pos, pos_before) << "opening a door takes the turn, not the step";

    EXPECT_TRUE(a.move({1, 0}));
    EXPECT_EQ(a.game.hero().a.pos, (Vec2{11, 10}));
}

TEST(Movement, ChasmsCannotBeEntered) {
    Arena a;
    a.game.mutable_level().map.set({11, 10}, Tile::Chasm);
    EXPECT_FALSE(a.move({1, 0}));
    EXPECT_EQ(a.game.hero().a.pos, (Vec2{10, 10}));
}

TEST(Movement, WadingThroughWaterCostsMoreThanWalking) {
    Arena dry;
    const int dry_turns_before = dry.game.turn();
    dry.move({1, 0});
    const int dry_cost = dry.game.turn() - dry_turns_before;

    Arena wet;
    wet.game.mutable_level().map.set({11, 10}, Tile::Water);
    const int wet_turns_before = wet.game.turn();
    wet.move({1, 0});
    const int wet_cost = wet.game.turn() - wet_turns_before;

    EXPECT_GT(wet_cost, dry_cost) << "water should slow the hero down";
}

TEST(Movement, AZeroDirectionIsRejected) {
    Arena a;
    EXPECT_FALSE(a.move({0, 0}));
}

// --- Melee -----------------------------------------------------------------

TEST(Combat, SteppingIntoAMonsterAttacksInsteadOfMoving) {
    Arena a;
    a.spawn("upyr", {11, 10}, 200);  // tough enough to survive the swing
    const Vec2 pos_before = a.game.hero().a.pos;
    const int hp_before = a.game.monsters()[0].a.hp;

    EXPECT_TRUE(a.move({1, 0}));
    EXPECT_EQ(a.game.hero().a.pos, pos_before) << "the hero must stay put and swing";
    ASSERT_FALSE(a.game.monsters().empty());
    EXPECT_LT(a.game.monsters()[0].a.hp, hp_before);
}

TEST(Combat, ArmourReducesDamageButNeverToZero) {
    // The stone idol has defence 8, far above a starting hero's attack. Even
    // then every hit must land for at least one point.
    Arena a;
    a.spawn("kamennaya", {11, 10}, 500);
    a.game.mutable_hero().a.max_hp = 100000;  // survive long enough to finish the sample
    a.game.mutable_hero().a.hp = 100000;

    for (int i = 0; i < 25; ++i) {
        a.place_beside_hero();
        if (::testing::Test::HasFatalFailure()) return;
        const int before = a.game.monsters()[0].a.hp;
        a.move({1, 0});
        ASSERT_FALSE(a.game.monsters().empty());
        EXPECT_LT(a.game.monsters()[0].a.hp, before) << "swing " << i << " dealt no damage at all";
    }
}

TEST(Combat, KillingAMonsterRemovesItAndAwardsExperience) {
    Arena a;
    a.spawn("anchutka", {11, 10}, 1);  // one hit is enough
    const int xp_before = a.game.hero().xp;
    const int kills_before = a.game.hero().kills;

    a.move({1, 0});

    EXPECT_TRUE(a.game.monsters().empty()) << "the corpse must be cleared away";
    EXPECT_GT(a.game.hero().xp, xp_before);
    EXPECT_EQ(a.game.hero().kills, kills_before + 1);
}

TEST(Combat, EnoughExperienceLevelsTheHeroUp) {
    Arena a;
    ASSERT_EQ(a.game.hero().level, 1);
    const int hp_before = a.game.hero().a.max_hp;

    // Feed the hero a stream of trivially killable monsters.
    for (int i = 0; i < 40 && a.game.hero().level < 3; ++i) {
        a.game.mutable_level().monsters.clear();
        a.spawn("likho", {11, 10}, 1);
        a.move({1, 0});
    }

    EXPECT_GE(a.game.hero().level, 2);
    EXPECT_GT(a.game.hero().a.max_hp, hp_before) << "levelling up must raise the health pool";
    EXPECT_GE(a.game.hero().xp, xp_for_level(a.game.hero().level));
}

TEST(Combat, TheHeroCanBeKilledAndTheRunEnds) {
    Arena a;
    a.game.mutable_hero().a.hp = 1;
    a.spawn("zmey", {11, 10}, 500);  // hits far harder than one point

    for (int i = 0; i < 40 && a.game.state() == RunState::Playing; ++i) a.wait_turn();

    EXPECT_EQ(a.game.state(), RunState::Dead);
    EXPECT_EQ(a.game.hero().a.hp, 0);
    EXPECT_FALSE(a.game.hero().a.alive);
}

TEST(Combat, NoActionIsAcceptedOnceTheRunIsOver) {
    Arena a;
    a.game.mutable_hero().a.hp = 1;
    a.spawn("zmey", {11, 10}, 500);
    for (int i = 0; i < 40 && a.game.state() == RunState::Playing; ++i) a.wait_turn();
    ASSERT_NE(a.game.state(), RunState::Playing);

    const int turn = a.game.turn();
    EXPECT_FALSE(a.wait_turn());
    EXPECT_FALSE(a.move({1, 0}));
    EXPECT_EQ(a.game.turn(), turn) << "the clock must stop when the run does";
}

TEST(Combat, KoscheiRisesAgainWhileTheNeedleIsWhole) {
    // His death is not in his body. Killing him without breaking the needle
    // first is supposed to fail — that is the whole fight.
    Arena a;
    a.spawn("koschei", {11, 10}, 1);
    a.place_beside_hero();
    a.move({1, 0});

    ASSERT_FALSE(a.game.monsters().empty()) << "Кощей stayed dead with the needle unbroken";
    EXPECT_GT(a.game.monsters()[0].a.hp, 0);
    EXPECT_EQ(a.game.monsters()[0].revives, 1);
    EXPECT_EQ(a.game.state(), RunState::Playing);
    EXPECT_TRUE(a.game.needle_intact());
}

TEST(Combat, BreakingTheNeedleMakesKoscheiMortal) {
    Arena a;
    Item needle{};
    needle.kind = ItemKind::Needle;
    needle.identified = true;
    ASSERT_TRUE(a.game.mutable_hero().inv.add(needle));
    const int index = static_cast<int>(a.game.hero().inv.items.size()) - 1;
    ASSERT_TRUE(a.game.perform(Action{ActionType::UseItem, {}, index, {}}));
    EXPECT_FALSE(a.game.needle_intact());

    a.spawn("koschei", {11, 10}, 1);
    a.place_beside_hero();
    a.move({1, 0});

    EXPECT_TRUE(a.game.monsters().empty()) << "the broken needle should let him stay dead";
    // The run does not end with him any more: four floors and one guardian
    // still wait below.
    EXPECT_EQ(a.game.state(), RunState::Playing);
}

// --- Derived statistics ----------------------------------------------------

TEST(Stats, EquippingAWeaponRaisesTheAttackRating) {
    Arena a;
    const int before = a.game.hero_attack();

    Item axe = a.make_gear("sekira");
    ASSERT_TRUE(a.game.mutable_hero().inv.add(axe));
    const int index = static_cast<int>(a.game.mutable_hero().inv.items.size()) - 1;
    ASSERT_TRUE(a.game.perform(Action{ActionType::EquipItem, {}, index, {}}));

    EXPECT_GT(a.game.hero_attack(), before);
}

TEST(Stats, EquippingArmourRaisesTheDefenceRating) {
    Arena a;
    const int before = a.game.hero_defence();
    Item plate = a.make_gear("zertsalo");
    a.game.mutable_hero().inv.add(plate);
    const int index = static_cast<int>(a.game.mutable_hero().inv.items.size()) - 1;
    a.game.perform(Action{ActionType::EquipItem, {}, index, {}});
    EXPECT_GT(a.game.hero_defence(), before);
}

TEST(Stats, TheLifeCharmRaisesTheMaximumHealth) {
    Arena a;
    const int before = a.game.hero().a.max_hp;
    Item charm = a.make_gear("ob_zhizni");
    a.game.mutable_hero().inv.add(charm);
    const int index = static_cast<int>(a.game.mutable_hero().inv.items.size()) - 1;
    a.game.perform(Action{ActionType::EquipItem, {}, index, {}});

    EXPECT_EQ(a.game.hero().a.max_hp, before + charm.power);
    EXPECT_LE(a.game.hero().a.hp, a.game.hero().a.max_hp);
}

TEST(Stats, RemovingTheLifeCharmDoesNotLeaveHealthAboveTheMaximum) {
    Arena a;
    Item charm = a.make_gear("ob_zhizni");
    a.game.mutable_hero().inv.add(charm);
    const int index = static_cast<int>(a.game.mutable_hero().inv.items.size()) - 1;
    a.game.perform(Action{ActionType::EquipItem, {}, index, {}});
    a.game.mutable_hero().a.hp = a.game.hero().a.max_hp;

    a.game.perform(Action{ActionType::EquipItem, {}, index, {}});  // take it off again
    EXPECT_LE(a.game.hero().a.hp, a.game.hero().a.max_hp);
}

TEST(Stats, TheSightCharmWidensTheFieldOfView) {
    Arena a;
    const int before = a.game.hero_sight();
    Item charm = a.make_gear("ob_zorko");
    a.game.mutable_hero().inv.add(charm);
    const int index = static_cast<int>(a.game.mutable_hero().inv.items.size()) - 1;
    a.game.perform(Action{ActionType::EquipItem, {}, index, {}});
    EXPECT_GT(a.game.hero_sight(), before);
}

TEST(Stats, BlindnessCollapsesTheFieldOfViewToOneCell) {
    Arena a;
    a.game.mutable_hero().a.add_effect(Effect::Blind, 5, 1);
    EXPECT_EQ(a.game.hero_sight(), 1);
}

TEST(Stats, MightRaisesTheAttackRatingWhileItLasts) {
    Arena a;
    const int before = a.game.hero_attack();
    a.game.mutable_hero().a.add_effect(Effect::Might, 5, 4);
    EXPECT_EQ(a.game.hero_attack(), before + 4);
}

// --- Items in play ---------------------------------------------------------

TEST(Items, GoldGoesStraightIntoThePurse) {
    Arena a;
    Item gold{};
    gold.kind = ItemKind::Gold;
    gold.count = 75;
    gold.pos = a.game.hero().a.pos;
    a.game.mutable_level().items.push_back(gold);

    const int purse_before = a.game.hero().gold;
    EXPECT_TRUE(a.game.perform(Action{ActionType::PickUp, {}, -1, {}}));
    EXPECT_EQ(a.game.hero().gold, purse_before + 75);
    EXPECT_TRUE(a.game.floor_items().empty());
}

TEST(Items, PickingUpWithNothingUnderfootIsRefused) {
    Arena a;
    EXPECT_FALSE(a.game.perform(Action{ActionType::PickUp, {}, -1, {}}));
}

TEST(Items, DroppingPutsTheItemBackOnTheFloor) {
    Arena a;
    const int index = 0;
    ASSERT_FALSE(a.game.hero().inv.items.empty());
    const std::size_t pack_before = a.game.hero().inv.items.size();

    EXPECT_TRUE(a.game.perform(Action{ActionType::DropItem, {}, index, {}}));
    EXPECT_LT(a.game.hero().inv.items.size(), pack_before);
    ASSERT_EQ(a.game.floor_items().size(), 1u);
    EXPECT_EQ(a.game.floor_items()[0].pos, a.game.hero().a.pos);
}

TEST(Items, ADroppedItemCanBePickedUpAgain) {
    Arena a;
    const std::size_t pack_before = a.game.hero().inv.items.size();
    a.game.perform(Action{ActionType::DropItem, {}, 0, {}});
    a.game.perform(Action{ActionType::PickUp, {}, -1, {}});
    EXPECT_EQ(a.game.hero().inv.items.size(), pack_before);
}

TEST(Items, AHealingPotionRestoresHealthAndIsConsumed) {
    Arena a;
    a.game.mutable_hero().a.hp = 5;

    int potion_index = -1;
    for (std::size_t i = 0; i < a.game.hero().inv.items.size(); ++i)
        if (a.game.hero().inv.items[i].kind == ItemKind::Potion) potion_index = static_cast<int>(i);
    ASSERT_GE(potion_index, 0) << "the starting kit should contain healing potions";
    const int count_before = a.game.hero().inv.items[static_cast<std::size_t>(potion_index)].count;

    EXPECT_TRUE(a.game.perform(Action{ActionType::UseItem, {}, potion_index, {}}));
    EXPECT_GT(a.game.hero().a.hp, 5);

    int count_after = 0;
    for (const auto& it : a.game.hero().inv.items)
        if (it.kind == ItemKind::Potion) count_after = it.count;
    EXPECT_EQ(count_after, count_before - 1);
}

TEST(Items, DrinkingAPotionIdentifiesThatKind) {
    Arena a;
    int potion_index = -1;
    for (std::size_t i = 0; i < a.game.hero().inv.items.size(); ++i)
        if (a.game.hero().inv.items[i].kind == ItemKind::Potion) potion_index = static_cast<int>(i);
    ASSERT_GE(potion_index, 0);
    const int subtype = a.game.hero().inv.items[static_cast<std::size_t>(potion_index)].subtype;
    ASSERT_FALSE(a.game.identification().knows(ItemKind::Potion, subtype));

    a.game.perform(Action{ActionType::UseItem, {}, potion_index, {}});
    EXPECT_TRUE(a.game.identification().knows(ItemKind::Potion, subtype));
}

TEST(Items, TheMagicMapScrollRevealsTheWholeFloor) {
    Arena a;
    Item scroll{};
    scroll.kind = ItemKind::Scroll;
    scroll.subtype = static_cast<int>(ScrollKind::MagicMap);
    ASSERT_TRUE(a.game.mutable_hero().inv.add(scroll));
    const int index = static_cast<int>(a.game.hero().inv.items.size()) - 1;

    a.game.perform(Action{ActionType::UseItem, {}, index, {}});

    const Vec2 far_corner{a.game.map().width() - 2, a.game.map().height() - 2};
    EXPECT_TRUE(a.game.map().explored(far_corner));
}

TEST(Items, TheIdentifyScrollNamesEverythingInThePack) {
    Arena a;
    Item mystery{};
    mystery.kind = ItemKind::Potion;
    mystery.subtype = static_cast<int>(PotionKind::Regen);
    a.game.mutable_hero().inv.add(mystery);

    Item scroll{};
    scroll.kind = ItemKind::Scroll;
    scroll.subtype = static_cast<int>(ScrollKind::Identify);
    a.game.mutable_hero().inv.add(scroll);
    const int index = static_cast<int>(a.game.hero().inv.items.size()) - 1;

    a.game.perform(Action{ActionType::UseItem, {}, index, {}});
    EXPECT_TRUE(a.game.identification().knows(ItemKind::Potion,
                                              static_cast<int>(PotionKind::Regen)));
}

TEST(Items, UsingAnInvalidInventoryIndexIsRefused) {
    Arena a;
    EXPECT_FALSE(a.game.perform(Action{ActionType::UseItem, {}, -1, {}}));
    EXPECT_FALSE(a.game.perform(Action{ActionType::UseItem, {}, 999, {}}));
}

// --- Stairs ----------------------------------------------------------------

TEST(Stairs, DescendingRequiresStandingOnTheStaircase) {
    Arena a;
    EXPECT_FALSE(a.game.perform(Action{ActionType::Descend, {}, -1, {}}));
    EXPECT_EQ(a.game.depth(), 1);
}

TEST(Stairs, DescendingMovesToTheNextFloor) {
    Arena a;
    a.game.mutable_level().map.set(a.game.hero().a.pos, Tile::StairsDown);
    EXPECT_TRUE(a.game.perform(Action{ActionType::Descend, {}, -1, {}}));
    EXPECT_EQ(a.game.depth(), 2);
    EXPECT_EQ(a.game.hero().deepest, 2);
}

TEST(Stairs, TheFirstFloorLeadsBackToTheCrossroadsAndNoFurther) {
    // Climbing out of the first floor returns the hero to the crossroads,
    // which is a room and not an exit: from there the stairs only go down.
    Arena a;
    a.game.mutable_level().map.set(a.game.hero().a.pos, Tile::StairsUp);
    ASSERT_TRUE(a.game.perform(Action{ActionType::Ascend, {}, -1, {}}));
    EXPECT_EQ(a.game.depth(), kLobbyDepth);
    EXPECT_TRUE(a.game.in_lobby());

    a.game.mutable_level().map.set(a.game.hero().a.pos, Tile::StairsUp);
    EXPECT_FALSE(a.game.perform(Action{ActionType::Ascend, {}, -1, {}}));
    EXPECT_EQ(a.game.depth(), kLobbyDepth) << "Nav should not open onto the sky";
}

TEST(Stairs, AFloorIsRememberedWhenTheHeroComesBack) {
    // Levels are cached, so loot left behind and monsters killed must still be
    // that way on the way back up.
    Arena a;
    a.game.mutable_level().map.set(a.game.hero().a.pos, Tile::StairsDown);
    ASSERT_TRUE(a.game.perform(Action{ActionType::Descend, {}, -1, {}}));
    ASSERT_EQ(a.game.depth(), 2);

    a.game.mutable_level().monsters.clear();
    a.game.mutable_level().items.clear();
    Item marker{};
    marker.kind = ItemKind::Gold;
    marker.count = 999;
    marker.pos = a.game.hero().a.pos;
    a.game.mutable_level().items.push_back(marker);

    a.game.mutable_hero().a.pos = a.game.level().entrance;
    ASSERT_EQ(a.game.map().at(a.game.hero().a.pos), Tile::StairsUp);
    ASSERT_TRUE(a.game.perform(Action{ActionType::Ascend, {}, -1, {}}));
    ASSERT_EQ(a.game.depth(), 1);

    a.game.mutable_hero().a.pos = a.game.level().exit;
    ASSERT_TRUE(a.game.perform(Action{ActionType::Descend, {}, -1, {}}));
    ASSERT_EQ(a.game.depth(), 2);

    ASSERT_EQ(a.game.floor_items().size(), 1u);
    EXPECT_EQ(a.game.floor_items()[0].count, 999) << "the floor was regenerated instead of restored";
}

// --- Spells ----------------------------------------------------------------

TEST(Spells, TheSorcererStartsKnowingFireArrow) {
    Arena a(HeroClass::Vedun);
    EXPECT_TRUE(a.game.hero().knows(Spell::FireArrow));
    EXPECT_GT(a.game.hero().max_mana, 0);
}

TEST(Spells, TheWarriorKnowsNoSpellsAtFirst) {
    Arena a(HeroClass::Vityaz);
    EXPECT_TRUE(a.game.castable_spells().empty());
}

TEST(Spells, CastingAnUnknownSpellIsRefused) {
    Arena a(HeroClass::Vityaz);
    EXPECT_FALSE(a.game.perform(
        Action{ActionType::CastSpell, {}, static_cast<int>(Spell::Lightning), {11, 10}}));
}

TEST(Spells, FireArrowDamagesItsTargetAndSpendsPower) {
    Arena a(HeroClass::Vedun);
    a.spawn("upyr", {13, 10}, 60);
    const int hp_before = a.game.monsters()[0].a.hp;
    const int mana_before = a.game.hero().mana;

    ASSERT_TRUE(a.game.perform(
        Action{ActionType::CastSpell, {}, static_cast<int>(Spell::FireArrow), {13, 10}}));

    ASSERT_FALSE(a.game.monsters().empty());
    EXPECT_LT(a.game.monsters()[0].a.hp, hp_before);
    EXPECT_LT(a.game.hero().mana, mana_before);
}

TEST(Spells, CastingIsRefusedWithoutEnoughPower) {
    Arena a(HeroClass::Vedun);
    a.spawn("upyr", {13, 10}, 60);
    a.game.mutable_hero().mana = 0;
    EXPECT_FALSE(a.game.perform(
        Action{ActionType::CastSpell, {}, static_cast<int>(Spell::FireArrow), {13, 10}}));
}

TEST(Spells, TargetsOutOfRangeAreRefused) {
    Arena a(HeroClass::Vedun);
    const int range = spell_info(Spell::FireArrow).range;
    const Vec2 far_away{10 + range + 3, 10};
    a.spawn("upyr", far_away, 60);
    EXPECT_FALSE(a.game.perform(
        Action{ActionType::CastSpell, {}, static_cast<int>(Spell::FireArrow), far_away}));
}

TEST(Spells, TargetListingOnlyOffersVisibleCreaturesInRange) {
    Arena a(HeroClass::Vedun);
    a.spawn("upyr", {13, 10}, 60);
    a.game.perform(Action{ActionType::Wait, {}, -1, {}});  // refresh the field of view

    const Vec2 where = a.game.monsters()[0].a.pos;  // it may have taken a step
    const auto targets = a.game.spell_targets(Spell::FireArrow);
    ASSERT_FALSE(targets.empty());
    EXPECT_EQ(targets.front(), where) << "the only creature in range should be offered";

    EXPECT_TRUE(a.game.spell_targets(Spell::Heal).empty()) << "a self-spell needs no target";
}

TEST(Spells, HealingRestoresHealthWithoutATarget) {
    Arena a(HeroClass::Vedun);
    a.game.mutable_hero().learn(Spell::Heal);
    a.game.mutable_hero().mana = a.game.hero().max_mana;
    a.game.mutable_hero().a.hp = 3;

    ASSERT_TRUE(a.game.perform(
        Action{ActionType::CastSpell, {}, static_cast<int>(Spell::Heal), {-1, -1}}));
    EXPECT_GT(a.game.hero().a.hp, 3);
}

TEST(Spells, ConfusionMakesACastFizzleButStillCostPower) {
    Arena a(HeroClass::Vedun);
    a.spawn("upyr", {13, 10}, 60);
    a.game.mutable_hero().a.add_effect(Effect::Confusion, 5, 1);
    const int mana_before = a.game.hero().mana;
    const int hp_before = a.game.monsters()[0].a.hp;

    EXPECT_TRUE(a.game.perform(
        Action{ActionType::CastSpell, {}, static_cast<int>(Spell::FireArrow), {13, 10}}));
    EXPECT_LT(a.game.hero().mana, mana_before);
    ASSERT_FALSE(a.game.monsters().empty());
    EXPECT_EQ(a.game.monsters()[0].a.hp, hp_before) << "a fizzled spell must not still hit";
}

TEST(Spells, BossesShrugOffLongFreezes) {
    Arena a(HeroClass::Vedun);
    a.game.mutable_hero().learn(Spell::IceBind);
    a.game.mutable_hero().mana = 99;
    a.spawn("koschei", {13, 10}, 500);

    a.game.perform(Action{ActionType::CastSpell, {}, static_cast<int>(Spell::IceBind), {13, 10}});
    ASSERT_FALSE(a.game.monsters().empty());
    for (const auto& e : a.game.monsters()[0].a.effects) {
        if (e.kind == Effect::Freeze) {
            EXPECT_LE(e.turns, 2) << "a boss must not be frozen out of the fight";
        }
    }
}

// --- The shrine ------------------------------------------------------------

TEST(Shrine, PrayingElsewhereIsRefused) {
    Arena a;
    EXPECT_FALSE(a.game.perform(Action{ActionType::Pray, {}, -1, {}}));
}

TEST(Shrine, AnOfferingCostsGoldAndEnchantsWornGear) {
    Arena a;
    a.game.mutable_level().map.set(a.game.hero().a.pos, Tile::Altar);
    a.game.mutable_hero().gold = 5000;
    const int gold_before = a.game.hero().gold;
    const int attack_before = a.game.hero_attack();
    const int defence_before = a.game.hero_defence();

    ASSERT_TRUE(a.game.perform(Action{ActionType::Pray, {}, -1, {}}));

    EXPECT_LT(a.game.hero().gold, gold_before);
    EXPECT_TRUE(a.game.hero_attack() > attack_before || a.game.hero_defence() > defence_before)
        << "the offering changed nothing";
    EXPECT_NE(a.game.map().at(a.game.hero().a.pos), Tile::Altar) << "a shrine is single use";
}

TEST(Shrine, AnOfferingWithoutEnoughGoldIsRefused) {
    Arena a;
    a.game.mutable_level().map.set(a.game.hero().a.pos, Tile::Altar);
    a.game.mutable_hero().gold = 0;
    EXPECT_FALSE(a.game.perform(Action{ActionType::Pray, {}, -1, {}}));
    EXPECT_EQ(a.game.map().at(a.game.hero().a.pos), Tile::Altar) << "a refusal must not consume it";
}
