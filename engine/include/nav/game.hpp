// SPDX-License-Identifier: MIT
#pragma once
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

struct GameConfig {
    std::uint64_t seed{0};
    std::string seed_text;          ///< What the player typed, kept for display.
    HeroClass hero_class{HeroClass::Vityaz};
    int map_width{72};
    int map_height{34};
};

/// One dungeon floor, kept in memory so ascending returns to the level as it
/// was left — corpses, dropped loot and explored tiles included.
struct Level {
    Map map;
    std::vector<Monster> monsters;
    std::vector<Item> items;
    Vec2 entrance{-1, -1};
    Vec2 exit{-1, -1};
    bool generated{false};
    bool boss_slain{false};
};

/// Everything the frontends need to draw one cell.
struct RenderCell {
    char glyph{' '};
    const char* color{"#888"};
    bool visible{false};
    bool explored{false};
};

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
    const Hero& hero() const { return hero_; }
    const std::vector<Monster>& monsters() const { return level().monsters; }
    const std::vector<Item>& floor_items() const { return level().items; }
    const std::vector<LogEntry>& log() const { return log_; }
    const Identification& identification() const { return ident_; }
    const GameConfig& config() const { return cfg_; }

    int depth() const { return depth_; }
    int turn() const { return turn_; }
    RunState state() const { return state_; }
    /// Final score: gold, depth, kills and hero level rolled into one number.
    int score() const;

    /// Derived hero statistics, after gear and status effects.
    int hero_attack() const;
    int hero_defence() const;
    int hero_sight() const;
    int hero_speed() const;

    /// The belt of the dungeon the hero is currently in.
    Zone zone() const { return zone_for_depth(depth_); }
    /// True while Кощей can still rise again — that is, the needle is unbroken.
    bool needle_intact() const { return !needle_broken_; }

    /// The monster standing on `p`, or nullptr.
    const Monster* monster_at(Vec2 p) const;
    /// Index of the topmost floor item at `p`, or -1.
    int item_index_at(Vec2 p) const;

    /// What to draw at `p`, resolving monster over item over terrain.
    RenderCell render_at(Vec2 p) const;

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

private:
    // --- Level management (game.cpp) --------------------------------------
    void ensure_level(int depth);
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
    void recompute_fov();
    void reap_dead();

    // --- Hero actions (actions.cpp) ---------------------------------------
    bool act_move(Vec2 dir);
    bool act_pick_up();
    bool act_use_item(int index);
    bool act_equip(int index);
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
    /// True while any of Баба-Яга's huts still stands on this floor.
    bool huts_standing() const;

    // --- Shared helpers ---------------------------------------------------
    Monster* monster_at_mut(Vec2 p);
    bool blocked_for_monster(Vec2 p, Vec2 self) const;
    void damage_monster(Monster& m, int amount, const Text& source);
    void damage_hero(int amount, const Text& source);
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
    int depth_{1};
    int turn_{0};
    RunState state_{RunState::Playing};
    bool needs_flow_rebuild_{true};
    bool needle_broken_{false};  ///< Until this is true, Кощей does not stay dead.
};

}  // namespace nav
