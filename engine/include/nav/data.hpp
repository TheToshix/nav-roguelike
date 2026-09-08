// SPDX-License-Identifier: MIT
#pragma once
#include <vector>

#include "nav/entity.hpp"
#include "nav/item.hpp"
#include "nav/text.hpp"
#include "nav/types.hpp"

namespace nav {

/// Total depth of the dungeon. Кощей waits on the last floor.
inline constexpr int kMaxDepth = 12;

// ---------------------------------------------------------------------------
// Zones
// ---------------------------------------------------------------------------

/// The dungeon is three belts of four floors, each ending in a boss.
///
/// Twelve floors cut by one generator in one palette read as repetition. Giving
/// each belt its own algorithm, colours and hazards turns the descent into a
/// journey, and gives each boss a world instead of a bigger room.
enum class Zone : std::uint8_t {
    Pogost,   ///< 1-4: dry crypts and corridors. Вий waits at the bottom.
    Chernotop,///< 5-8: flooded caves. Баба-Яга waits at the bottom.
    Koshchei, ///< 9-12: the frozen bone kingdom. Кощей waits at the bottom.
};

struct ZoneTheme {
    Zone zone;
    Text name;
    Text arrival;          ///< Shown once, on first entering the belt.
    const char* wall_color;
    const char* floor_color;
    const char* liquid_color;
    Text liquid_name;      ///< Water is water in a crypt and black mire in a swamp.
    bool caves;            ///< Cellular-automaton caves instead of BSP rooms.
    int water_chance;
    int chasm_chance;
    int door_chance;
    int extra_monsters;
};

Zone zone_for_depth(int depth);
const ZoneTheme& zone_theme(Zone zone);
inline const ZoneTheme& zone_theme_for_depth(int depth) { return zone_theme(zone_for_depth(depth)); }
/// True when `depth` is the first floor of its belt (where the flavour lands).
bool is_zone_entrance(int depth);

/// Starting loadout and growth curve for one hero class.
struct ClassTemplate {
    HeroClass cls;
    Text name;
    Text blurb;
    int hp;
    int mana;
    int attack;
    int defence;
    int speed;
    int sight;
    int hp_per_level;
    int mana_per_level;
    int crit_chance;      ///< Percent chance a melee hit deals double damage.
    int evasion;          ///< Percent chance to dodge an incoming melee hit.
    std::uint32_t traits;      ///< ClassTrait flags — what makes the class play differently.
    const char* start_weapon;  ///< Key into gear_table().
    const char* start_armor;
};

/// True when the class carries the given trait.
bool class_has(HeroClass c, ClassTrait trait);

struct SpellTemplate {
    Spell spell;
    Text name;
    Text note;
    int cost;         ///< Mana.
    int range;        ///< 0 = self.
    int power;        ///< Damage or heal amount at hero level 1.
    bool needs_target;
};

/// A boss placed by hand on a fixed floor.
struct BossPlacement {
    int depth;
    const char* species_key;
};

const std::vector<ClassTemplate>& class_table();
const ClassTemplate& class_info(HeroClass c);

const std::vector<SpellTemplate>& spell_table();
const SpellTemplate& spell_info(Spell s);

/// Spells a class gains, and the hero level at which each is granted.
const std::vector<std::pair<Spell, int>>& class_spells(HeroClass c);

const std::vector<BossPlacement>& boss_table();
/// Species key of the boss guarding `depth`, or nullptr.
const char* boss_for_depth(int depth);

/// Spawn weight of `species` on `depth`, after the depth window is applied.
int spawn_weight(const Species& s, int depth);

/// XP required to reach `level` from level 1.
int xp_for_level(int level);

}  // namespace nav
