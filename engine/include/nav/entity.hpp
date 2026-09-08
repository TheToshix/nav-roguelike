// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <vector>

#include "nav/item.hpp"
#include "nav/text.hpp"
#include "nav/types.hpp"

namespace nav {

/// Energy accumulated per turn by a creature of normal speed. A creature acts
/// once its energy reaches this threshold, so speed 150 acts 1.5x as often.
inline constexpr int kEnergyPerTurn = 100;

/// Everything that can fight, move and carry status effects.
struct Actor {
    Vec2 pos{0, 0};
    int hp{1};
    int max_hp{1};
    int attack{1};
    int defence{0};
    int speed{100};
    int energy{0};
    bool alive{true};
    std::vector<ActiveEffect> effects;

    bool has(Effect e) const {
        return std::any_of(effects.begin(), effects.end(),
                           [e](const ActiveEffect& a) { return a.kind == e && a.turns > 0; });
    }

    int effect_power(Effect e) const {
        int best = 0;
        for (const auto& a : effects)
            if (a.kind == e && a.turns > 0) best = std::max(best, a.power);
        return best;
    }

    /// Applies an effect, keeping the stronger of the two when one is already
    /// running and extending the duration otherwise.
    void add_effect(Effect e, int turns, int power = 1) {
        for (auto& a : effects) {
            if (a.kind != e) continue;
            a.turns = std::max(a.turns, turns);
            a.power = std::max(a.power, power);
            return;
        }
        effects.push_back(ActiveEffect{e, turns, power});
    }

    void clear_effect(Effect e) {
        effects.erase(std::remove_if(effects.begin(), effects.end(),
                                     [e](const ActiveEffect& a) { return a.kind == e; }),
                      effects.end());
    }

    /// Speed after haste/slow. Never drops below 25 so nothing can freeze the
    /// turn scheduler by having zero energy gain.
    int effective_speed() const {
        int s = speed;
        if (has(Effect::Haste)) s = s * 3 / 2;
        if (has(Effect::Slow)) s = s * 2 / 3;
        return std::max(25, s);
    }

    /// A frozen creature loses its turn entirely.
    bool can_act() const { return alive && !has(Effect::Freeze); }

    void damage(int amount) {
        hp -= amount;
        if (hp <= 0) { hp = 0; alive = false; }
    }

    void heal(int amount) { hp = std::min(max_hp, hp + amount); }
};

/// AI archetypes. Combined as flags because several monsters mix behaviours
/// (a caster that also flees, for instance).
enum AiFlag : std::uint32_t {
    AiMelee     = 1u << 0,
    AiRanged    = 1u << 1,  ///< Attacks from a distance while in line of sight.
    AiErratic   = 1u << 2,  ///< Moves randomly half the time.
    AiCoward    = 1u << 3,  ///< Flees below a third of its health.
    AiSummoner  = 1u << 4,  ///< Calls reinforcements.
    AiStationary= 1u << 5,  ///< Never leaves its cell.
    AiBoss      = 1u << 6,  ///< Never sleeps, immune to instant effects.
};

/// A monster species — one row of the bestiary.
struct Species {
    const char* key;
    Text name;
    char glyph;
    const char* color;      ///< CSS colour used by the web renderer.
    int hp;
    int attack;
    int defence;
    int speed;
    int sight;
    int xp;
    int min_depth;
    int max_depth;
    int weight;             ///< Spawn weight; 0 for bosses (placed explicitly).
    std::uint32_t ai;
    Effect on_hit{Effect::Poison};
    int on_hit_chance{0};   ///< Percent chance the melee hit applies `on_hit`.
    int on_hit_turns{0};
    Text description;
};

/// A living monster on the current floor.
struct Monster {
    Actor a;
    int species{0};
    bool awake{false};
    Vec2 last_seen{-1, -1};  ///< Where the hero was last spotted; drives searching.
    int search_turns{0};
    int summon_cooldown{0};
};

/// One inventory slot's worth of goods, plus what is currently worn.
struct Inventory {
    std::vector<Item> items;
    int weapon{-1};  ///< Indices into `items`; -1 when the slot is empty.
    int armor{-1};
    int amulet{-1};

    static constexpr std::size_t kCapacity = 20;
    bool full() const { return items.size() >= kCapacity; }

    /// Adds an item, merging into an existing stack when possible.
    /// Returns false only when the pack is full.
    bool add(const Item& it) {
        if (it.stackable()) {
            for (auto& have : items) {
                if (!have.same_as(it)) continue;
                have.count += it.count;
                return true;
            }
        }
        if (full()) return false;
        items.push_back(it);
        items.back().pos = {-1, -1};
        return true;
    }

    /// Removes `count` units at `index`, fixing up the equipment indices when
    /// the removal shifts the vector. Returns the removed item.
    Item take(int index, int count = 1) {
        Item out{};
        if (index < 0 || index >= static_cast<int>(items.size())) return out;
        Item& src = items[static_cast<std::size_t>(index)];
        out = src;
        out.count = std::min(count, src.count);
        src.count -= out.count;
        if (src.count > 0) return out;

        items.erase(items.begin() + index);
        auto fix = [index](int& slot) {
            if (slot == index) slot = -1;
            else if (slot > index) --slot;
        };
        fix(weapon);
        fix(armor);
        fix(amulet);
        return out;
    }

    bool is_equipped(int index) const {
        return index >= 0 && (index == weapon || index == armor || index == amulet);
    }

    int& slot_ref(Slot s) {
        static int none = -1;
        switch (s) {
            case Slot::Weapon: return weapon;
            case Slot::Armor:  return armor;
            case Slot::Amulet: return amulet;
            default: none = -1; return none;
        }
    }
};

/// The hero: an Actor plus everything only the player has.
struct Hero {
    Actor a;
    HeroClass cls{HeroClass::Vityaz};
    int mana{0};
    int max_mana{0};
    int level{1};
    int xp{0};
    int gold{0};
    int sight{8};
    int nutrition{900};       ///< Counts down each turn; 0 starts starvation.
    int kills{0};
    int deepest{1};
    Inventory inv;
    std::vector<std::uint8_t> spells;  ///< Indexed by Spell; 1 when known.

    bool knows(Spell s) const {
        const std::size_t i = static_cast<std::size_t>(s);
        return i < spells.size() && spells[i] != 0;
    }
    void learn(Spell s) {
        const std::size_t i = static_cast<std::size_t>(s);
        if (spells.size() <= i) spells.resize(i + 1, 0);
        spells[i] = 1;
    }
};

/// The bestiary table (defined in data.cpp).
const std::vector<Species>& bestiary();
/// Index of a species by key, or -1.
int species_index(const char* key);

}  // namespace nav
