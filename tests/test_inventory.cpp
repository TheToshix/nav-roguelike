// SPDX-License-Identifier: MIT
//
// The inventory stores equipment as indices into its own vector, which is the
// kind of design that breaks the moment an item is removed from the middle.
// These tests exist mostly to pin that behaviour down.
#include <gtest/gtest.h>

#include "nav/data.hpp"
#include "nav/entity.hpp"
#include "nav/item.hpp"

using namespace nav;

namespace {

Item potion(PotionKind kind, int count = 1) {
    Item it{};
    it.kind = ItemKind::Potion;
    it.subtype = static_cast<int>(kind);
    it.count = count;
    return it;
}

Item gear_named(const char* key) {
    const auto& table = gear_table();
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (std::string(table[i].key) != key) continue;
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

}  // namespace

TEST(Inventory, AddingIdenticalConsumablesMergesTheStack) {
    Inventory inv;
    EXPECT_TRUE(inv.add(potion(PotionKind::Heal, 2)));
    EXPECT_TRUE(inv.add(potion(PotionKind::Heal, 3)));
    ASSERT_EQ(inv.items.size(), 1u);
    EXPECT_EQ(inv.items[0].count, 5);
}

TEST(Inventory, DifferentConsumablesDoNotMerge) {
    Inventory inv;
    inv.add(potion(PotionKind::Heal));
    inv.add(potion(PotionKind::Haste));
    EXPECT_EQ(inv.items.size(), 2u);
}

TEST(Inventory, ItemsWithDifferentEnchantmentsDoNotMerge) {
    Inventory inv;
    Item plain = gear_named("mech");
    Item blessed = gear_named("mech");
    blessed.enchant = 1;
    inv.add(plain);
    inv.add(blessed);
    EXPECT_EQ(inv.items.size(), 2u) << "gear does not stack, and +1 is not the same item";
}

TEST(Inventory, EquipmentDoesNotStack) {
    Inventory inv;
    inv.add(gear_named("mech"));
    inv.add(gear_named("mech"));
    EXPECT_EQ(inv.items.size(), 2u);
}

TEST(Inventory, RejectsItemsOnceFull) {
    Inventory inv;
    for (std::size_t i = 0; i < Inventory::kCapacity; ++i) {
        Item it = gear_named("nozh");
        it.enchant = static_cast<int>(i);  // keep every entry distinct
        ASSERT_TRUE(inv.add(it)) << "slot " << i << " was refused too early";
    }
    EXPECT_TRUE(inv.full());
    Item extra = gear_named("mech");
    EXPECT_FALSE(inv.add(extra));
    EXPECT_EQ(inv.items.size(), Inventory::kCapacity);
}

TEST(Inventory, AFullPackStillAcceptsMoreOfAnExistingStack) {
    Inventory inv;
    inv.add(potion(PotionKind::Heal, 1));
    for (std::size_t i = 1; i < Inventory::kCapacity; ++i) {
        Item it = gear_named("nozh");
        it.enchant = static_cast<int>(i);
        inv.add(it);
    }
    ASSERT_TRUE(inv.full());
    EXPECT_TRUE(inv.add(potion(PotionKind::Heal, 4))) << "merging must not need a free slot";
    EXPECT_EQ(inv.items[0].count, 5);
}

TEST(Inventory, TakingPartOfAStackLeavesTheRest) {
    Inventory inv;
    inv.add(potion(PotionKind::Heal, 5));
    const Item taken = inv.take(0, 2);
    EXPECT_EQ(taken.count, 2);
    ASSERT_EQ(inv.items.size(), 1u);
    EXPECT_EQ(inv.items[0].count, 3);
}

TEST(Inventory, TakingTheWholeStackRemovesTheEntry) {
    Inventory inv;
    inv.add(potion(PotionKind::Heal, 2));
    inv.take(0, 2);
    EXPECT_TRUE(inv.items.empty());
}

TEST(Inventory, TakingMoreThanIsPresentTakesWhatThereIs) {
    Inventory inv;
    inv.add(potion(PotionKind::Heal, 2));
    const Item taken = inv.take(0, 99);
    EXPECT_EQ(taken.count, 2);
    EXPECT_TRUE(inv.items.empty());
}

TEST(Inventory, TakingAnInvalidIndexIsHarmless) {
    Inventory inv;
    inv.add(potion(PotionKind::Heal));
    EXPECT_EQ(inv.take(-1).count, 1) << "a default-constructed Item is returned";
    EXPECT_EQ(inv.take(99).count, 1);
    EXPECT_EQ(inv.items.size(), 1u) << "the pack must be untouched";
}

TEST(Inventory, RemovingAnItemShiftsTheEquipmentIndicesDown) {
    // This is the regression the whole file is really about: dropping the
    // first item used to leave the weapon slot pointing at the wrong entry.
    Inventory inv;
    inv.add(potion(PotionKind::Heal));   // index 0
    inv.add(gear_named("mech"));         // index 1
    inv.add(gear_named("kolchuga"));     // index 2
    inv.weapon = 1;
    inv.armor = 2;

    inv.take(0, 1);  // drop the potion

    EXPECT_EQ(inv.weapon, 0);
    EXPECT_EQ(inv.armor, 1);
    EXPECT_EQ(inv.items[static_cast<std::size_t>(inv.weapon)].kind, ItemKind::Weapon);
    EXPECT_EQ(inv.items[static_cast<std::size_t>(inv.armor)].kind, ItemKind::Armor);
}

TEST(Inventory, RemovingTheEquippedItemClearsItsSlot) {
    Inventory inv;
    inv.add(gear_named("mech"));
    inv.add(gear_named("kolchuga"));
    inv.weapon = 0;
    inv.armor = 1;

    inv.take(0, 1);

    EXPECT_EQ(inv.weapon, -1) << "the weapon slot must be emptied, not left dangling";
    EXPECT_EQ(inv.armor, 0);
}

TEST(Inventory, RemovingALaterItemLeavesEarlierSlotsAlone) {
    Inventory inv;
    inv.add(gear_named("mech"));       // 0
    inv.add(potion(PotionKind::Heal)); // 1
    inv.weapon = 0;

    inv.take(1, 1);
    EXPECT_EQ(inv.weapon, 0);
}

TEST(Inventory, IsEquippedReportsEverySlot) {
    Inventory inv;
    inv.add(gear_named("mech"));
    inv.add(gear_named("kolchuga"));
    inv.add(gear_named("ob_sily"));
    inv.weapon = 0;
    inv.armor = 1;
    inv.amulet = 2;

    EXPECT_TRUE(inv.is_equipped(0));
    EXPECT_TRUE(inv.is_equipped(1));
    EXPECT_TRUE(inv.is_equipped(2));
    EXPECT_FALSE(inv.is_equipped(3));
    EXPECT_FALSE(inv.is_equipped(-1));
}

// --- Item naming and identification ---------------------------------------

TEST(Identification, ConsumablesShowAnAppearanceUntilTheyAreTried) {
    Identification ident;
    ident.reset(static_cast<std::size_t>(PotionKind::Count),
                static_cast<std::size_t>(ScrollKind::Count));

    const Item it = potion(PotionKind::GreaterHeal);
    const Text before = item_name(it, ident);
    EXPECT_NE(before.ru, potion_names()[static_cast<std::size_t>(PotionKind::GreaterHeal)].ru);
    EXPECT_EQ(item_note(it, ident).ru, "Неизвестно, что это.");

    ident.learn(ItemKind::Potion, it.subtype);
    EXPECT_EQ(item_name(it, ident).ru,
              potion_names()[static_cast<std::size_t>(PotionKind::GreaterHeal)].ru);
    EXPECT_FALSE(item_note(it, ident).empty());
}

TEST(Identification, LearningOnePotionDoesNotRevealAnother) {
    Identification ident;
    ident.reset(static_cast<std::size_t>(PotionKind::Count),
                static_cast<std::size_t>(ScrollKind::Count));
    ident.learn(ItemKind::Potion, static_cast<int>(PotionKind::Heal));

    EXPECT_TRUE(ident.knows(ItemKind::Potion, static_cast<int>(PotionKind::Heal)));
    EXPECT_FALSE(ident.knows(ItemKind::Potion, static_cast<int>(PotionKind::Haste)));
    EXPECT_FALSE(ident.knows(ItemKind::Scroll, static_cast<int>(ScrollKind::Fireball)));
}

TEST(Identification, KnowsIsSafeForOutOfRangeSubtypes) {
    Identification ident;
    ident.reset(4, 4);
    EXPECT_FALSE(ident.knows(ItemKind::Potion, 99));
    EXPECT_FALSE(ident.knows(ItemKind::Scroll, -1));
}

TEST(ItemNaming, EnchantedGearShowsItsBonus) {
    Item sword = gear_named("mech");
    sword.enchant = 2;
    Identification ident;
    ident.reset(8, 8);
    EXPECT_NE(item_name(sword, ident).ru.find("+2"), std::string::npos);
}

TEST(ItemNaming, GoldShowsItsAmount) {
    Item gold{};
    gold.kind = ItemKind::Gold;
    gold.count = 137;
    Identification ident;
    ident.reset(8, 8);
    EXPECT_NE(item_name(gold, ident).ru.find("137"), std::string::npos);
}

TEST(ItemSlots, MapToTheRightEquipmentSlot) {
    EXPECT_EQ(item_slot(gear_named("mech")), Slot::Weapon);
    EXPECT_EQ(item_slot(gear_named("kolchuga")), Slot::Armor);
    EXPECT_EQ(item_slot(gear_named("ob_sily")), Slot::Amulet);
    EXPECT_EQ(item_slot(potion(PotionKind::Heal)), Slot::None);
}

TEST(GameData, TablesAreInternallyConsistent) {
    // Cheap guards against a typo in the content tables — the kind of mistake
    // that otherwise only shows up as a strange crash three floors down.
    for (const auto& species : bestiary()) {
        EXPECT_GT(species.hp, 0) << species.key;
        EXPECT_GT(species.attack, 0) << species.key;
        EXPECT_GE(species.defence, 0) << species.key;
        EXPECT_GT(species.speed, 0) << species.key;
        EXPECT_GE(species.min_depth, 1) << species.key;
        EXPECT_LE(species.max_depth, kMaxDepth) << species.key;
        EXPECT_LE(species.min_depth, species.max_depth) << species.key;
        EXPECT_FALSE(species.name.ru.empty()) << species.key;
        EXPECT_FALSE(species.name.en.empty()) << species.key;
        EXPECT_NE(species.ai, 0u) << species.key << " has no behaviour at all";
    }

    for (const auto& gear : gear_table()) {
        EXPECT_GT(gear.power, 0) << gear.key;
        EXPECT_GE(gear.min_depth, 1) << gear.key;
        // Weight 0 is allowed, and means "never rolled by the loot tables,
        // placed by hand" — Кот Баюн's charm is one such. A negative weight is
        // still a typo.
        EXPECT_GE(gear.weight, 0) << gear.key;
    }

    EXPECT_EQ(potion_names().size(), static_cast<std::size_t>(PotionKind::Count));
    EXPECT_EQ(potion_notes().size(), static_cast<std::size_t>(PotionKind::Count));
    EXPECT_EQ(potion_appearances().size(), static_cast<std::size_t>(PotionKind::Count));
    EXPECT_EQ(scroll_names().size(), static_cast<std::size_t>(ScrollKind::Count));
    EXPECT_EQ(scroll_notes().size(), static_cast<std::size_t>(ScrollKind::Count));
    EXPECT_EQ(scroll_appearances().size(), static_cast<std::size_t>(ScrollKind::Count));
    EXPECT_EQ(spell_table().size(), static_cast<std::size_t>(Spell::Count));
}

TEST(GameData, EveryFloorHasSomethingThatCanSpawnOnIt) {
    for (int depth = 1; depth <= kMaxDepth; ++depth) {
        int total = 0;
        for (const auto& species : bestiary()) total += spawn_weight(species, depth);
        EXPECT_GT(total, 0) << "depth " << depth << " has an empty spawn table";
    }
}

TEST(GameData, EveryBossExistsInTheBestiary) {
    for (const auto& placement : boss_table()) {
        const int index = species_index(placement.species_key);
        ASSERT_GE(index, 0) << placement.species_key;
        EXPECT_TRUE(bestiary()[static_cast<std::size_t>(index)].ai & AiBoss)
            << placement.species_key << " is placed as a boss but is not flagged as one";
        EXPECT_EQ(bestiary()[static_cast<std::size_t>(index)].weight, 0)
            << placement.species_key << " could also spawn randomly";
    }
    EXPECT_EQ(species_index("no_such_creature"), -1);
}

TEST(GameData, ExperienceCurveRisesMonotonically) {
    EXPECT_EQ(xp_for_level(1), 0);
    for (int level = 2; level <= 30; ++level)
        EXPECT_GT(xp_for_level(level), xp_for_level(level - 1)) << "at level " << level;
}

TEST(GameData, EveryClassStartsWithGearThatExists) {
    for (const auto& cls : class_table()) {
        EXPECT_GT(cls.hp, 0) << cls.name.en;
        EXPECT_GT(cls.speed, 0) << cls.name.en;
        bool weapon_found = false, armor_found = false;
        for (const auto& gear : gear_table()) {
            if (std::string(gear.key) == cls.start_weapon) weapon_found = true;
            if (std::string(gear.key) == cls.start_armor) armor_found = true;
        }
        EXPECT_TRUE(weapon_found) << cls.start_weapon << " is not in the gear table";
        EXPECT_TRUE(armor_found) << cls.start_armor << " is not in the gear table";
    }
}
