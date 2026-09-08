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
    const char* start_weapon;  ///< Key into gear_table().
    const char* start_armor;
};

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
