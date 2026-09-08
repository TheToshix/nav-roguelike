// SPDX-License-Identifier: MIT
//
// Monster behaviour. Each species is described by a set of AI flags in the
// bestiary; this file turns those flags into one decision per turn.
#include <algorithm>
#include <cstring>

#include "nav/fov.hpp"
#include "nav/game.hpp"

namespace nav {
namespace {

/// A monster stops chasing once the hero has been out of sight this long.
constexpr int kSearchPersistence = 8;

}  // namespace

bool Game::spawn_species(Level& lvl, int species, Vec2 near, int radius) {
    const auto& beasts = bestiary();
    const std::size_t si = static_cast<std::size_t>(species);
    if (si >= beasts.size()) return false;

    const Vec2 spot = free_spot_near(lvl, near, radius);
    if (spot.x < 0) return false;

    Monster m{};
    m.species = species;
    m.a.pos = spot;
    m.a.hp = m.a.max_hp = beasts[si].hp;
    m.a.attack = beasts[si].attack;
    m.a.defence = beasts[si].defence;
    m.a.speed = beasts[si].speed;
    m.awake = true;
    m.last_seen = hero_.a.pos;
    m.search_turns = kSearchPersistence;
    lvl.monsters.push_back(m);
    return true;
}

void Game::monster_turn(std::size_t index) {
    Level& lvl = mutable_level();
    if (index >= lvl.monsters.size()) return;
    if (!lvl.monsters[index].a.alive) return;

    const auto& beasts = bestiary();
    const std::size_t si = static_cast<std::size_t>(lvl.monsters[index].species);
    if (si >= beasts.size()) return;
    const Species& sp = beasts[si];

    // The flow field is shared by every monster and only needs rebuilding when
    // the hero moves — running A* per monster per turn was the original design
    // and it dominated the frame time on the lower floors.
    if (needs_flow_rebuild_) {
        to_hero_.build(lvl.map, {hero_.a.pos}, [&lvl](Vec2 p) { return lvl.map.walkable(p); });
        needs_flow_rebuild_ = false;
    }

    Monster& m = lvl.monsters[index];
    const int distance = chebyshev(m.a.pos, hero_.a.pos);
    const bool sees_hero = distance <= sp.sight && !hero_.a.has(Effect::Invisible) &&
                           has_line_of_sight(lvl.map, m.a.pos, hero_.a.pos, sp.sight);

    // --- Waking up --------------------------------------------------------
    if (!m.awake) {
        // Sleeping monsters notice the hero by sight, or by noise when close.
        if (sees_hero || distance <= 2) {
            m.awake = true;
            if (lvl.map.visible(m.a.pos))
                message(format(Text{"{} замечает тебя.", "{} notices you."}, monster_name(m)),
                        Severity::Bad);
        }
        return;
    }

    if (sees_hero) {
        m.last_seen = hero_.a.pos;
        m.search_turns = kSearchPersistence;
    } else if (m.search_turns > 0) {
        --m.search_turns;
    }

    // --- Confusion overrides every other decision -------------------------
    if (m.a.has(Effect::Confusion)) {
        const Vec2 step = m.a.pos + directions8()[static_cast<std::size_t>(rng_.below(8))];
        if (!blocked_for_monster(step, m.a.pos)) m.a.pos = step;
        return;
    }

    // --- Melee, when already adjacent -------------------------------------
    if (distance <= 1 && !(sp.ai & AiStationary)) {
        monster_attacks_hero(m);
        return;
    }
    if (distance <= 1) {  // stationary monsters still swing
        monster_attacks_hero(m);
        return;
    }

    // --- Flee when badly hurt ---------------------------------------------
    if ((sp.ai & AiCoward) && m.a.hp * 3 < m.a.max_hp && sees_hero) {
        const Vec2 away = to_hero_.best_step(
            m.a.pos, [&](Vec2 p) { return !blocked_for_monster(p, m.a.pos); }, /*descend=*/false);
        if (away != m.a.pos) { m.a.pos = away; return; }
    }

    // --- Summon reinforcements --------------------------------------------
    if ((sp.ai & AiSummoner) && sees_hero && distance <= sp.sight) {
        if (m.summon_cooldown > 0) {
            --m.summon_cooldown;
        } else if (rng_.chance(35)) {
            monster_summon(m);
            return;
        }
    }

    // --- Ranged attack ----------------------------------------------------
    if ((sp.ai & AiRanged) && sees_hero && distance >= 2 && distance <= sp.sight) {
        if (rng_.chance(60)) { monster_ranged(m); return; }
    }

    if (sp.ai & AiStationary) return;

    // --- Erratic movement -------------------------------------------------
    if ((sp.ai & AiErratic) && rng_.chance(45)) {
        const Vec2 step = m.a.pos + directions8()[static_cast<std::size_t>(rng_.below(8))];
        if (!blocked_for_monster(step, m.a.pos)) m.a.pos = step;
        return;
    }

    // --- Approach ---------------------------------------------------------
    if (sees_hero || m.search_turns > 0) {
        const Vec2 self = m.a.pos;
        const Vec2 step = to_hero_.best_step(
            self, [&](Vec2 p) { return !blocked_for_monster(p, self); }, /*descend=*/true);
        if (step != self) {
            m.a.pos = step;
            // Monsters push doors open on their way through.
            if (lvl.map.at(step) == Tile::Door) lvl.map.set(step, Tile::OpenDoor);
            return;
        }

        // The flow field is blocked (usually another monster in a corridor);
        // fall back to a direct path that may route the long way round.
        const auto path = find_path(lvl.map, self, m.last_seen,
                                    [&](Vec2 p) { return !blocked_for_monster(p, self); }, 600);
        if (!path.empty() && !blocked_for_monster(path.front(), self)) m.a.pos = path.front();
    }
}

void Game::monster_attacks_hero(Monster& m) {
    const auto& beasts = bestiary();
    const std::size_t si = static_cast<std::size_t>(m.species);
    if (si >= beasts.size()) return;
    const Species& sp = beasts[si];
    const ClassTemplate& tpl = class_info(hero_.cls);
    const Text name = monster_name(m);

    if (rng_.chance(tpl.evasion)) {
        message(format(Text{"Ты уходишь от удара: {}.", "You dodge {}."}, name));
        return;
    }

    const int low = std::max(1, m.a.attack * 3 / 4);
    const int high = std::max(low, m.a.attack * 5 / 4);
    const int damage = std::max(1, rng_.range(low, high) - hero_defence() * 2 / 3);

    message(format(Text{"{} бьёт тебя на {}.", "{} hits you for {}."}, name, num(damage)),
            Severity::Bad);
    damage_hero(damage, name);
    if (!hero_.a.alive) return;

    // On-hit rider (poison, blindness, and so on).
    if (sp.on_hit_chance > 0 && rng_.chance(sp.on_hit_chance)) {
        const bool immune_to_poison =
            sp.on_hit == Effect::Poison && hero_.inv.amulet >= 0 &&
            std::strcmp(gear_table()[static_cast<std::size_t>(
                            hero_.inv.items[static_cast<std::size_t>(hero_.inv.amulet)].subtype)]
                            .key,
                        "ob_yada") == 0;
        if (!immune_to_poison) {
            hero_.a.add_effect(sp.on_hit, sp.on_hit_turns, 2);
            message(Text{"Тебя задело чем-то дурным.", "Something foul takes hold of you."},
                    Severity::Bad);
        }
    }
}

void Game::monster_ranged(Monster& m) {
    const auto& beasts = bestiary();
    const std::size_t si = static_cast<std::size_t>(m.species);
    if (si >= beasts.size()) return;
    const Species& sp = beasts[si];
    const Text name = monster_name(m);

    const int low = std::max(1, m.a.attack / 2);
    const int high = std::max(low, m.a.attack);
    const int damage = std::max(1, rng_.range(low, high) - hero_defence() / 2);

    message(format(Text{"{} бьёт издали на {}.", "{} strikes from afar for {}."},
                   name, num(damage)),
            Severity::Bad);
    damage_hero(damage, name);
    if (!hero_.a.alive) return;

    if (sp.on_hit_chance > 0 && rng_.chance(sp.on_hit_chance / 2)) {
        hero_.a.add_effect(sp.on_hit, sp.on_hit_turns, 2);
        message(Text{"Тебя задело чем-то дурным.", "Something foul takes hold of you."},
                Severity::Bad);
    }
}

void Game::monster_summon(Monster& m) {
    const auto& beasts = bestiary();
    std::vector<int> weights(beasts.size(), 0);
    for (std::size_t i = 0; i < beasts.size(); ++i) {
        // A summoner never calls a boss, and never calls something tougher
        // than itself — otherwise Баба-Яга can fill her room with Кощеи.
        if (beasts[i].ai & AiBoss) continue;
        if (beasts[i].xp > beasts[static_cast<std::size_t>(m.species)].xp) continue;
        weights[i] = spawn_weight(beasts[i], depth_);
    }

    const int pick = rng_.weighted(weights);
    if (pick < 0) return;

    const int wanted = 1 + rng_.below(2);
    int spawned = 0;
    for (int i = 0; i < wanted; ++i)
        if (spawn_species(mutable_level(), pick, m.a.pos, 3)) ++spawned;

    m.summon_cooldown = 6 + rng_.below(6);
    if (spawned > 0 && map().visible(m.a.pos))
        message(format(Text{"{} зовёт подмогу!", "{} calls for help!"}, monster_name(m)),
                Severity::Critical);
}

}  // namespace nav
