// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "nav/data.hpp"
#include "nav/entity.hpp"
#include "nav/item.hpp"
#include "nav/map.hpp"
#include "nav/mapgen.hpp"
#include "nav/pathfind.hpp"
#include "nav/rng.hpp"
#include "nav/types.hpp"

namespace nav {

/// The difference a piece of gear would make, for the inventory screen.
struct EquipPreview {
    bool valid{false};       ///< False when the item cannot be worn at all.
    bool taking_off{false};  ///< True when the item is currently worn.
    int attack{0};
    int defence{0};
    int max_hp{0};
    int speed{0};
    int sight{0};

    bool changes_nothing() const {
        return attack == 0 && defence == 0 && max_hp == 0 && speed == 0 && sight == 0;
    }
};

/// What the ending screen needs in order to explain a death.
///
/// Death here is final, so the game owes the player a reason. A screen that
/// says only "you died" invites the conclusion that the game cheated; the last
/// few blows, the killer, and the healing potion that was still in the pack
/// usually say something much more useful — often "you had the answer and did
/// not use it".
struct Postmortem {
    struct Blow {
        Text source;     ///< What did it: a creature, poison, a fall, starvation.
        int amount{0};   ///< Health actually lost.
        int hp_left{0};
        int turn{0};
    };
    Text killed_by;
    std::vector<Blow> blows;      ///< Oldest first; at most kPostmortemBlows.
    std::vector<Text> unspent;    ///< Consumables still in the pack at the end.
};

/// How many blows the ending screen looks back over.
inline constexpr std::size_t kPostmortemBlows = 5;

struct GameConfig {
    std::uint64_t seed{0};
    std::string seed_text;          ///< What the player typed, kept for display.
    HeroClass hero_class{HeroClass::Vityaz};
    int map_width{72};
    int map_height{34};
};

/// The guardian's hall: a walled room with one doorway.
///
/// A boss that can be pulled into a corridor and fought one square at a time is
/// not a boss, it is a monster with a large health bar — every mechanic that
/// makes a guardian interesting (the huts, the flooded floor, the fire that
/// walks a line) needs room to happen. So the fight gets a room, and while it
/// lasts neither side leaves it.
struct Arena {
    bool exists{false};
    Vec2 min{0, 0};        ///< Inclusive corner of the interior.
    Vec2 max{-1, -1};      ///< Inclusive far corner of the interior.
    Vec2 door{-1, -1};     ///< The one way in, on the wall.
    bool sealed{false};    ///< True once the hero has stepped inside.
    bool warned{false};    ///< The threshold is announced once, not every turn.
    /// Кощей's hall is never sealed: his death lies on a needle elsewhere on
    /// the floor, so locking the player in with him would be locking them in
    /// with something they cannot kill.
    bool seals{true};

    bool contains(Vec2 p) const {
        return exists && p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }
    /// The doorway counts as neither in nor out: it is the decision point.
    bool is_door(Vec2 p) const { return exists && p.x == door.x && p.y == door.y; }
};

/// A cell that is on fire, and the number of turns it has left to burn.
///
/// Пекло's own tactical device: the Огневик trails these behind it, and anything
/// standing on one takes fire damage on its turn. Kept as a list on the level
/// rather than as a tile so it needs no new Tile value, no change to the
/// generator or the pathfinder, and expires on its own.
struct Ember {
    Vec2 pos{-1, -1};
    int turns{0};
};

/// How long a cell the Огневик lights keeps burning.
inline constexpr int kEmberTurns = 3;

/// One dungeon floor, kept in memory so ascending returns to the level as it
/// was left — corpses, dropped loot and explored tiles included.
struct Level {
    Map map;
    std::vector<Monster> monsters;
    std::vector<Item> items;
    std::vector<Ember> embers;
    Vec2 entrance{-1, -1};
    Vec2 exit{-1, -1};
    bool generated{false};
    bool boss_slain{false};
    Arena arena;
};

/// Everything the frontends need to draw one cell.
///
/// `glyph`/`color` are what the terminal draws. `terrain` and `entity` are
/// stable string keys naming *what* is there, so a frontend with real artwork
/// can look up a sprite instead — and can draw the two as separate layers, with
/// a creature standing on a floor rather than replacing it. The engine still
/// decides what is where; it just no longer decides that the answer is a
/// character.
struct RenderCell {
    char glyph{' '};
    const char* color{"#888"};
    bool visible{false};
    bool explored{false};
    const char* terrain{"floor"};   ///< Ground: "wall", "water", "stairs_down", ...
    const char* entity{nullptr};    ///< On top: a species key, "hero_vityaz", "item_gold", ...
};

/// Sprite key for the hero of a given class ("hero_vityaz" and so on).
const char* hero_sprite_key(HeroClass c);
/// Sprite key for an item ("item_potion", "item_gold", ...).
const char* item_sprite_key(ItemKind kind);
/// Sprite key for a tile ("wall", "floor", "door", ...).
const char* tile_sprite_key(Tile t);
/// Sprite key for a burning cell, drawn over the floor beneath whatever stands
/// in it. Not a tile: fire is a timed overlay (see `Ember`).
const char* ember_sprite_key();

/// The complete rules engine.
///
/// `Game` performs no input and no output: frontends submit an Action and read
/// the resulting state back. That is what makes the whole rule set testable
/// without a screen, and what lets the same object drive both the terminal
/// build and the WebAssembly build.
class Game {
public:
    Game() = default;

    // --- Lifecycle --------------------------------------------------------
    void start(const GameConfig& cfg);
    /// Applies one hero action. Returns true when it consumed a turn.
    bool perform(const Action& action);

    // --- Queries ----------------------------------------------------------
    const Map& map() const { return level().map; }
    const Level& level() const { return levels_[static_cast<std::size_t>(depth_)]; }
    /// Any floor the run has been to, by depth. Floors are kept in memory, so
    /// this is how a harness asks what became of a guardian two belts up.
    const Level& level_at(int depth) const {
        return levels_[static_cast<std::size_t>(std::clamp(depth, kLobbyDepth, kMaxDepth))];
    }
    const Hero& hero() const { return hero_; }
    const std::vector<Monster>& monsters() const { return level().monsters; }
    const std::vector<Item>& floor_items() const { return level().items; }
    const std::vector<LogEntry>& log() const { return log_; }
    const Identification& identification() const { return ident_; }
    const GameConfig& config() const { return cfg_; }

    int depth() const { return depth_; }
    int turn() const { return turn_; }
    RunState state() const { return state_; }

    /// One byte per species, 1 once the hero has laid eyes on it: the codex.
    /// Kept in the save so a run's progress through the bestiary survives a
    /// reload. Always as long as `bestiary()`.
    const std::vector<std::uint8_t>& codex_seen() const { return codex_seen_; }
    bool codex_knows(int species) const {
        const std::size_t i = static_cast<std::size_t>(species);
        return i < codex_seen_.size() && codex_seen_[i] != 0;
    }

    // --- What the achievements watch ------------------------------------
    /// True once the run has used the run key or auto-explore even once.
    bool used_run_or_explore() const { return ever_ran_ || ever_explored_; }
    /// True while not one point of health has been lost this run.
    bool flawless() const { return !ever_hurt_; }
    /// The deepest floor reached before the first point of damage — frozen the
    /// moment any is taken.
    int deepest_unhurt() const { return deepest_unhurt_; }
    /// Final score: gold, depth, kills and hero level rolled into one number.
    int score() const;

    /// Derived hero statistics, after gear and status effects.
    int hero_attack() const;
    int hero_defence() const;
    int hero_sight() const;
    int hero_speed() const;

    /// The belt of the dungeon the hero is currently in.
    Zone zone() const { return zone_for_depth(depth_); }
    /// True while the hero stands on the crossroads rather than in the dungeon.
    bool in_lobby() const { return depth_ <= kLobbyDepth; }

    /// Every GearPower flag carried by what the hero is wearing right now.
    std::uint32_t hero_powers() const;
    /// The set the hero has completed, or GearSet::None.
    ///
    /// A set needs all three of its pieces worn at once, which is every slot
    /// the hero has — so this is never a happy accident.
    GearSet hero_set() const;
    /// The two-piece pair the hero has completed, or nullptr.
    const GearPair* hero_pair() const;
    bool hero_has(GearPower p) const { return (hero_powers() & p) != 0; }

    /// The boss of this floor while it still lives, or nullptr. The frontends
    /// use it to name the fight and to choose its music.
    const Monster* active_boss() const;
    /// True while Кощей can still rise again — that is, the needle is unbroken.
    bool needle_intact() const { return !needle_broken_; }

    /// True when anything alive is currently in the hero's sight.
    bool foe_in_view() const;

    /// The guardian's hall on this floor, if it has one.
    const Arena& arena() const { return level().arena; }
    /// Whether `who` may step from `from` to `to` given the seal.
    ///
    /// One function for both sides of the fight: the hero cannot walk out and
    /// the guardian cannot walk out, and neither can anything else that is in
    /// there with them. Two rules would eventually disagree, and the way a
    /// player would find out is by watching a boss stroll through the wall.
    bool arena_allows(Vec2 from, Vec2 to) const;

    /// A snapshot of "the situation", for anything that walks several steps.
    ///
    /// Three places need to agree on when a multi-step movement has to stop:
    /// the engine's own run and auto-explore, a key held down in the terminal,
    /// and a key held down in the browser. They are the same promise — the
    /// player asked to keep walking, not to keep walking into whatever turns
    /// up — so they read one rule rather than three lookalikes.
    struct Situation {
        int hp{0};
        int depth{0};
        std::uint32_t effects{0};
        bool foes{false};
        bool underfoot{false};   ///< An item, stairs or a shrine on this cell.
    };
    Situation situation() const;
    /// Whether anything worth stopping for has happened since `before`.
    bool situation_changed(const Situation& before) const;

    /// The monster standing on `p`, or nullptr.
    const Monster* monster_at(Vec2 p) const;
    /// Index of the topmost floor item at `p`, or -1.
    int item_index_at(Vec2 p) const;
    /// Turns of fire left on `p`, or 0. The frontends draw a flame wherever this
    /// is non-zero; the held-walk stop rule treats it as something underfoot.
    int ember_at(Vec2 p) const;

    /// What to draw at `p`, resolving monster over item over terrain.
    RenderCell render_at(Vec2 p) const;

    /// What putting a piece of gear on (or taking it off) would change.
    ///
    /// Every field is a difference, not a total: +2 attack, -1 defence. Without
    /// this the inventory screen is a list of names and the player is guessing,
    /// which turns finding gear — the most common decision in the game — into a
    /// coin toss.
    EquipPreview equip_preview(int index);

    /// The last few blows, the killer, and what was left unused.
    Postmortem postmortem() const;

    /// Cells the hero could target with `s` right now (for the aiming UI).
    std::vector<Vec2> spell_targets(Spell s) const;
    /// Spells the hero knows and can currently pay for.
    std::vector<Spell> castable_spells() const;

    // --- Serialisation ----------------------------------------------------
    std::string save() const;
    bool load(const std::string& blob);

    // --- Logging ----------------------------------------------------------
    void message(const Text& t, Severity sev = Severity::Info);
    void clear_log() { log_.clear(); }

    /// Hurts a monster: the one path through which damage, boss phases, death,
    /// experience and drops are resolved.
    ///
    /// Public because it is a real engine operation rather than a hero action —
    /// spells, fire and a boss's own breath all go through it — and because a
    /// test that reaches around it would be testing a fight the game never has.
    void damage_monster(Monster& m, int amount, const Text& source);
    /// Hurts the hero, through the warding shirt and on into the death check.
    /// Public for the same reason as `damage_monster`.
    void damage_hero(int amount, const Text& source);

    /// Sets a cell on fire for `turns`, refreshing an ember already there. A
    /// no-op off bare floor, or once the floor already holds too many. Public
    /// because it is a real engine operation — the Огневик, a spell, and a
    /// belt-wide event all light the ground the same way — not a hero action.
    void ignite(Vec2 p, int turns);

    /// Recomputes what the hero can see from where they now stand.
    ///
    /// `perform` already does this after every action, so normal play never
    /// needs it. It exists for the cases that move the hero without an action:
    /// resuming a save, and tests that place the hero directly.
    void refresh_view() { recompute_fov(); }

    /// Direct access for the save/load code and the tests.
    Hero& mutable_hero() { return hero_; }
    Level& mutable_level() { return levels_[static_cast<std::size_t>(depth_)]; }
    Rng& rng() { return rng_; }
    std::vector<std::uint8_t>& mutable_codex_seen() { return codex_seen_; }

private:
    // --- Level management (game.cpp) --------------------------------------
    void ensure_level(int depth);
    /// Lays out the crossroads by hand. Nothing here is random except which
    /// three pieces of gear are on offer.
    void build_lobby(Level& lvl);
    /// Walls off a hall around the floor's guardian, or leaves the floor alone.
    ///
    /// Carving walls into a finished level can cut it in two, so the carve is
    /// checked afterwards and rolled back if it did: a floor with no arena is a
    /// small loss, a floor with an unreachable half is a broken game.
    void build_arena(Level& lvl, int depth);
    /// Carves a small hidden room into the rock on a couple of mid-belt floors,
    /// with Кот Баюн and his charm inside, reached by one closed door. Only ever
    /// carves into solid wall, so it can strand nothing that already existed.
    void build_secret_room(Level& lvl, int depth);
    /// Maybe drops a Домовой, a Жар-птица or a Wolf's Rig half onto a
    /// non-guardian floor. Uses its own random stream so the main dungeon
    /// sequence is identical with or without them.
    void place_wanderers(Level& lvl, int depth);
    /// Marks a fraction of a floor's gear cursed, off a private stream.
    void curse_some_gear(Level& lvl, int depth);
    /// Closes the doors behind the hero, or announces the threshold.
    void update_arena();
    void enter_level(int depth, bool descending);
    void populate(Level& lvl, int depth);
    Vec2 free_spot_near(const Level& lvl, Vec2 origin, int radius) const;
    /// A random unoccupied cell. When `avoid` is a real position, the result is
    /// kept at least `min_distance` steps away from it — that is what stops a
    /// level from spawning monsters on top of the staircase the hero arrives by.
    Vec2 random_free_spot(const Level& lvl, Vec2 avoid = Vec2{-1, -1},
                          int min_distance = 0) const;

    // --- Turn scheduling (game.cpp) ---------------------------------------
    void advance_until_hero_turn();
    void tick_effects(Actor& a, bool is_hero);
    void tick_hero_upkeep();
    /// Burns whatever stands on a lit cell, then counts every ember down.
    void tick_embers();
    void recompute_fov();
    void reap_dead();

    // --- Travel (travel.cpp) ----------------------------------------------
    int open_neighbours(Vec2 p) const;
    std::vector<Vec2> explore_frontier() const;
    bool act_run(Vec2 dir);
    bool act_explore();

    /// One action, exactly as `perform` used to do it. Travel commands are
    /// loops around this, which is what keeps them from inventing new rules.
    bool perform_single(const Action& action);

    // --- Hero actions (actions.cpp) ---------------------------------------
    bool act_move(Vec2 dir);
    bool act_pick_up();
    bool act_use_item(int index);
    bool act_equip(int index);
    int derived_max_hp() const;
    int derived_max_mana() const;
    bool act_drop(int index);
    bool act_descend();
    bool act_ascend();
    bool act_cast(Spell s, Vec2 target);
    bool act_pray();

    void hero_attacks(Monster& m);
    void quaff(const Item& it);
    void read_scroll(const Item& it);
    void grant_xp(int amount);
    void check_level_up();

    // --- Monster turns (ai.cpp) -------------------------------------------
    void monster_turn(std::size_t index);
    void monster_attacks_hero(Monster& m);
    void monster_ranged(Monster& m);
    void monster_summon(Monster& m);
    bool spawn_species(Level& lvl, int species, Vec2 near, int radius);
    /// Runs a boss's own mechanic. Returns true when it consumed the turn.
    bool boss_turn(Monster& m, const Species& sp, bool sees_hero, int distance);
    /// Соловей-Разбойник's whistle on the crossroads. Returns true when it was
    /// his whole turn.
    bool solovey_turn(Monster& m, const Species& sp, bool sees_hero, int distance);
    /// Re-reads a boss's phase from its health, announcing any crossing.
    ///
    /// Called after damage rather than on a timer: a phase is a statement about
    /// how hurt the boss is, and tying it to anything else would let a player
    /// skip a pattern by out-running it.
    void update_boss_phase(Monster& m);
    /// Breathes fire along the line towards `target`. Змей Горыныч's attack.
    void breathe_fire(Monster& m, Vec2 target, int damage, int length);
    /// True while any of Баба-Яга's huts still stands on this floor.
    bool huts_standing() const;
    /// Whether worn gear keeps `e` off the hero entirely, however it arrives.
    bool hero_resists(Effect e) const;

    // --- Shared helpers ---------------------------------------------------
    Monster* monster_at_mut(Vec2 p);
    bool blocked_for_monster(Vec2 p, Vec2 self) const;
    void apply_effect_to_monster(Monster& m, Effect e, int turns, int power);
    Text monster_name(const Monster& m) const;
    int hero_move_cost(Vec2 to) const;

    GameConfig cfg_;
    Rng rng_;
    Hero hero_;
    Identification ident_;
    std::vector<Level> levels_;
    DijkstraMap to_hero_;
    std::vector<LogEntry> log_;
    std::vector<std::uint8_t> codex_seen_;  ///< The bestiary, filled in as creatures are met.
    int depth_{1};
    int turn_{0};
    RunState state_{RunState::Playing};
    bool needs_flow_rebuild_{true};
    bool needle_broken_{false};  ///< Until this is true, Кощей does not stay dead.
    // Achievement bookkeeping, part of the save so a resumed run keeps its
    // progress towards them.
    bool ever_ran_{false};
    bool ever_explored_{false};
    bool ever_hurt_{false};
    int deepest_unhurt_{0};
    /// The first-floor hints, said once per run. Deliberately not part of the
    /// save: a player who reloads has already read them, and a save file is a
    /// description of a dungeon rather than of what its owner has been told.
    bool hinted_start_{false};
    /// A short ring of the blows that landed on the hero, for the ending
    /// screen. Kept out of the save file for the same reason the hints are: it
    /// describes this sitting at the keyboard, not the dungeon.
    std::vector<Postmortem::Blow> blows_;
};

}  // namespace nav
