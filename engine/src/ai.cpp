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

/// Whether the hero's charms and worn gear keep `e` out entirely.
///
/// One place for the question, because there are two ways to be poisoned — bitten
/// and spat at — and for a while the amulet only stopped the first. A charm that
/// works against a bite and not against the same venom thrown from across the
/// room is not a rule, it is a bug wearing one (NAV-010).
bool Game::hero_resists(Effect e) const {
    // The old antivenom charm predates the gear-power flags and is still worn.
    if (e == Effect::Poison && hero_.inv.amulet >= 0) {
        const Item& am = hero_.inv.items[static_cast<std::size_t>(hero_.inv.amulet)];
        if (am.is_gear() &&
            std::strcmp(gear_table()[static_cast<std::size_t>(am.subtype)].key, "ob_yada") == 0)
            return true;
    }
    if (e == Effect::Poison && hero_has(GpNoPoison)) return true;
    if (e == Effect::Burn && hero_has(GpNoBurn)) return true;
    if ((e == Effect::Confusion || e == Effect::Blind) && hero_set() == GearSet::Oberezhny)
        return true;
    return false;
}

bool Game::huts_standing() const {
    const int hut = species_index("izbushka");
    if (hut < 0) return false;
    for (const auto& m : level().monsters)
        if (m.a.alive && m.species == hut) return true;
    return false;
}

/// Re-reads a boss's phase from its health and announces any crossing.
///
/// Thresholds are even fractions of the health bar rather than hand-picked
/// numbers, so a boss whose health is later rebalanced does not silently lose a
/// phase. The phase only ever rises: healing a boss back over a threshold must
/// not hand the player a pattern they have already beaten.
void Game::update_boss_phase(Monster& m) {
    const auto& beasts = bestiary();
    const std::size_t si = static_cast<std::size_t>(m.species);
    if (si >= beasts.size()) return;
    const Species& sp = beasts[si];
    if (sp.phases <= 1 || m.a.max_hp <= 0) return;

    // With three phases the crossings are at two thirds and one third; with
    // two, at a half.
    const int left = m.a.hp * sp.phases;
    int want = sp.phases - (left - 1) / std::max(1, m.a.max_hp);
    want = std::clamp(want, 1, sp.phases);
    if (want <= m.phase) return;

    while (m.phase < want) {
        ++m.phase;
        const Text line = boss_phase_line(sp.key, m.phase);
        if (!line.ru.empty()) message(line, Severity::Critical);
    }

    // A new pattern starts from a clean slate: a telegraph half-charged under
    // the old rules would fire under the new ones and read as a cheat.
    m.charge = 0;
    m.summon_cooldown = 0;

    // Баба-Яга calls a hut back when she is nearly done. It is the one phase
    // change in the game that restores a boss's defence, and it is announced a
    // line earlier, so the player knows to knock it down again rather than
    // wondering why their damage stopped landing.
    if (std::strcmp(sp.key, "babayaga") == 0 && m.phase == 3) {
        const int hut = species_index("izbushka");
        if (hut >= 0) spawn_species(mutable_level(), hut, m.a.pos, 3);
    }
}

/// A jet of fire along the line towards `target`, up to `length` cells.
///
/// Everything in the way is burned, the hero included — Змей Горыныч does not
/// aim around his own kin, which is what makes standing behind his summons a
/// real tactic rather than a mistake.
void Game::breathe_fire(Monster& m, Vec2 target, int damage, int length) {
    const std::vector<Vec2> path = line(m.a.pos, target);
    int reached = 0;
    for (const Vec2 p : path) {
        if (++reached > length) break;
        if (blocks_sight(map().at(p))) break;
        if (p == hero_.a.pos) {
            damage_hero(damage, Text{"пламя Горыныча", "Gorynych's fire"});
            if (hero_.a.alive && !hero_has(GpNoBurn)) hero_.a.add_effect(Effect::Burn, 4, 3);
        } else if (Monster* other = monster_at_mut(p)) {
            if (other != &m) damage_monster(*other, damage / 2, Text{"пламя", "the fire"});
        }
    }
    message(Text{"Горыныч выдыхает пламя.", "Gorynych breathes fire."}, Severity::Critical);
}

/// A boss's own mechanic, run before the ordinary behaviour.
///
/// Each of the three fights is built from the story it comes from rather than
/// from a bigger pile of health, because a boss that is only a large monster is
/// not a boss — it is a wall.
bool Game::boss_turn(Monster& m, const Species& sp, bool sees_hero, int distance) {
    if (!(sp.ai & AiBoss)) return false;

    // --- Вий: "поднимите мне веки" -----------------------------------------
    //
    // Three turns with his eyelids down, during which he is nearly blind and
    // takes the punishment; then he opens them, and anything he can see is
    // struck hard and left blind. The counter-play is to break line of sight on
    // the turn he is telegraphed to open — which is why the warning arrives one
    // turn early.
    if (std::strcmp(sp.key, "viy") == 0) {
        // The eyelid cycle shortens as he tires of waiting: four turns, then
        // three, then two. The counter-play never changes — get out of sight —
        // but the room to hit him between gazes keeps shrinking.
        const int cycle = m.phase >= 3 ? 2 : (m.phase == 2 ? 3 : 4);
        const int warn = cycle - 1;
        ++m.charge;
        if (m.charge == warn && map().visible(m.a.pos))
            message(Text{"Вий заносит руку к векам. Уйди с глаз!",
                         "Viy raises a hand towards his eyelids. Get out of sight!"},
                    Severity::Critical);

        if (m.charge >= cycle) {
            m.charge = 0;
            if (sees_hero && distance <= sp.sight) {
                message(Text{"«Поднимите мне веки!» — взгляд Вия находит тебя.",
                             "\"Lift up my eyelids!\" — Viy's gaze finds you."},
                        Severity::Critical);
                damage_hero(12 + depth_, Text{"взгляд Вия", "Viy's gaze"});
                // The warding circle is exactly the answer to a gaze: it does
                // not stop the blow, it stops the blindness that follows.
                if (hero_.a.alive && hero_set() != GearSet::Oberezhny)
                    hero_.a.add_effect(Effect::Blind, 8, 1);
            } else {
                // Deliberately not gated on seeing him: a player who has just
                // ducked behind a wall has played the fight correctly and
                // deserves to be told it worked.
                message(Text{"Где-то рядом Вий ревёт впустую — взгляд не нашёл тебя.",
                             "Somewhere close Viy roars at nothing — the gaze does not find you."},
                        Severity::Good);
            }
            return true;  // opening his eyes is the whole turn
        }
        return false;  // otherwise he closes in like anything else
    }

    // --- Баба-Яга: пока стоит изба ------------------------------------------
    //
    // She keeps her distance and calls for help while her huts stand; the fight
    // is about knocking those down first.
    // From her second phase she rides the mortar: faster than the hero, and
    // unwilling to stand still for a trade.
    if (std::strcmp(sp.key, "babayaga") == 0 && m.phase >= 2) m.a.speed = 145;

    if (std::strcmp(sp.key, "babayaga") == 0 && huts_standing()) {
        if (m.summon_cooldown > 0) --m.summon_cooldown;
        else if (sees_hero && rng_.chance(50)) { monster_summon(m); return true; }

        // While protected she backs away rather than trading blows.
        if (sees_hero && distance <= 3) {
            const Vec2 self = m.a.pos;
            const Vec2 away = to_hero_.best_step(
                self, [&](Vec2 p) { return !blocked_for_monster(p, self); }, /*descend=*/false);
            if (away != self) { m.a.pos = away; return true; }
        }
    }

    // --- Кощей: сначала бьёт, потом тянет, потом зовёт -----------------------
    if (std::strcmp(sp.key, "koschei") == 0) {
        // Phase two turns the fight into a race: every blow he lands closes his
        // own wounds, so out-healing him stops working and out-running his
        // health bar becomes the only line.
        if (m.phase == 2 && sees_hero && distance <= 1) {
            const int drain = 6 + depth_ / 2;
            damage_hero(drain, Text{"хватка Кощея", "Koschei's grip"});
            m.a.heal(drain);
            message(Text{"Кощей тянет из тебя жизнь, и его раны затягиваются.",
                         "Koschei draws the life out of you, and his wounds close."},
                    Severity::Bad);
            return true;
        }
        if (m.phase >= 3) {
            if (m.summon_cooldown > 0) --m.summon_cooldown;
            else if (sees_hero && rng_.chance(45)) { monster_summon(m); m.summon_cooldown = 4; return true; }
        }
    }

    // --- Змей Горыныч: по голове за фазу -------------------------------------
    //
    // The phases are literal here — a head falls at each threshold — so the
    // fight gets faster and hotter exactly as it gets shorter. Fewer heads,
    // less reason to pace himself.
    if (std::strcmp(sp.key, "gorynych") == 0) {
        const int heads = 4 - m.phase;                  // 3, then 2, then 1
        const int between = m.phase >= 3 ? 1 : (m.phase == 2 ? 2 : 3);
        ++m.charge;
        if (sees_hero && m.charge >= between && distance <= sp.sight) {
            m.charge = 0;
            breathe_fire(m, hero_.a.pos, 8 + depth_ / 2 + heads * 2, sp.sight);
            return true;
        }
        // On one head he stops circling and simply comes at you.
        if (m.phase >= 3) m.a.speed = 150;
        return false;
    }

    // --- Мара: морок, а на второй фазе ещё и двойники ------------------------
    if (std::strcmp(sp.key, "mara") == 0) {
        if (sees_hero && distance <= sp.sight && rng_.chance(m.phase >= 2 ? 55 : 35)) {
            message(Text{"Мара шепчет, и стены начинают двоиться.",
                         "Mara whispers, and the walls begin to double."},
                    Severity::Bad);
            // The warding circle is what a player wears when they have met her
            // once and did not enjoy it.
            if (hero_set() != GearSet::Oberezhny)
                hero_.a.add_effect(Effect::Confusion, 4 + m.phase, 1);
            return true;
        }
        // She will not be cornered: adjacency puts her somewhere else.
        if (distance <= 1) {
            const Vec2 spot = free_spot_near(level(), hero_.a.pos, 6);
            if (spot.x >= 0) { m.a.pos = spot; return true; }
        }
        if (m.phase >= 2 && m.summon_cooldown <= 0 && sees_hero) {
            m.summon_cooldown = 6;
            monster_summon(m);
            return true;
        }
        if (m.summon_cooldown > 0) --m.summon_cooldown;
        return false;
    }

    // --- Водяной: вода — его дом ---------------------------------------------
    if (std::strcmp(sp.key, "vodyanoy") == 0) {
        if (map().at(m.a.pos) == Tile::Water) m.a.heal(m.phase >= 2 ? 5 : 3);

        // Phase two floods the room he is standing in, which turns his healing
        // from a quirk into the thing the player has to fight.
        if (m.phase >= 2 && rng_.chance(30)) {
            Map& map_ref = mutable_level().map;
            int flooded = 0;
            for (int dy = -2; dy <= 2 && flooded < 4; ++dy)
                for (int dx = -2; dx <= 2 && flooded < 4; ++dx) {
                    const Vec2 p{m.a.pos.x + dx, m.a.pos.y + dy};
                    if (!map_ref.in_bounds(p) || map_ref.at(p) != Tile::Floor) continue;
                    if (p == hero_.a.pos) continue;
                    map_ref.set(p, Tile::Water);
                    ++flooded;
                }
            if (flooded > 0) {
                message(Text{"Вода прибывает — пол уходит под воду.",
                             "The water rises; the floor goes under."},
                        Severity::Bad);
                return true;
            }
        }
        if (m.summon_cooldown > 0) --m.summon_cooldown;
        else if (sees_hero && rng_.chance(25)) { m.summon_cooldown = 5; monster_summon(m); return true; }
        return false;
    }

    // --- Морозко: «тепло ли тебе?» -------------------------------------------
    if (std::strcmp(sp.key, "morozko") == 0) {
        ++m.charge;
        const int cycle = m.phase >= 2 ? 3 : 4;
        if (m.charge == cycle - 1 && map().visible(m.a.pos))
            message(Text{"Морозко набирает воздух: «Тепло ли тебе?»",
                         "Morozko draws breath: \"Are you warm?\""},
                    Severity::Critical);
        if (m.charge >= cycle) {
            m.charge = 0;
            if (sees_hero && distance <= sp.sight) {
                damage_hero(7 + depth_ / 2, Text{"стужа Морозко", "Morozko's cold"});
                // Standing still is the mistake; the freeze is short enough to
                // be survivable and long enough to be frightening.
                if (hero_.a.alive) hero_.a.add_effect(Effect::Freeze, m.phase >= 2 ? 2 : 1, 1);
                message(Text{"Стужа хватает — ноги не идут.",
                             "The cold takes hold; your legs will not move."},
                        Severity::Critical);
            }
            return true;
        }
        if (m.phase >= 2 && sees_hero && rng_.chance(25))
            hero_.a.add_effect(Effect::Slow, 3, 1);
        return false;
    }

    // --- Огненный Полоз: ходит сквозь камень ---------------------------------
    if (std::strcmp(sp.key, "polozh") == 0) {
        if (sees_hero && distance <= 1 && rng_.chance(40) && !hero_has(GpNoBurn))
            hero_.a.add_effect(Effect::Burn, 4, 3);

        // From phase two he burrows: distance stops being safety, which is the
        // whole answer to a player who has learned to kite the first half.
        if (m.phase >= 2 && distance > 2 && rng_.chance(30)) {
            const Vec2 spot = free_spot_near(level(), hero_.a.pos, 2);
            if (spot.x >= 0) {
                m.a.pos = spot;
                message(Text{"Камень трескается — Полоз выходит рядом с тобой.",
                             "The stone cracks, and the Poloz comes up beside you."},
                        Severity::Critical);
                return true;
            }
        }
        return false;
    }

    return false;
}

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

    // --- A boss's own mechanic comes before the ordinary behaviour --------
    if (boss_turn(m, sp, sees_hero, distance)) return;

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
    const int before = hero_.a.hp;
    damage_hero(damage, name);
    if (!hero_.a.alive) return;

    // Бахтерец returns a third of whatever actually got through — nothing when
    // the warding shirt ate the blow, which is how the two pieces stay
    // distinguishable rather than stacking into one blur.
    const int taken = before - hero_.a.hp;
    if (taken > 0 && hero_has(GpThorns))
        damage_monster(m, std::max(1, taken / 3), Text{"шипы бахтерца", "the cuirass's scales"});

    // On-hit rider (poison, blindness, and so on).
    if (sp.on_hit_chance > 0 && rng_.chance(sp.on_hit_chance)) {
        if (!hero_resists(sp.on_hit)) {
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

    if (sp.on_hit_chance > 0 && rng_.chance(sp.on_hit_chance / 2) && !hero_resists(sp.on_hit)) {
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
