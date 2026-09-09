// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>

#include "nav/entity.hpp"
#include "nav/item.hpp"
#include "nav/text.hpp"
#include "nav/types.hpp"

namespace nav {

/// Total depth of the dungeon. Змей Горыныч waits on the last floor.
inline constexpr int kMaxDepth = 16;

/// Depth of the crossroads the hero sets out from. Not a dungeon floor: no
/// monsters, no generator, and the only place a run can be prepared for.
inline constexpr int kLobbyDepth = 0;

// ---------------------------------------------------------------------------
// The keys behaviour hangs on
// ---------------------------------------------------------------------------
//
// Content lives in tables and is looked up by a short string key, which is what
// keeps `data.cpp` readable and lets a frontend name a sprite without knowing
// an enum. The cost is that the key appears twice: once in the table that
// defines the creature, and once in the rule that gives it its mechanic. Spelt
// as bare literals, those two are free to disagree, and they disagree quietly —
// `species_index` returns -1 and the caller does nothing, or a `strcmp` never
// matches and a guardian simply stops using its ability. Nothing crashes and no
// test necessarily fails.
//
// So every key that a *rule* depends on is written down once, here, and used by
// both sides. Renaming a creature is now one edit that cannot half-apply.
// Keys that only ever name themselves — the ordinary bestiary, most of the
// gear — stay as literals in the table; they have no second place to drift to.
namespace species_key {

inline constexpr const char* kViy       = "viy";
inline constexpr const char* kBabaYaga  = "babayaga";
inline constexpr const char* kIzbushka  = "izbushka";
inline constexpr const char* kKoschei   = "koschei";
inline constexpr const char* kGorynych  = "gorynych";
inline constexpr const char* kMara      = "mara";
inline constexpr const char* kVodyanoy  = "vodyanoy";
inline constexpr const char* kMorozko   = "morozko";
inline constexpr const char* kPolozh    = "polozh";
inline constexpr const char* kOgnevik   = "ognevik";
inline constexpr const char* kKotBayun  = "kot_bayun";
inline constexpr const char* kSolovey   = "solovey";

}  // namespace species_key

namespace gear_key {

inline constexpr const char* kObZhizni    = "ob_zhizni";
inline constexpr const char* kObSily      = "ob_sily";
inline constexpr const char* kObZorko     = "ob_zorko";
inline constexpr const char* kObSkoro     = "ob_skoro";
inline constexpr const char* kObYada      = "ob_yada";
inline constexpr const char* kKoshkinGlaz = "koshkin_glaz";
inline constexpr const char* kPosokh      = "posokh";
inline constexpr const char* kMantiya     = "mantiya";

}  // namespace gear_key

/// Every key declared above, in one list each.
///
/// The defining tables in `data.cpp` still spell their keys as literals: they
/// are column-aligned so that a balance change reads as a balance change in the
/// diff, and threading constants through them would cost more than it buys.
/// What that leaves is one seam — a creature renamed in the table while the
/// constant keeps the old spelling — and `GameData.EveryBehaviourKeyNamesSomething`
/// stands on it. Add a constant above, add it here, and the test does the rest.
const std::vector<const char*>& behaviour_species_keys();
const std::vector<const char*>& behaviour_gear_keys();

// ---------------------------------------------------------------------------
// Zones
// ---------------------------------------------------------------------------

/// The dungeon is four belts of four floors, each ending in a guardian and
/// holding a lesser one halfway down.
///
/// Sixteen floors cut by one generator in one palette read as repetition.
/// Giving each belt its own algorithm, colours and hazards turns the descent
/// into a journey, and gives each boss a world instead of a bigger room.
enum class Zone : std::uint8_t {
    Rasputye, ///< 0: the crossroads above. Not generated; not a fight.
    Pogost,   ///< 1-4: dry crypts and corridors. Вий waits at the bottom.
    Chernotop,///< 5-8: flooded caves. Баба-Яга waits at the bottom.
    Koshchei, ///< 9-12: the frozen bone kingdom. Кощей waits at the bottom.
    Peklo,    ///< 13-16: burning stone and ash. Змей Горыныч waits at the bottom.
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

/// The belts of the descent, in the order they are met.
///
/// The crossroads is deliberately not one of them: nothing generates it and
/// nothing guards it. Anything that wants to list the belts — a title screen, a
/// manual, a test — asks here rather than writing the list out again, because a
/// hand-written copy is how the fourth belt spent a whole release invisible to
/// players (see docs/BUG_REPORTS.md, NAV-015).
const std::vector<Zone>& descending_belts();

/// The deepest floor of a belt — the one its guardian stands on.
int belt_last_depth(Zone zone);
const ZoneTheme& zone_theme(Zone zone);
inline const ZoneTheme& zone_theme_for_depth(int depth) { return zone_theme(zone_for_depth(depth)); }
/// True when `depth` is the first floor of its belt (where the flavour lands).
bool is_zone_entrance(int depth);

/// Rewrites a `#rrggbb` colour for a colour-blind-safe palette. For
/// `Palette::Colorblind` (red-green blindness): greens are pushed towards blue
/// and reds towards a lighter orange, so the two stop reading alike and each
/// keeps its own lightness. `Palette::Default`, an unparseable string, or a
/// colour already clear of the red-green axis comes back unchanged. Pure
/// function — both frontends run their map colours through it.
std::string display_color(const char* hex, Palette mode);

/// The floor event a belt can raise, or EventKind::None for belts that have
/// none (Погост, the crossroads). One place to ask, for the same reason as
/// descending_belts().
EventKind belt_event(Zone zone);
/// Short name and one-line note for a floor event, both languages.
Text event_name(EventKind kind);
Text event_note(EventKind kind);

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

/// What the log says when a boss crosses into a new phase.
///
/// The line is the only warning the player gets that the pattern they have
/// learned is about to stop working, so every boss has one for every phase it
/// can reach.
struct BossPhaseLine {
    const char* species_key;
    int phase;             ///< 2 or 3.
    Text line;
};

const std::vector<BossPhaseLine>& boss_phase_table();
/// The line for `key` entering `phase`, or an empty Text when there is none.
Text boss_phase_line(const char* key, int phase);

const std::vector<ClassTemplate>& class_table();
const ClassTemplate& class_info(HeroClass c);

const std::vector<SpellTemplate>& spell_table();
const SpellTemplate& spell_info(Spell s);

/// Spells a class gains, and the hero level at which each is granted.
const std::vector<std::pair<Spell, int>>& class_spells(HeroClass c);

const std::vector<BossPlacement>& boss_table();
/// Species key of the boss guarding `depth`, or nullptr.
const char* boss_for_depth(int depth);
/// The floor `species_key` is placed on, or -1.
///
/// The inverse of `boss_for_depth`, and the reason it exists: anything that
/// belongs to a particular boss — Кощей's needle, most of all — has to be
/// placed by asking where that boss is, not by spelling out a number that was
/// true when it was written (see docs/BUG_REPORTS.md, NAV-011).
int boss_depth(const char* species_key);

/// Spawn weight of `species` on `depth`, after the depth window is applied.
int spawn_weight(const Species& s, int depth);

/// XP required to reach `level` from level 1.
int xp_for_level(int level);

}  // namespace nav
