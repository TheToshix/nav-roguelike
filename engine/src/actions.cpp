// SPDX-License-Identifier: MIT
//
// Everything the hero can do on their turn, plus the damage model both sides
// of a fight share.
#include <algorithm>
#include <cstring>
#include <functional>

#include "nav/fov.hpp"
#include "nav/game.hpp"

namespace nav {
namespace {

/// Damage roll: ±25% around the attacker's rating, with armour subtracting two
/// thirds of its value. Armour therefore always helps but never makes a
/// creature untouchable — the result is clamped to at least 1.
int roll_damage(Rng& rng, int attack, int defence) {
    const int low = std::max(1, attack * 3 / 4);
    const int high = std::max(low, attack * 5 / 4);
    return std::max(1, rng.range(low, high) - defence * 2 / 3);
}

bool has_amulet(const Hero& h, const char* key) {
    const auto& inv = h.inv;
    if (inv.amulet < 0 || inv.amulet >= static_cast<int>(inv.items.size())) return false;
    const Item& am = inv.items[static_cast<std::size_t>(inv.amulet)];
    const auto& gear = gear_table();
    const std::size_t i = static_cast<std::size_t>(am.subtype);
    return i < gear.size() && std::strcmp(gear[i].key, key) == 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Damage plumbing
// ---------------------------------------------------------------------------

void Game::damage_hero(int amount, const Text& source) {
    if (amount <= 0 || !hero_.a.alive) return;

    // Сорочка-неуязвимка turns one blow aside per floor. One is the whole
    // design: a shirt that stopped everything would end the game's tension,
    // and a shirt that stopped a tenth of everything would be a number.
    if (hero_has(GpWard) && hero_.ward_ready) {
        hero_.ward_ready = 0;
        message(Text{"Сорочка-неуязвимка твердеет — удар уходит мимо.",
                     "The warding shirt hardens, and the blow goes wide."},
                Severity::Good);
        return;
    }

    const int before = hero_.a.hp;
    hero_.a.damage(amount);

    // Remember the blow. A run that ends badly ends in a few seconds, and the
    // player cannot read a scrolling log while it happens — so the ending
    // screen reads it back to them afterwards.
    blows_.push_back(Postmortem::Blow{source, before - hero_.a.hp, hero_.a.hp, turn_});
    if (blows_.size() > kPostmortemBlows) blows_.erase(blows_.begin());

    if (hero_.a.alive) return;

    // Перо Жар-птицы: one death undone, then the feather is spent. It acts on
    // its own — there is no key for it — which is the whole point of a thing you
    // cannot decide to waste.
    for (std::size_t i = 0; i < hero_.inv.items.size(); ++i) {
        if (hero_.inv.items[i].kind != ItemKind::Feather) continue;
        hero_.inv.take(static_cast<int>(i), 1);
        hero_.a.alive = true;
        hero_.a.hp = std::max(1, hero_.a.max_hp * 2 / 5);
        hero_.a.clear_effect(Effect::Poison);
        hero_.a.clear_effect(Effect::Burn);
        hero_.a.clear_effect(Effect::Sleep);
        hero_.a.clear_effect(Effect::Freeze);
        message(Text{"Перо Жар-птицы вспыхивает и рассыпается — ты снова дышишь.",
                     "The firebird's feather flares and crumbles — and you draw breath again."},
                Severity::Critical);
        return;
    }

    state_ = RunState::Dead;
    message(format(Text{"Ты погиб. Причина: {}.", "You died. Cause: {}."}, source),
            Severity::Critical);
    message(format(Text{"Глубина {}, ходов {}, очков {}.",
                        "Depth {}, turns {}, score {}."},
                   num(hero_.deepest), num(turn_), num(score())),
            Severity::System);
}

void Game::damage_monster(Monster& m, int amount, const Text& source) {
    if (amount <= 0 || !m.a.alive) return;

    const auto& beasts_all = bestiary();
    const std::size_t idx = static_cast<std::size_t>(m.species);
    const char* key = idx < beasts_all.size() ? beasts_all[idx].key : "";

    // Вий with his eyelids down cannot see the blow coming: the window between
    // gazes is when the fight is actually winnable.
    if (std::strcmp(key, "viy") == 0 && m.charge < 3) amount = amount * 3 / 2;

    // Баба-Яга is shielded while her huts stand. Knocking them down is the fight.
    if (std::strcmp(key, "babayaga") == 0 && huts_standing()) {
        amount = std::max(1, amount / 5);
        if (rng_.chance(25))
            message(Text{"Удар вязнет — изба держит хозяйку.",
                         "The blow goes nowhere — the hut is holding her."},
                    Severity::Bad);
    }

    m.a.damage(amount);
    m.awake = true;
    update_boss_phase(m);

    // A skittish thing spooks at the first touch: it is gone, and there is no
    // corpse, no experience and no feather — the feather is the reward for
    // catching it without hurting it.
    if (m.a.alive && idx < beasts_all.size() && (beasts_all[idx].ai & AiSkittish)) {
        m.a.alive = false;
        if (map().visible(m.a.pos))
            message(format(Text{"{} вспыхивает и уходит ввысь — ты её спугнул.",
                                "{} flares and is gone into the dark — you startled it."},
                           monster_name(m)),
                    Severity::Bad);
        return;
    }

    // A struck Домовой stops being a bystander. From here it is an ordinary
    // brute; `m.revives` is the latch the AI reads, reusing the field Кощей uses
    // for the same "this monster no longer behaves the default way" purpose.
    if (m.a.alive && idx < beasts_all.size() && (beasts_all[idx].ai & AiNeutral) &&
        m.revives == 0) {
        m.revives = 1;
        m.charge = 0;
        if (map().visible(m.a.pos))
            message(format(Text{"{} оскорблён и бросается на тебя.",
                                "{} takes offence and turns on you."},
                           monster_name(m)),
                    Severity::Bad);
    }

    // Кощей's death is not in his body. Until the needle is broken he simply
    // gets back up, and the first time he does the floor gives up the needle's
    // location — a mechanic the player cannot guess is a mechanic that is only
    // unfair.
    if (!m.a.alive && std::strcmp(key, "koschei") == 0 && !needle_broken_) {
        m.a.alive = true;
        m.a.hp = std::max(1, m.a.max_hp * 3 / 5);
        ++m.revives;
        message(Text{"Кощей поднимается. Смерть его не здесь.",
                     "Koschei rises again. His death is not here."},
                Severity::Critical);
        if (m.revives == 1) {
            message(Text{"Смерть его — на конце иглы. Игла — на этом этаже.",
                         "His death is on a needle's point. The needle is on this floor."},
                    Severity::System);
            mutable_level().map.reveal_all();
        }
        return;
    }

    if (m.a.alive) return;

    const Text name = monster_name(m);
    message(format(Text{"{} падает замертво.", "{} falls dead."}, name), Severity::Good);
    ++hero_.kills;

    if (std::strcmp(key, "izbushka") == 0) {
        // `huts_standing()` still counts this one until the corpse is reaped,
        // so look for a second hut rather than trusting the count.
        int remaining = 0;
        for (const auto& other : level().monsters)
            if (&other != &m && other.a.alive && other.species == m.species) ++remaining;
        if (remaining == 0)
            message(Text{"Изба оседает. Баба-Яга остаётся без защиты.",
                         "The hut collapses. Baba Yaga stands unprotected."},
                    Severity::Good);
    }

    const auto& beasts = bestiary();
    const std::size_t si = static_cast<std::size_t>(m.species);
    if (si < beasts.size()) {
        grant_xp(beasts[si].xp);
        if (beasts[si].ai & AiBoss) {
            mutable_level().boss_slain = true;
            message(format(Text{"Страж этажа повержен. ({})", "The floor's guardian is slain. ({})"},
                           source),
                    Severity::System);
            if (std::strcmp(beasts[si].key, "koschei") == 0)
                message(Text{"Игла сломана. Кощей рассыпается прахом.",
                             "The needle snaps, and Koschei crumbles to dust."},
                        Severity::Critical);

            // The run ends when the dungeon's own last guardian falls — asked
            // for by depth, not named. Naming one was true for exactly as long
            // as he was the last thing down there (NAV-013).
            const char* last = boss_for_depth(kMaxDepth);
            if (last && std::strcmp(beasts[si].key, last) == 0) {
                state_ = RunState::Ascended;
                message(Text{"Последняя голова падает. Навь отпускает тебя.",
                             "The last head falls, and Nav lets you go."},
                        Severity::Critical);
                message(format(Text{"Победа! Очков: {}.", "Victory! Score: {}."}, num(score())),
                        Severity::System);
            }
        }
    }

    // Bosses and a fraction of ordinary monsters leave something behind.
    const bool boss = si < beasts.size() && (beasts[si].ai & AiBoss);
    if (boss || rng_.chance(22)) {
        Item drop{};
        if (boss) {
            const auto& gear = gear_table();
            std::vector<int> weights(gear.size(), 0);
            for (std::size_t i = 0; i < gear.size(); ++i)
                weights[i] = gear[i].min_depth <= depth_ + 2 ? gear[i].weight : 0;
            const int pick = rng_.weighted(weights);
            if (pick >= 0) {
                drop.kind = gear[static_cast<std::size_t>(pick)].kind;
                drop.subtype = pick;
                drop.power = gear[static_cast<std::size_t>(pick)].power;
                drop.enchant = 1 + rng_.below(2);
                drop.identified = true;
            }
        } else if (rng_.chance(55)) {
            drop.kind = ItemKind::Gold;
            drop.count = 5 + rng_.below(10 + depth_ * 5);
            drop.identified = true;
        } else {
            drop.kind = ItemKind::Potion;
            drop.subtype = rng_.below(static_cast<int>(PotionKind::Count));
        }
        drop.pos = m.a.pos;
        if (map().walkable(drop.pos)) mutable_level().items.push_back(drop);
    }
}

void Game::apply_effect_to_monster(Monster& m, Effect e, int turns, int power) {
    const auto& beasts = bestiary();
    const std::size_t si = static_cast<std::size_t>(m.species);
    // Bosses shrug off the effects that would otherwise remove them from the
    // fight outright; damage-over-time still applies.
    if (si < beasts.size() && (beasts[si].ai & AiBoss)) {
        if (e == Effect::Freeze || e == Effect::Confusion) turns = std::min(turns, 2);
    }
    m.a.add_effect(e, turns, power);
}

// ---------------------------------------------------------------------------
// Movement and melee
// ---------------------------------------------------------------------------

int Game::hero_move_cost(Vec2 to) const {
    // Лапти-скороходы and the rest of the traveller's kit make wading free.
    // In the flooded belt that is not a small bonus, which is the point: a set
    // should change where you can afford to walk, not add two to a number.
    if (map().at(to) != Tile::Water) return kEnergyPerTurn;
    return hero_set() == GearSet::Hodovoy ? kEnergyPerTurn : kEnergyPerTurn * 3 / 2;
}

bool Game::act_move(Vec2 dir) {
    if (dir.x == 0 && dir.y == 0) return false;

    // A confused hero mostly staggers in a random direction.
    if (hero_.a.has(Effect::Confusion) && rng_.chance(60)) {
        dir = directions8()[static_cast<std::size_t>(rng_.below(8))];
        message(Text{"Тебя ведёт в сторону.", "You stagger sideways."}, Severity::Bad);
    }

    const Vec2 target = hero_.a.pos + dir;

    if (Monster* m = monster_at_mut(target)) {
        hero_attacks(*m);
        hero_.a.energy -= kEnergyPerTurn;
        return true;
    }

    // The guardian's hall holds both sides of the fight. Refused before the
    // door is touched, so the hero cannot open their way out and then be told
    // no — a refusal that costs a turn is worse than a wall.
    if (!arena_allows(hero_.a.pos, target)) {
        message(Text{"Двери не поддаются. Пока страж жив — отсюда не выйти.",
                     "The doors will not give. While the guardian lives, there is no way out."},
                Severity::Bad);
        return false;
    }

    if (map().at(target) == Tile::Door) {
        mutable_level().map.set(target, Tile::OpenDoor);
        message(Text{"Дверь со скрипом открывается.", "The door creaks open."});
        hero_.a.energy -= kEnergyPerTurn;
        return true;
    }

    if (!map().walkable(target)) {
        if (map().at(target) == Tile::Chasm)
            message(Text{"Провал. Туда лучше не шагать.", "A chasm. Better not step in."},
                    Severity::Bad);
        return false;  // bumping a wall must never cost a turn
    }

    const int cost = hero_move_cost(target);
    hero_.a.pos = target;
    hero_.a.energy -= cost;

    if (map().at(target) == Tile::Water)
        message(Text{"Ты бредёшь по холодной воде.", "You wade through cold water."});
    if (map().at(target) == Tile::Altar)
        message(Text{"Здесь древнее капище. Можно принести жертву (p).",
                     "An old shrine stands here. You may make an offering (p)."},
                Severity::Good);

    const int item = item_index_at(target);
    if (item >= 0) {
        const Item& it = level().items[static_cast<std::size_t>(item)];
        message(format(Text{"Здесь лежит: {}.", "Here lies: {}."}, item_name(it, ident_)));
    }
    if (map().at(target) == Tile::StairsDown)
        message(Text{"Лестница вниз (>).", "Stairs leading down (>)."});
    return true;
}

void Game::hero_attacks(Monster& m) {
    const ClassTemplate& tpl = class_info(hero_.cls);
    const Text name = monster_name(m);

    int damage = roll_damage(rng_, hero_attack(), m.a.defence);
    const bool crit = rng_.chance(tpl.crit_chance);
    if (crit) damage *= 2;

    // Рогатина is a boar spear: it was always meant for the big ones.
    const auto& beasts = bestiary();
    const std::size_t si = static_cast<std::size_t>(m.species);
    const bool big = si < beasts.size() && (beasts[si].ai & AiBoss);
    if (big && hero_has(GpVsBoss)) damage = damage * 3 / 2;

    if (crit)
        message(format(Text{"Точный удар! {} получает {} урона.",
                            "A precise strike! {} takes {} damage."},
                       name, num(damage)),
                Severity::Good);
    else
        message(format(Text{"Ты бьёшь: {} получает {} урона.", "You hit {} for {} damage."},
                       name, num(damage)));

    const Vec2 struck = m.a.pos;
    const bool was_alive = m.a.alive;
    damage_monster(m, damage, Text{"твой удар", "your blow"});

    // Палица knocks them down rather than through: a lost turn is worth more
    // than the damage it replaces, so the club is deliberately not the
    // hardest-hitting weapon in the table.
    if (m.a.alive && hero_has(GpStun) && !(big && m.a.max_hp > 200) && rng_.chance(20)) {
        m.a.add_effect(Effect::Freeze, 1, 1);
        message(format(Text{"{} сшиблен с ног.", "{} is knocked off their feet."}, name),
                Severity::Good);
    }

    if (was_alive && !m.a.alive) {
        if (hero_has(GpLifesteal)) hero_.a.heal(2 + depth_ / 4);
        if (hero_set() == GearSet::Naviy) {
            hero_.a.heal(3);
            hero_.mana = std::min(hero_.max_mana, hero_.mana + 1);
        }
    }

    // Богатырь's swing carries through to everything else within reach. The
    // sweep is resolved after the main target so a cleave cannot kill the
    // creature whose position the loop is reading.
    // The war gathering sweeps like a Богатырь does. A class trait and a set
    // that grant the same thing is on purpose: the set is how any other class
    // buys its way into that style of fighting, at the cost of every slot.
    if (class_has(hero_.cls, TraitCleave) || hero_set() == GearSet::Ratny) {
        int swept = 0;
        for (Vec2 d : directions8()) {
            const Vec2 p = hero_.a.pos + d;
            if (p == struck) continue;
            Monster* other = monster_at_mut(p);
            if (!other) continue;
            damage_monster(*other, std::max(1, damage / 2), Text{"размах", "the sweep"});
            ++swept;
        }
        if (swept > 0)
            message(format(Text{"Размах достаёт ещё {}.", "The sweep catches {} more."},
                           num(swept)),
                    Severity::Good);
    }
}

// ---------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------

bool Game::act_pick_up() {
    const int index = item_index_at(hero_.a.pos);
    if (index < 0) {
        message(Text{"Здесь ничего нет.", "There is nothing here."});
        return false;
    }

    Item it = level().items[static_cast<std::size_t>(index)];

    // The needle never goes into the pack. It used to, and a hero who arrived
    // on Кощей's floor with twenty things already in hand simply could not pick
    // it up — which made him unkillable, with nothing on screen to say why
    // (NAV-012). There is no reason to carry it, so taking it is breaking it.
    if (it.kind == ItemKind::Needle) {
        auto& floor = mutable_level().items;
        floor.erase(floor.begin() + index);
        needle_broken_ = true;
        message(Text{"Ты поднимаешь иглу и ломаешь её. Где-то далеко Кощей чувствует это.",
                     "You take up the needle and snap it. Somewhere far off, Koschei feels it."},
                Severity::Critical);
        hero_.a.energy -= kEnergyPerTurn;
        return true;
    }

    if (it.kind == ItemKind::Gold) {
        // Гривна: gold finds its way to whoever is already wearing some.
        if (hero_has(GpRichGold)) it.count += it.count / 3 + 1;
        hero_.gold += it.count;
        message(format(Text{"Ты подобрал {} золота.", "You pick up {} gold."}, num(it.count)),
                Severity::Good);
    } else {
        if (!hero_.inv.add(it)) {
            message(Text{"Котомка полна.", "Your pack is full."}, Severity::Bad);
            return false;
        }
        message(format(Text{"Ты подобрал: {}.", "You pick up: {}."}, item_name(it, ident_)),
                Severity::Good);
    }

    auto& items = mutable_level().items;
    items.erase(items.begin() + index);

    // На перекрёстке берут одно.
    //
    // The rule lives here rather than in a flag on the item: three things on
    // three pedestals and one pair of hands is the whole idea of the room, and
    // the engine already knows which floor it is standing on.
    if (in_lobby() && !items.empty()) {
        items.clear();
        message(Text{"Остальное перекрёсток оставляет себе.",
                     "The crossroads keeps the rest."},
                Severity::System);
    }

    hero_.a.energy -= kEnergyPerTurn;
    return true;
}

bool Game::act_drop(int index) {
    if (index < 0 || index >= static_cast<int>(hero_.inv.items.size())) return false;
    if (item_index_at(hero_.a.pos) >= 0) {
        message(Text{"Здесь уже что-то лежит.", "Something already lies here."}, Severity::Bad);
        return false;
    }

    Item dropped = hero_.inv.take(index, hero_.inv.items[static_cast<std::size_t>(index)].count);
    dropped.pos = hero_.a.pos;
    mutable_level().items.push_back(dropped);

    // `take` clears the slot when the dropped item was worn, but the derived
    // maxima — max HP from the Charm of Life, max mana from a staff or robe —
    // are stored on the hero and only recomputed where a piece goes on or off.
    // Dropping is the third way a worn piece leaves, and it has to fold the
    // bonus back out too, or the stat stays inflated for the rest of the run
    // (see docs/BUG_REPORTS.md, NAV-019).
    hero_.a.max_hp = derived_max_hp();
    hero_.a.hp = std::min(hero_.a.hp, hero_.a.max_hp);
    hero_.max_mana = derived_max_mana();
    hero_.mana = std::min(hero_.mana, hero_.max_mana);

    message(format(Text{"Ты бросил: {}.", "You drop: {}."}, item_name(dropped, ident_)));
    hero_.a.energy -= kEnergyPerTurn;
    return true;
}

bool Game::act_equip(int index) {
    if (index < 0 || index >= static_cast<int>(hero_.inv.items.size())) return false;
    Item& it = hero_.inv.items[static_cast<std::size_t>(index)];
    const Slot slot = item_slot(it);
    if (slot == Slot::None) {
        message(Text{"Это не надеть.", "That cannot be worn."}, Severity::Bad);
        return false;
    }

    int& worn = hero_.inv.slot_ref(slot);
    if (worn == index) {
        worn = -1;
        message(format(Text{"Ты снял: {}.", "You take off: {}."}, item_name(it, ident_)));
    } else {
        worn = index;
        message(format(Text{"Ты надел: {}.", "You equip: {}."}, item_name(it, ident_)),
                Severity::Good);
    }

    hero_.a.max_hp = derived_max_hp();
    hero_.a.hp = std::min(hero_.a.hp, hero_.a.max_hp);
    hero_.max_mana = derived_max_mana();
    hero_.mana = std::min(hero_.mana, hero_.max_mana);

    hero_.a.energy -= kEnergyPerTurn;
    return true;
}

/// Maximum health as the worn gear makes it.
///
/// Pulled out of `act_equip` so that the inventory screen can ask what a piece
/// *would* do without the two answers being computed by two different pieces of
/// code — which is exactly how a preview ends up lying about the thing it is
/// previewing.
int Game::derived_max_hp() const {
    const auto& gear = gear_table();
    int bonus = 0;
    if (hero_.inv.amulet >= 0) {
        const Item& am = hero_.inv.items[static_cast<std::size_t>(hero_.inv.amulet)];
        const std::size_t gi = static_cast<std::size_t>(am.subtype);
        if (gi < gear.size() && std::strcmp(gear[gi].key, "ob_zhizni") == 0)
            bonus = am.total_power();
    }
    const ClassTemplate& tpl = class_info(hero_.cls);
    return tpl.hp + tpl.hp_per_level * (hero_.level - 1) + bonus;
}

int Game::derived_max_mana() const {
    const auto& gear = gear_table();
    const ClassTemplate& tpl = class_info(hero_.cls);
    int mana_bonus = 0;
    auto staff_bonus = [&](int slot_index) {
        if (slot_index < 0) return;
        const Item& g = hero_.inv.items[static_cast<std::size_t>(slot_index)];
        const std::size_t gi = static_cast<std::size_t>(g.subtype);
        if (gi >= gear.size()) return;
        if (std::strcmp(gear[gi].key, "posokh") == 0 || std::strcmp(gear[gi].key, "mantiya") == 0)
            mana_bonus += 5;
    };
    staff_bonus(hero_.inv.weapon);
    staff_bonus(hero_.inv.armor);
    return tpl.mana + tpl.mana_per_level * (hero_.level - 1) + mana_bonus;
}

EquipPreview Game::equip_preview(int index) {
    EquipPreview p;
    if (index < 0 || index >= static_cast<int>(hero_.inv.items.size())) return p;
    const Item& it = hero_.inv.items[static_cast<std::size_t>(index)];
    const Slot slot = item_slot(it);
    if (slot == Slot::None) return p;

    int& worn = hero_.inv.slot_ref(slot);
    const int had = worn;
    p.valid = true;
    p.taking_off = had == index;

    const int atk = hero_attack(), def = hero_defence();
    const int hp = derived_max_hp(), spd = hero_speed(), sight = hero_sight();

    // The honest way to answer "what would this do" is to put it on, ask the
    // same questions the character sheet asks, and take it off again. Anything
    // else is a second implementation of the rules, kept in step by hope.
    worn = p.taking_off ? -1 : index;
    p.attack = hero_attack() - atk;
    p.defence = hero_defence() - def;
    p.max_hp = derived_max_hp() - hp;
    p.speed = hero_speed() - spd;
    p.sight = hero_sight() - sight;
    worn = had;
    return p;
}

bool Game::act_use_item(int index) {
    if (index < 0 || index >= static_cast<int>(hero_.inv.items.size())) return false;
    const Item it = hero_.inv.items[static_cast<std::size_t>(index)];

    switch (it.kind) {
        case ItemKind::Potion:
            hero_.inv.take(index, 1);
            quaff(it);
            break;
        case ItemKind::Scroll:
            hero_.inv.take(index, 1);
            read_scroll(it);
            break;
        case ItemKind::Food:
            hero_.inv.take(index, 1);
            hero_.nutrition = std::min(1600, hero_.nutrition + 700);
            message(Text{"Ты ешь. Стало легче.", "You eat. That helps."}, Severity::Good);
            break;
        case ItemKind::Needle:
            hero_.inv.take(index, 1);
            needle_broken_ = true;
            message(Text{"Ты ломаешь иглу. Где-то далеко Кощей чувствует это.",
                         "You snap the needle. Somewhere far off, Koschei feels it."},
                    Severity::Critical);
            break;
        case ItemKind::Weapon:
        case ItemKind::Armor:
        case ItemKind::Amulet:
            return act_equip(index);
        case ItemKind::Feather:
            // Nothing to do by hand: it acts on its own, once, when the blow
            // that would kill the hero lands.
            message(Text{"Перо само знает, когда понадобится.",
                         "The feather knows for itself when it will be needed."});
            return false;
        case ItemKind::Gold:
            return false;
    }

    hero_.a.energy -= kEnergyPerTurn;
    return true;
}

void Game::quaff(const Item& it) {
    ident_.learn(ItemKind::Potion, it.subtype);
    const Text name = item_name(it, ident_);
    message(format(Text{"Ты выпил: {}.", "You drink: {}."}, name));

    // In a herbalist's hands a draught goes half again as far, and its ill
    // effects run shorter — he knows what he is holding.
    const bool herbalist = class_has(hero_.cls, TraitHerbalist);
    const auto boost = [herbalist](int amount) { return herbalist ? amount * 3 / 2 : amount; };
    const auto shorten = [herbalist](int turns) { return herbalist ? turns / 2 : turns; };

    switch (static_cast<PotionKind>(it.subtype)) {
        case PotionKind::Heal: {
            const int amount = boost(15 + hero_.level * 2);
            hero_.a.heal(amount);
            message(format(Text{"Раны затягиваются (+{}).", "Your wounds close (+{})."},
                           num(amount)), Severity::Good);
            break;
        }
        case PotionKind::GreaterHeal: {
            const int amount = boost(40 + hero_.level * 3);
            hero_.a.heal(amount);
            hero_.a.clear_effect(Effect::Poison);
            hero_.a.clear_effect(Effect::Burn);
            message(format(Text{"Ты чувствуешь себя целым (+{}).", "You feel whole again (+{})."},
                           num(amount)), Severity::Good);
            break;
        }
        case PotionKind::Mana:
            hero_.mana = hero_.max_mana;
            message(Text{"Силы возвращаются.", "Your power returns."}, Severity::Good);
            break;
        case PotionKind::Might:
            hero_.a.add_effect(Effect::Might, boost(25), herbalist ? 7 : 5);
            message(Text{"Руки наливаются силой.", "Strength floods your arms."}, Severity::Good);
            break;
        case PotionKind::Haste:
            hero_.a.add_effect(Effect::Haste, boost(25), 1);
            message(Text{"Всё вокруг замедлилось.", "Everything around you slows down."},
                    Severity::Good);
            break;
        case PotionKind::Regen:
            hero_.a.add_effect(Effect::Regen, boost(40), herbalist ? 3 : 2);
            message(Text{"Живая вода. Раны заживают на глазах.",
                         "Living water. Your wounds knit closed."}, Severity::Good);
            break;
        case PotionKind::Poison:
            if (has_amulet(hero_, "ob_yada")) {
                message(Text{"Оберег гасит отраву.", "Your charm neutralises the venom."},
                        Severity::Good);
            } else {
                hero_.a.add_effect(Effect::Poison, shorten(14), 2);
                message(Text{"Отрава! Горло жжёт.", "Venom! Your throat burns."}, Severity::Bad);
            }
            break;
        case PotionKind::Confusion:
            hero_.a.add_effect(Effect::Confusion, shorten(14), 1);
            message(Text{"Пол уходит из-под ног.", "The floor tilts under you."}, Severity::Bad);
            break;
        case PotionKind::Count:
            break;
    }
}

void Game::read_scroll(const Item& it) {
    ident_.learn(ItemKind::Scroll, it.subtype);
    message(format(Text{"Ты прочёл: {}.", "You read: {}."}, item_name(it, ident_)));

    const int power = 10 + depth_ * 2 + hero_.level;
    auto affect_nearby = [&](int radius, const std::function<void(Monster&)>& fn) {
        for (auto& m : mutable_level().monsters) {
            if (!m.a.alive) continue;
            if (chebyshev(hero_.a.pos, m.a.pos) > radius) continue;
            if (!map().visible(m.a.pos)) continue;
            fn(m);
        }
    };

    switch (static_cast<ScrollKind>(it.subtype)) {
        case ScrollKind::Fireball:
            message(Text{"Пламя разлетается кругом!", "Flame bursts outward!"}, Severity::Good);
            affect_nearby(4, [&](Monster& m) {
                damage_monster(m, power, Text{"огненный шар", "a fireball"});
                if (m.a.alive) apply_effect_to_monster(m, Effect::Burn, 4, 2);
            });
            break;
        case ScrollKind::Lightning:
            message(Text{"Молнии бьют из свитка!", "Lightning leaps from the scroll!"},
                    Severity::Good);
            affect_nearby(7, [&](Monster& m) {
                damage_monster(m, power + 4, Text{"молния", "lightning"});
            });
            break;
        case ScrollKind::Frost:
            message(Text{"Всё вокруг схватывает стужей.", "Frost seizes everything around."},
                    Severity::Good);
            affect_nearby(5, [&](Monster& m) { apply_effect_to_monster(m, Effect::Freeze, 5, 1); });
            break;
        case ScrollKind::Blind:
            affect_nearby(6, [&](Monster& m) { apply_effect_to_monster(m, Effect::Blind, 8, 1); });
            message(Text{"Свет гаснет в чужих глазах.", "The light dies in their eyes."},
                    Severity::Good);
            break;
        case ScrollKind::Teleport: {
            const Vec2 spot = random_free_spot(level());
            if (map().walkable(spot)) {
                hero_.a.pos = spot;
                message(Text{"Тебя выбрасывает в другом месте.", "You are flung elsewhere."},
                        Severity::Good);
            }
            break;
        }
        case ScrollKind::MagicMap:
            mutable_level().map.reveal_all();
            message(Text{"Этаж проступает перед глазами.", "The floor takes shape in your mind."},
                    Severity::Good);
            break;
        case ScrollKind::Identify:
            for (auto& inv_item : hero_.inv.items) {
                inv_item.identified = true;
                if (inv_item.kind == ItemKind::Potion || inv_item.kind == ItemKind::Scroll)
                    ident_.learn(inv_item.kind, inv_item.subtype);
            }
            message(Text{"Ты понимаешь, что несёшь.", "You understand what you carry."},
                    Severity::Good);
            break;
        case ScrollKind::Summon: {
            message(Text{"Из темноты отвечают...", "Something answers from the dark..."},
                    Severity::Critical);
            const auto& beasts = bestiary();
            std::vector<int> weights(beasts.size(), 0);
            for (std::size_t i = 0; i < beasts.size(); ++i)
                weights[i] = spawn_weight(beasts[i], depth_);
            for (int i = 0; i < 3; ++i) {
                const int pick = rng_.weighted(weights);
                if (pick >= 0) spawn_species(mutable_level(), pick, hero_.a.pos, 4);
            }
            break;
        }
        case ScrollKind::Count:
            break;
    }
}

// ---------------------------------------------------------------------------
// Stairs and the shrine
// ---------------------------------------------------------------------------

bool Game::act_descend() {
    if (map().at(hero_.a.pos) != Tile::StairsDown) {
        message(Text{"Здесь нет лестницы вниз.", "There are no stairs down here."});
        return false;
    }
    if (depth_ >= kMaxDepth) return false;

    enter_level(depth_ + 1, true);
    message(format(Text{"Ты спускаешься. Глубина {}.", "You descend. Depth {}."}, num(depth_)),
            Severity::System);
    if (boss_for_depth(depth_) && !level().boss_slain)
        message(Text{"Воздух тяжелеет. Здесь кто-то ждёт.",
                     "The air grows heavy. Something waits here."}, Severity::Critical);
    hero_.a.energy -= kEnergyPerTurn;
    return true;
}

bool Game::act_ascend() {
    if (map().at(hero_.a.pos) != Tile::StairsUp) {
        message(Text{"Здесь нет лестницы вверх.", "There are no stairs up here."});
        return false;
    }
    if (depth_ <= kLobbyDepth) {
        message(Text{"Выше только небо, и оно не для тебя.",
                     "There is only sky above, and it is not for you."},
                Severity::Bad);
        return false;
    }
    if (depth_ == 1) {
        // Going back up to the crossroads is allowed exactly once, and only
        // before the first floor has been left behind: a run that could
        // re-shop between belts would be a different game.
        message(Text{"Ты возвращаешься на перекрёсток.", "You climb back to the crossroads."},
                Severity::System);
    }
    enter_level(depth_ - 1, false);
    message(format(Text{"Ты поднимаешься. Глубина {}.", "You climb. Depth {}."}, num(depth_)),
            Severity::System);
    hero_.a.energy -= kEnergyPerTurn;
    return true;
}

bool Game::act_pray() {
    if (map().at(hero_.a.pos) != Tile::Altar) {
        message(Text{"Здесь нечему молиться.", "There is nothing to pray to here."});
        return false;
    }

    // The shrine strengthens what you already carry, and takes payment in gold.
    int price = 40 + depth_ * 20;
    if (class_has(hero_.cls, TraitSmith)) price /= 2;
    if (hero_.gold < price) {
        message(format(Text{"Капище требует {} золота. У тебя {}.",
                            "The shrine asks {} gold. You have {}."},
                       num(price), num(hero_.gold)),
                Severity::Bad);
        return false;
    }

    std::vector<int> candidates;
    if (hero_.inv.weapon >= 0) candidates.push_back(hero_.inv.weapon);
    if (hero_.inv.armor >= 0) candidates.push_back(hero_.inv.armor);
    if (candidates.empty()) {
        message(Text{"Нечего освящать.", "You carry nothing to bless."}, Severity::Bad);
        return false;
    }

    hero_.gold -= price;
    Item& target = hero_.inv.items[static_cast<std::size_t>(rng_.pick(candidates))];
    ++target.enchant;
    mutable_level().map.set(hero_.a.pos, Tile::Floor);
    message(format(Text{"Капище принимает жертву. {} становится сильнее.",
                        "The shrine accepts. {} grows stronger."},
                   item_name(target, ident_)),
            Severity::Good);
    hero_.a.energy -= kEnergyPerTurn;
    return true;
}

// ---------------------------------------------------------------------------
// Spellcasting
// ---------------------------------------------------------------------------

bool Game::act_cast(Spell s, Vec2 target) {
    if (s >= Spell::Count) return false;
    if (!hero_.knows(s)) {
        message(Text{"Ты не знаешь такого заклятья.", "You do not know that spell."},
                Severity::Bad);
        return false;
    }

    const SpellTemplate& t = spell_info(s);
    // Зеркальце takes a third off every casting, rounded in the hero's favour.
    const int cost = hero_has(GpCheapSpell) ? std::max(1, t.cost * 2 / 3) : t.cost;
    if (hero_.mana < cost) {
        message(Text{"Не хватает сил.", "You lack the power."}, Severity::Bad);
        return false;
    }
    if (hero_.a.has(Effect::Confusion)) {
        message(Text{"Мысли путаются — заклятье срывается.",
                     "Your thoughts scatter; the spell fails."}, Severity::Bad);
        hero_.mana -= cost;
        hero_.a.energy -= kEnergyPerTurn;
        return true;
    }

    if (t.needs_target) {
        if (!map().in_bounds(target) || chebyshev(hero_.a.pos, target) > t.range ||
            !has_line_of_sight(map(), hero_.a.pos, target, t.range)) {
            message(Text{"Туда не достать.", "You cannot reach that."}, Severity::Bad);
            return false;
        }
    }

    hero_.mana -= cost;
    const int power = t.power + hero_.level * 2;

    switch (s) {
        case Spell::FireArrow: {
            Monster* m = monster_at_mut(target);
            if (!m) { message(Text{"Стрела уходит в пустоту.", "The arrow flies into nothing."}); break; }
            message(format(Text{"Огненная стрела бьёт в {}.", "A fire arrow strikes {}."},
                           monster_name(*m)), Severity::Good);
            damage_monster(*m, power, Text{"огненная стрела", "a fire arrow"});
            if (m->a.alive) apply_effect_to_monster(*m, Effect::Burn, 4, 2);
            break;
        }
        case Spell::IceBind: {
            Monster* m = monster_at_mut(target);
            if (!m) { message(Text{"Стужа тает впустую.", "The frost dissipates."}); break; }
            damage_monster(*m, power / 2, Text{"стужа", "frost"});
            if (m->a.alive) {
                apply_effect_to_monster(*m, Effect::Freeze, 4, 1);
                message(format(Text{"{} скован льдом.", "{} is bound in ice."}, monster_name(*m)),
                        Severity::Good);
            }
            break;
        }
        case Spell::Lightning: {
            message(Text{"Молния прошивает подземелье.", "Lightning tears through the dungeon."},
                    Severity::Good);
            for (Vec2 p : line(hero_.a.pos, target)) {
                if (!map().transparent(p)) break;
                if (Monster* m = monster_at_mut(p))
                    damage_monster(*m, power, Text{"молния", "lightning"});
            }
            break;
        }
        case Spell::Heal: {
            const int amount = power + 4;
            hero_.a.heal(amount);
            message(format(Text{"Раны затягиваются (+{}).", "Your wounds close (+{})."},
                           num(amount)), Severity::Good);
            break;
        }
        case Spell::Morok: {
            int touched = 0;
            for (auto& m : mutable_level().monsters) {
                if (!m.a.alive || chebyshev(hero_.a.pos, m.a.pos) > t.range) continue;
                apply_effect_to_monster(m, Effect::Confusion, 6, 1);
                ++touched;
            }
            message(format(Text{"Морок накрывает {} тварей.", "Bewilderment takes {} creatures."},
                           num(touched)), Severity::Good);
            break;
        }
        case Spell::Ward:
            hero_.a.add_effect(Effect::Shield, 20, t.power);
            message(Text{"Вокруг тебя смыкается оберег.", "A ward closes around you."},
                    Severity::Good);
            break;
        case Spell::Count:
            break;
    }

    hero_.a.energy -= kEnergyPerTurn;
    return true;
}

// ---------------------------------------------------------------------------
// Progression
// ---------------------------------------------------------------------------

void Game::grant_xp(int amount) {
    hero_.xp += amount;
    check_level_up();
}

void Game::check_level_up() {
    const ClassTemplate& tpl = class_info(hero_.cls);
    while (hero_.level < 30 && hero_.xp >= xp_for_level(hero_.level + 1)) {
        ++hero_.level;
        hero_.a.max_hp += tpl.hp_per_level;
        hero_.a.hp += tpl.hp_per_level;
        hero_.max_mana += tpl.mana_per_level;
        hero_.mana += tpl.mana_per_level;
        if (hero_.level % 2 == 0) ++hero_.a.attack;
        if (hero_.level % 3 == 0) ++hero_.a.defence;

        message(format(Text{"Ты стал сильнее. Уровень {}.", "You grow stronger. Level {}."},
                       num(hero_.level)),
                Severity::Good);

        for (const auto& [spell, need] : class_spells(hero_.cls)) {
            if (need != hero_.level || hero_.knows(spell)) continue;
            hero_.learn(spell);
            message(format(Text{"Новое заклятье: {}.", "New spell: {}."}, spell_info(spell).name),
                    Severity::Good);
        }
    }
}

}  // namespace nav
