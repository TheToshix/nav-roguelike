// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "nav/geometry.hpp"
#include "nav/text.hpp"

namespace nav {

// ---------------------------------------------------------------------------
// Map
// ---------------------------------------------------------------------------

enum class Tile : std::uint8_t {
    Wall,
    Floor,
    StairsDown,
    StairsUp,
    Door,        ///< Blocks sight until opened; walking into it opens it.
    OpenDoor,
    Water,       ///< Passable, but costs extra energy to cross.
    Chasm,       ///< Impassable, though it does not block sight.
    Altar,       ///< Consumes an item, grants a blessing.
};

/// Closed doors are *walkable*: stepping into one opens it. Treating them as
/// solid here would make the connectivity repair pass tunnel around every door
/// it placed, which is how the first version of the generator produced levels
/// riddled with redundant corridors (see docs/BUG_REPORTS.md, NAV-004).
inline bool blocks_move(Tile t) {
    return t == Tile::Wall || t == Tile::Chasm;
}

inline bool blocks_sight(Tile t) {
    return t == Tile::Wall || t == Tile::Door;
}

// ---------------------------------------------------------------------------
// Hero classes
// ---------------------------------------------------------------------------

/// New classes are appended, never inserted: the value is written into save
/// files, so reordering would silently turn one hero into another.
enum class HeroClass : std::uint8_t {
    Vityaz,    ///< warrior — armour and health
    Vedun,     ///< sorcerer — spells
    Tat,       ///< rogue — speed, evasion, critical hits
    Znahar,    ///< herbalist — knows every potion, and they work better
    Kuznets,   ///< smith — every worn item counts as one grade better
    Bogatyr,   ///< champion — every blow sweeps all adjacent enemies
    Count
};

inline const char* hero_class_key(HeroClass c) {
    switch (c) {
        case HeroClass::Vityaz:  return "vityaz";
        case HeroClass::Vedun:   return "vedun";
        case HeroClass::Tat:     return "tat";
        case HeroClass::Znahar:  return "znahar";
        case HeroClass::Kuznets: return "kuznets";
        case HeroClass::Bogatyr: return "bogatyr";
        default:                 return "vityaz";
    }
}

/// What sets a class apart mechanically. Stat spreads alone make classes that
/// play the same; a trait changes how the game is played.
enum ClassTrait : std::uint32_t {
    TraitNone      = 0,
    TraitHerbalist = 1u << 0,  ///< Every potion is known on sight and works harder.
    TraitSmith     = 1u << 1,  ///< Worn gear counts as +1, and shrines charge half.
    TraitCleave    = 1u << 2,  ///< A melee blow also strikes every other adjacent foe.
};

// ---------------------------------------------------------------------------
// Status effects
// ---------------------------------------------------------------------------

enum class Effect : std::uint8_t {
    Poison,     ///< Damage over time, ignores armour.
    Burn,       ///< Heavier damage over time, shorter.
    Freeze,     ///< Target cannot act.
    Confusion,  ///< Movement direction is randomised.
    Blind,      ///< Sight radius drops to 1.
    Haste,      ///< +50% speed.
    Slow,       ///< -33% speed.
    Regen,      ///< Heals each turn.
    Might,      ///< +attack.
    Shield,     ///< +defence.
    Invisible,  ///< Monsters lose track of the hero.
    Sleep,      ///< The hero loses every turn until it wears off — a full input lock. Кот Баюн's song.
    Count
};

struct ActiveEffect {
    Effect kind{Effect::Poison};
    int turns{0};
    int power{1};
};

// ---------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------

/// Appended, never reordered — the value goes into save files.
enum class ItemKind : std::uint8_t { Weapon, Armor, Amulet, Potion, Scroll, Food, Gold, Needle, Feather };

enum class PotionKind : std::uint8_t {
    Heal, GreaterHeal, Mana, Might, Haste, Regen, Poison, Confusion, Count
};

enum class ScrollKind : std::uint8_t {
    Fireball, Lightning, Frost, Blind, Teleport, MagicMap, Identify, Summon, Count
};

// ---------------------------------------------------------------------------
// Spells
// ---------------------------------------------------------------------------

enum class Spell : std::uint8_t {
    FireArrow,   ///< Single target, burns.
    IceBind,     ///< Single target, freezes.
    Lightning,   ///< Pierces a line of targets.
    Heal,        ///< Self heal.
    Morok,       ///< Confuses everything nearby.
    Ward,        ///< Temporary shield.
    Count
};

// ---------------------------------------------------------------------------
// Actions the frontends may submit
// ---------------------------------------------------------------------------

enum class ActionType : std::uint8_t {
    None,
    Move,          ///< `dir` holds the step; attacks by bumping.
    Wait,
    PickUp,
    UseItem,       ///< `index` into the inventory.
    DropItem,
    EquipItem,
    Descend,
    Ascend,
    CastSpell,     ///< `index` is the Spell, `target` the aimed cell.
    Quaff,
    Pray,          ///< On an altar.
    /// Several ordinary steps in `dir`, until something worth noticing happens.
    /// Not a new way to move: a Run is a Move repeated by the engine rather
    /// than by the player's finger, so every rule that applies to one applies
    /// to the other.
    Run,
    /// Walks towards the nearest place the hero has not seen, with the same
    /// interruptions as a Run. When there is nothing left to find, it heads for
    /// the stairs down.
    Explore,
};

struct Action {
    ActionType type{ActionType::None};
    Vec2 dir{0, 0};
    int index{-1};
    Vec2 target{-1, -1};
};

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------

enum class Severity : std::uint8_t { Info, Good, Bad, Critical, System };

struct LogEntry {
    Text text;
    Severity severity{Severity::Info};
    int turn{0};
};

// ---------------------------------------------------------------------------
// Run outcome
// ---------------------------------------------------------------------------

enum class RunState : std::uint8_t { Playing, Dead, Ascended };

}  // namespace nav
