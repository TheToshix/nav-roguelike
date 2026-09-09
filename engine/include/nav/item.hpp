// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>

#include "nav/text.hpp"
#include "nav/types.hpp"

namespace nav {

/// Where an item can be worn. `None` means it is consumed, not equipped.
enum class Slot : std::uint8_t { None, Weapon, Armor, Amulet };

/// What a piece of gear does beyond its plain attack or armour number.
///
/// Numbers alone make gear that is only ever "the bigger one"; a flag makes a
/// choice. Bits rather than an enum because one item may carry two.
enum GearPower : std::uint32_t {
    GpNone       = 0,
    GpStun       = 1u << 0,  ///< A blow may leave the target unable to act.
    GpVsBoss     = 1u << 1,  ///< Heavier against the floor guardians.
    GpWard       = 1u << 2,  ///< Turns aside one blow, then needs a floor to recover.
    GpThorns     = 1u << 3,  ///< Returns part of the damage taken.
    GpLifesteal  = 1u << 4,  ///< A kill returns health.
    GpQuick      = 1u << 5,  ///< Faster on your feet.
    GpSight      = 1u << 6,  ///< Sees further.
    GpNoBurn     = 1u << 7,  ///< Fire does not catch.
    GpNoPoison   = 1u << 8,  ///< Venom does not take.
    GpRegen      = 1u << 9,  ///< Slow, constant mending.
    GpCheapSpell = 1u << 10, ///< Spells cost less.
    GpRichGold   = 1u << 11, ///< Finds more gold.
    GpNoSleep    = 1u << 12, ///< Кот Баюн's song cannot put you under.
};

/// The four matched sets. Each is exactly one weapon, one armour and one
/// amulet, so a full set fills the hero completely — wearing one is a decision
/// about the whole run rather than about a slot.
enum class GearSet : std::uint8_t { None, Oberezhny, Ratny, Naviy, Hodovoy, Count };

/// A static entry in the weapon/armour/amulet tables.
struct GearTemplate {
    const char* key;
    Text name;
    ItemKind kind;
    char glyph;
    int power;       ///< Attack bonus, or armour value.
    int min_depth;   ///< Earliest floor this may be generated on.
    int weight;      ///< Relative spawn weight.
    Text note;       ///< Short flavour/effect line shown in the inventory.
    std::uint32_t powers{GpNone};
    GearSet set{GearSet::None};
};

/// Name and effect of a completed set, for the inventory screen.
struct GearSetInfo {
    GearSet set;
    Text name;
    Text note;
};

const std::vector<GearSetInfo>& gear_set_table();
const GearSetInfo& gear_set_info(GearSet s);

/// A two-piece matched pair: a lighter commitment than a full set. One power for
/// wearing both halves; each half is an ordinary stat item on its own.
struct GearPair {
    const char* key_a;
    const char* key_b;
    Text name;
    Text note;
    std::uint32_t powers;   ///< GearPower bits, granted only while both are worn.
};

const std::vector<GearPair>& gear_pair_table();

/// One item, either on the floor or in the pack.
struct Item {
    ItemKind kind{ItemKind::Gold};
    int subtype{0};      ///< Index into the matching table (gear / potion / scroll).
    int power{0};        ///< Base effect magnitude.
    int enchant{0};      ///< +N from altars; adds to power for gear.
    int count{1};        ///< Stack size; only gold and consumables stack.
    bool identified{false};
    /// A cursed piece of gear cannot be taken off, dropped or swapped out once
    /// worn — only a Scroll of Remove Curse frees the slot. Unknown until it is
    /// put on for the first time.
    bool cursed{false};
    Vec2 pos{-1, -1};    ///< Valid only while the item lies on the floor.

    bool is_gear() const {
        return kind == ItemKind::Weapon || kind == ItemKind::Armor || kind == ItemKind::Amulet;
    }
    bool stackable() const {
        return kind == ItemKind::Gold || kind == ItemKind::Potion ||
               kind == ItemKind::Scroll || kind == ItemKind::Food;
    }
    /// Two items merge only when every field that affects behaviour matches.
    bool same_as(const Item& o) const {
        return kind == o.kind && subtype == o.subtype && power == o.power &&
               enchant == o.enchant && identified == o.identified;
    }
    int total_power() const { return power + enchant; }
};

/// Per-run scrambling of consumable appearances.
///
/// Potions and scrolls are described by a random label until the hero drinks or
/// reads one ("мутное зелье" -> "зелье исцеления"). The permutation is derived
/// from the run seed, so an identified potion stays identified across a save.
struct Identification {
    std::vector<int> potion_look;  ///< PotionKind -> appearance index
    std::vector<int> scroll_look;  ///< ScrollKind -> appearance index
    std::vector<std::uint8_t> potion_known;
    std::vector<std::uint8_t> scroll_known;

    void reset(std::size_t potions, std::size_t scrolls) {
        potion_look.resize(potions);
        scroll_look.resize(scrolls);
        potion_known.assign(potions, 0);
        scroll_known.assign(scrolls, 0);
        for (std::size_t i = 0; i < potions; ++i) potion_look[i] = static_cast<int>(i);
        for (std::size_t i = 0; i < scrolls; ++i) scroll_look[i] = static_cast<int>(i);
    }

    bool knows(ItemKind kind, int subtype) const {
        const auto& flags = kind == ItemKind::Potion ? potion_known : scroll_known;
        const std::size_t i = static_cast<std::size_t>(subtype);
        return i < flags.size() && flags[i] != 0;
    }

    void learn(ItemKind kind, int subtype) {
        auto& flags = kind == ItemKind::Potion ? potion_known : scroll_known;
        const std::size_t i = static_cast<std::size_t>(subtype);
        if (i < flags.size()) flags[i] = 1;
    }
};

// --- Static tables (defined in data.cpp) ------------------------------------

const std::vector<GearTemplate>& gear_table();
const std::vector<Text>& potion_names();       ///< True names, indexed by PotionKind.
const std::vector<Text>& scroll_names();       ///< True names, indexed by ScrollKind.
const std::vector<Text>& potion_appearances(); ///< Unidentified labels.
const std::vector<Text>& scroll_appearances();
const std::vector<Text>& potion_notes();
const std::vector<Text>& scroll_notes();

/// What a status effect is called, in both languages.
///
/// Lives here rather than in a frontend because the log names effects too, and
/// a message that says "something foul takes hold of you" while the sidebar
/// says "poison" is two different games describing one event.
Text effect_name(Effect e);

/// Display name, respecting what the hero has identified so far.
Text item_name(const Item& it, const Identification& ident);
/// One-line description of what the item does (empty while unidentified).
Text item_note(const Item& it, const Identification& ident);
char item_glyph(const Item& it);
Slot item_slot(const Item& it);

}  // namespace nav
