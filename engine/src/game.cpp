// SPDX-License-Identifier: MIT
//
// Game lifecycle, level management and the turn scheduler.
// Hero actions live in actions.cpp; monster behaviour lives in ai.cpp.
#include "nav/game.hpp"

#include <algorithm>
#include <cstring>

#include "nav/fov.hpp"

namespace nav {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Game::start(const GameConfig& cfg) {
    cfg_ = cfg;
    rng_.reseed(cfg.seed);
    log_.clear();
    turn_ = 0;
    depth_ = 1;
    state_ = RunState::Playing;

    // Scramble consumable appearances for this run.
    ident_.reset(static_cast<std::size_t>(PotionKind::Count),
                 static_cast<std::size_t>(ScrollKind::Count));
    rng_.shuffle(ident_.potion_look);
    rng_.shuffle(ident_.scroll_look);

    // A herbalist never has to gamble on a draught: he recognises them all.
    if (class_has(cfg.hero_class, TraitHerbalist))
        for (int i = 0; i < static_cast<int>(PotionKind::Count); ++i)
            ident_.learn(ItemKind::Potion, i);

    const ClassTemplate& tpl = class_info(cfg.hero_class);
    hero_ = Hero{};
    hero_.cls = cfg.hero_class;
    hero_.a.hp = hero_.a.max_hp = tpl.hp;
    hero_.a.attack = tpl.attack;
    hero_.a.defence = tpl.defence;
    hero_.a.speed = tpl.speed;
    hero_.a.energy = kEnergyPerTurn;  // the hero moves first
    hero_.mana = hero_.max_mana = tpl.mana;
    hero_.sight = tpl.sight;
    hero_.level = 1;
    hero_.deepest = 1;

    // Starting gear, equipped straight away.
    const auto& gear = gear_table();
    auto give = [&](const char* key, Slot slot) {
        for (std::size_t i = 0; i < gear.size(); ++i) {
            if (std::strcmp(gear[i].key, key) != 0) continue;
            Item it{};
            it.kind = gear[i].kind;
            it.subtype = static_cast<int>(i);
            it.power = gear[i].power;
            it.identified = true;
            hero_.inv.add(it);
            hero_.inv.slot_ref(slot) = static_cast<int>(hero_.inv.items.size()) - 1;
            return;
        }
    };
    give(tpl.start_weapon, Slot::Weapon);
    give(tpl.start_armor, Slot::Armor);

    Item bread{};
    bread.kind = ItemKind::Food;
    bread.count = 2;
    bread.identified = true;
    hero_.inv.add(bread);

    Item heal{};
    heal.kind = ItemKind::Potion;
    heal.subtype = static_cast<int>(PotionKind::Heal);
    heal.count = 2;
    hero_.inv.add(heal);

    // Level-1 spells.
    hero_.spells.assign(static_cast<std::size_t>(Spell::Count), 0);
    for (const auto& [spell, lvl] : class_spells(cfg.hero_class))
        if (lvl <= 1) hero_.learn(spell);

    levels_.assign(static_cast<std::size_t>(kMaxDepth) + 1, Level{});
    depth_ = kLobbyDepth;
    enter_level(kLobbyDepth, true);

    message(Text{"Перекрёсток. Три дороги, и все вниз.",
                 "The crossroads. Three roads, and all of them lead down."},
            Severity::System);
    message(Text{"Возьми с собой одну вещь — остальное перекрёсток оставит себе.",
                 "Take one thing with you; the crossroads keeps the rest."},
            Severity::Info);
    message(format(Text{"{} — в путь.", "{} — on your way."}, tpl.name), Severity::Info);
}

// ---------------------------------------------------------------------------
// Level management
// ---------------------------------------------------------------------------

void Game::ensure_level(int depth) {
    if (depth < kLobbyDepth || depth > kMaxDepth) return;
    Level& lvl = levels_[static_cast<std::size_t>(depth)];
    if (lvl.generated) return;

    if (depth == kLobbyDepth) { build_lobby(lvl); return; }

    const ZoneTheme& theme = zone_theme_for_depth(depth);

    MapGenConfig mg;
    mg.width = cfg_.map_width;
    mg.height = cfg_.map_height;
    mg.max_depth = 4 + (depth >= 5 ? 1 : 0);
    mg.place_altar = (depth % 3 == 0);
    // Even the first floor gets stairs up now: they lead back to the
    // crossroads, which is a place rather than an exit.
    mg.place_stairs_up = true;
    // Everything that gives a belt its character comes from its theme rather
    // than from the depth number: caves or rooms, how much water, how many
    // chasms, whether there are doors at all.
    mg.caves = theme.caves;
    mg.water_chance = theme.water_chance;
    mg.chasm_chance = theme.chasm_chance;
    mg.door_chance = theme.door_chance;

    GeneratedLevel gen = generate_level(rng_, mg, depth);
    lvl.map = std::move(gen.map);
    lvl.entrance = gen.entrance;
    lvl.exit = gen.exit;
    lvl.generated = true;

    // The bottom floor has no way further down.
    if (depth == kMaxDepth) lvl.map.set(lvl.exit, Tile::Floor);

    populate(lvl, depth);
}

/// The crossroads: the one room in the game nothing generates.
///
/// It exists so that a run is prepared for rather than merely begun, and so
/// that the first thing the player sees is a place instead of a menu. Three
/// pieces of gear lie on it and the hero may carry exactly one away — the rule
/// lives in act_pick_up, so nothing here needs a flag of its own.
void Game::build_lobby(Level& lvl) {
    const int w = cfg_.map_width, h = cfg_.map_height;
    lvl.map = Map(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) lvl.map.set({x, y}, Tile::Wall);

    // A room a little wider than it is tall, centred, with a beaten path down
    // the middle. Odd dimensions so the altar and the stairs sit dead centre.
    const int rw = 21, rh = 13;
    const int x0 = (w - rw) / 2, y0 = (h - rh) / 2;
    for (int y = y0; y < y0 + rh; ++y)
        for (int x = x0; x < x0 + rw; ++x) lvl.map.set({x, y}, Tile::Floor);

    const Vec2 centre{x0 + rw / 2, y0 + rh / 2};
    lvl.map.set(centre, Tile::Altar);

    // The way down is at the far end; the hero starts at the near one, so the
    // room is crossed rather than stepped over.
    lvl.exit = Vec2{centre.x, y0 + rh - 2};
    lvl.entrance = Vec2{centre.x, y0 + 1};
    lvl.map.set(lvl.exit, Tile::StairsDown);

    // Three pedestals, evenly spaced across the upper half.
    const Vec2 stands[3] = {{centre.x - 6, centre.y - 3},
                            {centre.x,     centre.y - 4},
                            {centre.x + 6, centre.y - 3}};

    // The offer is drawn from the shallow, cheap end of the gear tables: the
    // crossroads is a head start, not a shortcut past the first belt.
    const auto& gear = gear_table();
    std::vector<int> offer;
    for (std::size_t i = 0; i < gear.size(); ++i)
        if (gear[i].min_depth <= 5 && gear[i].weight > 0) offer.push_back(static_cast<int>(i));
    rng_.shuffle(offer);

    for (int i = 0; i < 3 && i < static_cast<int>(offer.size()); ++i) {
        const GearTemplate& g = gear[static_cast<std::size_t>(offer[static_cast<std::size_t>(i)])];
        Item it{};
        it.kind = g.kind;
        it.subtype = offer[static_cast<std::size_t>(i)];
        it.power = g.power;
        it.identified = true;
        it.pos = stands[i];
        lvl.items.push_back(it);
    }

    lvl.generated = true;
}

void Game::enter_level(int depth, bool descending) {
    const int previous = depth_;
    const int clamped = std::clamp(depth, kLobbyDepth, kMaxDepth);
    const bool first_level = !levels_[static_cast<std::size_t>(clamped)].generated;

    ensure_level(depth);
    const bool crossed_belt = zone_for_depth(previous) != zone_for_depth(depth);
    depth_ = clamped;
    Level& lvl = mutable_level();

    hero_.a.pos = descending ? lvl.entrance : lvl.exit;
    if (!lvl.map.walkable(hero_.a.pos)) hero_.a.pos = random_free_spot(lvl);
    hero_.deepest = std::max(hero_.deepest, depth_);

    // The warding shirt gets its one blow back on every new floor. Tying it to
    // the floor rather than to a timer means the player can count on it at the
    // start of a fight, which is the only way a single-use defence is ever
    // something to plan around.
    hero_.ward_ready = 1;

    // The belts are the shape of the descent, so crossing into one is worth
    // saying out loud — but only the first time, and only going down.
    if (descending && (crossed_belt || first_level)) {
        const ZoneTheme& theme = zone_theme_for_depth(depth_);
        message(format(Text{"— {} —", "— {} —"}, theme.name), Severity::System);
        message(theme.arrival, Severity::Critical);
    }

    // A handful of lines the first time the dungeon proper is entered. Someone
    // opening this for the first time knows none of the conventions a roguelike
    // treats as obvious, and the cheapest place to say so is the log they are
    // already reading. Said once, never again — a game that keeps explaining
    // itself is a game that does not trust the player.
    if (depth_ == 1 && !hinted_start_) {
        hinted_start_ = true;
        message(Text{"Шаг в чудище — это удар. Отдельной кнопки для драки нет.",
                     "Stepping into a creature attacks it. There is no separate attack key."},
                Severity::System);
        message(Text{"Не спеши вниз: на этаже есть еда, зелья и опыт, а внизу будет труднее.",
                     "Do not rush down: this floor holds food, potions and experience, and the next is worse."},
                Severity::System);
        message(Text{"Зелья и свитки не подписаны, пока не испробуешь. Это часть игры.",
                     "Potions and scrolls are unlabelled until you try one. That is the game."},
                Severity::System);
    }

    needs_flow_rebuild_ = true;
    recompute_fov();
}

Vec2 Game::random_free_spot(const Level& lvl, Vec2 avoid, int min_distance) const {
    const auto cells = lvl.map.walkable_cells();
    if (cells.empty()) return {1, 1};
    // `rng_` is logically const here but the draw must still advance the run's
    // sequence, so the mutable access is deliberate.
    Rng& r = const_cast<Rng&>(rng_);

    Vec2 fallback{-1, -1};
    for (int attempt = 0; attempt < 300; ++attempt) {
        const Vec2 p = cells[static_cast<std::size_t>(r.below(static_cast<int>(cells.size())))];
        if (p == hero_.a.pos) continue;
        bool occupied = false;
        for (const auto& m : lvl.monsters)
            if (m.a.alive && m.a.pos == p) { occupied = true; break; }
        if (occupied) continue;

        if (fallback.x < 0) fallback = p;
        // A cramped level may have no cell far enough from `avoid`; in that
        // case the first unoccupied cell found is still a valid answer.
        if (avoid.x >= 0 && min_distance > 0 && chebyshev(p, avoid) < min_distance) continue;
        return p;
    }
    return fallback.x >= 0 ? fallback : cells.front();
}

Vec2 Game::free_spot_near(const Level& lvl, Vec2 origin, int radius) const {
    std::vector<Vec2> candidates;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const Vec2 p{origin.x + dx, origin.y + dy};
            if (!lvl.map.walkable(p) || p == hero_.a.pos) continue;
            bool occupied = false;
            for (const auto& m : lvl.monsters)
                if (m.a.alive && m.a.pos == p) { occupied = true; break; }
            if (!occupied) candidates.push_back(p);
        }
    }
    if (candidates.empty()) return {-1, -1};
    Rng& r = const_cast<Rng&>(rng_);
    return candidates[static_cast<std::size_t>(r.below(static_cast<int>(candidates.size())))];
}

void Game::populate(Level& lvl, int depth) {
    const auto& beasts = bestiary();

    // --- Monsters ---------------------------------------------------------
    std::vector<int> weights(beasts.size(), 0);
    int total = 0;
    for (std::size_t i = 0; i < beasts.size(); ++i) {
        weights[i] = spawn_weight(beasts[i], depth);
        total += weights[i];
    }

    const int count = 6 + depth + rng_.below(4) + zone_theme_for_depth(depth).extra_monsters;
    if (total > 0) {
        for (int i = 0; i < count; ++i) {
            const int pick = rng_.weighted(weights);
            if (pick < 0) break;
            // Keep the arrival staircase clear: waking up surrounded is not
            // difficulty, it is an unfair death (see docs/BUG_REPORTS.md, NAV-001).
            const Vec2 spot = random_free_spot(lvl, lvl.entrance, 7);
            if (!lvl.map.walkable(spot)) continue;

            Monster m{};
            m.species = pick;
            const Species& sp = beasts[static_cast<std::size_t>(pick)];
            m.a.pos = spot;
            m.a.hp = m.a.max_hp = sp.hp;
            m.a.attack = sp.attack;
            m.a.defence = sp.defence;
            m.a.speed = sp.speed;
            m.awake = (sp.ai & AiBoss) != 0;
            lvl.monsters.push_back(m);
        }
    }

    // --- Boss -------------------------------------------------------------
    if (const char* key = boss_for_depth(depth)) {
        const int idx = species_index(key);
        if (idx >= 0) {
            const Species& sp = beasts[static_cast<std::size_t>(idx)];
            Monster boss{};
            boss.species = idx;
            boss.a.hp = boss.a.max_hp = sp.hp;
            boss.a.attack = sp.attack;
            boss.a.defence = sp.defence;
            boss.a.speed = sp.speed;
            boss.awake = true;
            // Bosses guard the way down.
            Vec2 spot = free_spot_near(lvl, lvl.exit, 3);
            if (spot.x < 0) spot = random_free_spot(lvl);
            boss.a.pos = spot;
            lvl.monsters.push_back(boss);

            // Баба-Яга does not fight alone: her huts stand with her, and she
            // is all but untouchable while any of them is still standing.
            if (std::strcmp(key, "babayaga") == 0) {
                const int hut = species_index("izbushka");
                if (hut >= 0) {
                    const Species& hut_sp = beasts[static_cast<std::size_t>(hut)];
                    for (int i = 0; i < 2; ++i) {
                        const Vec2 place = free_spot_near(lvl, spot, 4);
                        if (place.x < 0) continue;
                        Monster m{};
                        m.species = hut;
                        m.a.pos = place;
                        m.a.hp = m.a.max_hp = hut_sp.hp;
                        m.a.attack = hut_sp.attack;
                        m.a.defence = hut_sp.defence;
                        m.a.speed = hut_sp.speed;
                        m.awake = true;
                        lvl.monsters.push_back(m);
                    }
                }
            }
        }
    }

    // Кощей's death is on a needle's point, and the needle is on his floor.
    // Without it he simply rises again, so its placement is not optional — and
    // the floor is asked for by his name, never spelled as a number. It was
    // spelled as one once, the dungeon grew four floors deeper, and the needle
    // quietly moved away from him (NAV-011).
    if (depth == boss_depth("koschei")) {
        Item needle{};
        needle.kind = ItemKind::Needle;
        needle.identified = true;
        needle.pos = random_free_spot(lvl, lvl.entrance, 12);
        if (lvl.map.walkable(needle.pos)) lvl.items.push_back(needle);
    }

    // --- Loot -------------------------------------------------------------
    const auto& gear = gear_table();
    std::vector<int> gear_weights(gear.size(), 0);
    for (std::size_t i = 0; i < gear.size(); ++i)
        gear_weights[i] = gear[i].min_depth <= depth ? gear[i].weight : 0;

    const int loot = 3 + rng_.below(4);
    for (int i = 0; i < loot; ++i) {
        Item it{};
        const int roll = rng_.below(100);
        if (roll < 30) {  // gear
            const int pick = rng_.weighted(gear_weights);
            if (pick < 0) continue;
            it.kind = gear[static_cast<std::size_t>(pick)].kind;
            it.subtype = pick;
            it.power = gear[static_cast<std::size_t>(pick)].power;
            it.identified = true;
            if (depth >= 4 && rng_.chance(20)) it.enchant = 1 + rng_.below(depth / 4);
        } else if (roll < 60) {  // potion
            it.kind = ItemKind::Potion;
            it.subtype = rng_.below(static_cast<int>(PotionKind::Count));
        } else if (roll < 82) {  // scroll
            it.kind = ItemKind::Scroll;
            it.subtype = rng_.below(static_cast<int>(ScrollKind::Count));
        } else if (roll < 92) {  // food
            it.kind = ItemKind::Food;
            it.identified = true;
        } else {  // gold
            it.kind = ItemKind::Gold;
            it.count = 10 + rng_.below(20 + depth * 8);
            it.identified = true;
        }
        it.pos = random_free_spot(lvl);
        if (lvl.map.walkable(it.pos)) lvl.items.push_back(it);
    }
}

// ---------------------------------------------------------------------------
// Turn scheduling
// ---------------------------------------------------------------------------

bool Game::perform(const Action& action) {
    if (state_ != RunState::Playing) return false;

    // The travel commands are loops over `perform_single`, so they are peeled
    // off here rather than inside the switch: they consume many turns, not one.
    if (action.type == ActionType::Run) return act_run(action.dir);
    if (action.type == ActionType::Explore) return act_explore();
    return perform_single(action);
}

bool Game::perform_single(const Action& action) {
    if (state_ != RunState::Playing) return false;

    bool consumed = false;
    switch (action.type) {
        case ActionType::Move:      consumed = act_move(action.dir); break;
        case ActionType::Wait:      consumed = true; hero_.a.energy -= kEnergyPerTurn; break;
        case ActionType::PickUp:    consumed = act_pick_up(); break;
        case ActionType::UseItem:
        case ActionType::Quaff:     consumed = act_use_item(action.index); break;
        case ActionType::EquipItem: consumed = act_equip(action.index); break;
        case ActionType::DropItem:  consumed = act_drop(action.index); break;
        case ActionType::Descend:   consumed = act_descend(); break;
        case ActionType::Ascend:    consumed = act_ascend(); break;
        case ActionType::CastSpell:
            consumed = act_cast(static_cast<Spell>(action.index), action.target);
            break;
        case ActionType::Pray:      consumed = act_pray(); break;
        // Handled by `perform` before it ever gets here; listed so that adding
        // an action to the enum still fails the build until it is handled.
        case ActionType::Run:
        case ActionType::Explore:
        case ActionType::None:      break;
    }

    if (!consumed) return false;

    // Corpses are cleared here as well as inside the scheduler. The scheduler
    // returns early once the run is over, so a monster killed by the blow that
    // ends the game used to stay in the list as a dead entry — a state every
    // frontend and every invariant check assumes cannot happen.
    reap_dead();
    recompute_fov();
    advance_until_hero_turn();
    reap_dead();
    recompute_fov();
    return true;
}

void Game::advance_until_hero_turn() {
    // Bounded so a scheduling bug shows up as a failing test rather than as a
    // browser tab that stops responding.
    for (int guard = 0; guard < 512; ++guard) {
        if (state_ != RunState::Playing) return;
        if (hero_.a.energy >= kEnergyPerTurn) return;

        ++turn_;

        hero_.a.energy += hero_.a.effective_speed();
        Level& lvl = mutable_level();
        for (auto& m : lvl.monsters)
            if (m.a.alive) m.a.energy += m.a.effective_speed();

        // Monsters act. The index loop is deliberate: summoning appends to the
        // vector mid-iteration, and newcomers must wait for the next tick.
        const std::size_t active = lvl.monsters.size();
        for (std::size_t i = 0; i < active && i < mutable_level().monsters.size(); ++i) {
            for (int acts = 0; acts < 4; ++acts) {
                Monster& m = mutable_level().monsters[i];
                if (!m.a.alive || m.a.energy < kEnergyPerTurn) break;
                m.a.energy -= kEnergyPerTurn;
                if (m.a.can_act()) monster_turn(i);
            }
            if (state_ != RunState::Playing) break;
        }

        for (auto& m : mutable_level().monsters)
            if (m.a.alive) tick_effects(m.a, false);
        tick_effects(hero_.a, true);
        tick_hero_upkeep();
        reap_dead();
        needs_flow_rebuild_ = true;
    }
}

void Game::tick_effects(Actor& a, bool is_hero) {
    for (auto& e : a.effects) {
        if (e.turns <= 0) continue;
        switch (e.kind) {
            case Effect::Poison:
                if (is_hero) damage_hero(e.power, Text{"яд", "poison"});
                else a.damage(e.power);
                break;
            case Effect::Burn:
                if (is_hero) damage_hero(e.power + 1, Text{"огонь", "fire"});
                else a.damage(e.power + 1);
                break;
            case Effect::Regen:
                a.heal(e.power);
                break;
            default:
                break;
        }
        --e.turns;
    }
    a.effects.erase(std::remove_if(a.effects.begin(), a.effects.end(),
                                   [](const ActiveEffect& e) { return e.turns <= 0; }),
                    a.effects.end());
}

void Game::tick_hero_upkeep() {
    if (!hero_.a.alive) return;

    // Hunger.
    if (hero_.nutrition > 0) {
        --hero_.nutrition;
        if (hero_.nutrition == 200)
            message(Text{"Ты проголодался.", "You are getting hungry."}, Severity::Bad);
        if (hero_.nutrition == 50)
            message(Text{"Голод сводит живот.", "Hunger is gnawing at you."}, Severity::Bad);
    } else if (turn_ % 5 == 0) {
        damage_hero(1, Text{"голод", "starvation"});
    }

    // Slow natural recovery, faster while well fed.
    const int interval = hero_.nutrition > 0 ? 12 : 40;
    if (turn_ % interval == 0 && hero_.a.hp < hero_.a.max_hp) hero_.a.heal(1);
    if (turn_ % 12 == 0 && hero_.mana < hero_.max_mana) ++hero_.mana;

    // Саван mends on its own, three times as fast as flesh does.
    if (hero_has(GpRegen) && turn_ % 4 == 0 && hero_.a.hp < hero_.a.max_hp) hero_.a.heal(1);
}

void Game::reap_dead() {
    Level& lvl = mutable_level();
    for (auto& m : lvl.monsters) {
        if (m.a.alive || m.a.hp > 0) continue;
        if (m.a.max_hp <= 0) continue;
        // Already reported; nothing to do here.
    }
    lvl.monsters.erase(std::remove_if(lvl.monsters.begin(), lvl.monsters.end(),
                                      [](const Monster& m) { return !m.a.alive; }),
                       lvl.monsters.end());
}

void Game::recompute_fov() {
    Level& lvl = mutable_level();
    compute_fov(lvl.map, hero_.a.pos, hero_sight());
}

// ---------------------------------------------------------------------------
// Derived statistics
// ---------------------------------------------------------------------------

namespace {

/// The gear template behind an equipped slot, or nullptr when it is empty.
const GearTemplate* worn(const Inventory& inv, int slot) {
    if (slot < 0 || slot >= static_cast<int>(inv.items.size())) return nullptr;
    const Item& it = inv.items[static_cast<std::size_t>(slot)];
    if (!it.is_gear()) return nullptr;
    const auto& gear = gear_table();
    const std::size_t i = static_cast<std::size_t>(it.subtype);
    return i < gear.size() ? &gear[i] : nullptr;
}

}  // namespace

std::uint32_t Game::hero_powers() const {
    const Inventory& inv = hero_.inv;
    std::uint32_t bits = 0;
    for (int slot : {inv.weapon, inv.armor, inv.amulet})
        if (const GearTemplate* g = worn(inv, slot)) bits |= g->powers;
    return bits;
}

GearSet Game::hero_set() const {
    const Inventory& inv = hero_.inv;
    const GearTemplate* w = worn(inv, inv.weapon);
    const GearTemplate* a = worn(inv, inv.armor);
    const GearTemplate* m = worn(inv, inv.amulet);
    if (!w || !a || !m) return GearSet::None;
    if (w->set == GearSet::None) return GearSet::None;
    if (w->set != a->set || w->set != m->set) return GearSet::None;
    return w->set;
}

const Monster* Game::active_boss() const {
    const char* key = boss_for_depth(depth_);
    if (!key) return nullptr;
    const int idx = species_index(key);
    if (idx < 0) return nullptr;
    for (const auto& m : level().monsters)
        if (m.a.alive && m.species == idx) return &m;
    return nullptr;
}

int Game::hero_attack() const {
    int atk = hero_.a.attack;
    const auto& inv = hero_.inv;
    if (inv.weapon >= 0 && inv.weapon < static_cast<int>(inv.items.size())) {
        atk += inv.items[static_cast<std::size_t>(inv.weapon)].total_power();
        // A smith gets one extra grade out of whatever he is holding.
        if (class_has(hero_.cls, TraitSmith)) ++atk;
    }
    if (inv.amulet >= 0 && inv.amulet < static_cast<int>(inv.items.size())) {
        const Item& am = inv.items[static_cast<std::size_t>(inv.amulet)];
        if (std::strcmp(gear_table()[static_cast<std::size_t>(am.subtype)].key, "ob_sily") == 0)
            atk += am.total_power();
    }
    atk += hero_.a.effect_power(Effect::Might);
    return std::max(1, atk);
}

int Game::hero_defence() const {
    int def = hero_.a.defence;
    const auto& inv = hero_.inv;
    if (inv.armor >= 0 && inv.armor < static_cast<int>(inv.items.size())) {
        def += inv.items[static_cast<std::size_t>(inv.armor)].total_power();
        if (class_has(hero_.cls, TraitSmith)) ++def;
    }
    def += hero_.a.effect_power(Effect::Shield);
    return std::max(0, def);
}

int Game::hero_sight() const {
    if (hero_.a.has(Effect::Blind)) return 1;
    int sight = hero_.sight;
    const auto& inv = hero_.inv;
    if (inv.amulet >= 0 && inv.amulet < static_cast<int>(inv.items.size())) {
        const Item& am = inv.items[static_cast<std::size_t>(inv.amulet)];
        if (std::strcmp(gear_table()[static_cast<std::size_t>(am.subtype)].key, "ob_zorko") == 0)
            sight += am.total_power();
    }
    if (hero_has(GpSight)) sight += 3;
    return std::clamp(sight, 1, 20);
}

int Game::hero_speed() const {
    int speed = hero_.a.speed;
    const auto& inv = hero_.inv;
    if (inv.amulet >= 0 && inv.amulet < static_cast<int>(inv.items.size())) {
        const Item& am = inv.items[static_cast<std::size_t>(inv.amulet)];
        if (std::strcmp(gear_table()[static_cast<std::size_t>(am.subtype)].key, "ob_skoro") == 0)
            speed += am.total_power();
    }
    // Both halves of the traveller's kit push, so wearing the pair is worth
    // more than wearing either — which is what makes gathering it feel like
    // progress before the third piece turns up.
    if (const GearTemplate* w = worn(hero_.inv, hero_.inv.weapon))
        if (w->powers & GpQuick) speed += 15;
    if (const GearTemplate* a = worn(hero_.inv, hero_.inv.armor))
        if (a->powers & GpQuick) speed += 15;
    return speed;
}

Postmortem Game::postmortem() const {
    Postmortem pm;
    pm.blows = blows_;
    if (!blows_.empty()) pm.killed_by = blows_.back().source;

    // Everything drinkable or readable still in the pack. This is the line that
    // stings, and it should: most deaths in this game are not "the dungeon was
    // too hard" but "the healing draught was two keys away".
    for (const Item& it : hero_.inv.items) {
        if (it.kind != ItemKind::Potion && it.kind != ItemKind::Scroll &&
            it.kind != ItemKind::Food)
            continue;
        Text line = item_name(it, ident_);
        if (it.count > 1) line = line + Text{" x"} + num(it.count);
        pm.unspent.push_back(line);
    }
    return pm;
}

int Game::score() const {
    int s = hero_.gold + hero_.deepest * 120 + hero_.kills * 12 + hero_.level * 60;
    if (state_ == RunState::Ascended) s += 5000;
    return s;
}

// ---------------------------------------------------------------------------
// Sprite keys
//
// Deliberately strings rather than the enum values: a frontend keyed on numbers
// silently draws the wrong thing the day an enum gains a member, while a
// missing string key is caught by the test that walks every tile, species and
// item and demands a sprite for each.
// ---------------------------------------------------------------------------

const char* hero_sprite_key(HeroClass c) {
    switch (c) {
        case HeroClass::Vityaz:  return "hero_vityaz";
        case HeroClass::Vedun:   return "hero_vedun";
        case HeroClass::Tat:     return "hero_tat";
        case HeroClass::Znahar:  return "hero_znahar";
        case HeroClass::Kuznets: return "hero_kuznets";
        case HeroClass::Bogatyr: return "hero_bogatyr";
        default:                 return "hero_vityaz";
    }
}

const char* item_sprite_key(ItemKind kind) {
    switch (kind) {
        case ItemKind::Weapon: return "item_weapon";
        case ItemKind::Armor:  return "item_armor";
        case ItemKind::Amulet: return "item_amulet";
        case ItemKind::Potion: return "item_potion";
        case ItemKind::Scroll: return "item_scroll";
        case ItemKind::Food:   return "item_food";
        case ItemKind::Gold:   return "item_gold";
        case ItemKind::Needle: return "item_needle";
    }
    return "item_gold";
}

const char* tile_sprite_key(Tile t) {
    switch (t) {
        case Tile::Wall:       return "wall";
        case Tile::Floor:      return "floor";
        case Tile::StairsDown: return "stairs_down";
        case Tile::StairsUp:   return "stairs_up";
        case Tile::Door:       return "door";
        case Tile::OpenDoor:   return "door_open";
        case Tile::Water:      return "water";
        case Tile::Chasm:      return "chasm";
        case Tile::Altar:      return "altar";
    }
    return "floor";
}

// ---------------------------------------------------------------------------
// Spatial queries
// ---------------------------------------------------------------------------

const Monster* Game::monster_at(Vec2 p) const {
    for (const auto& m : level().monsters)
        if (m.a.alive && m.a.pos == p) return &m;
    return nullptr;
}

Monster* Game::monster_at_mut(Vec2 p) {
    for (auto& m : mutable_level().monsters)
        if (m.a.alive && m.a.pos == p) return &m;
    return nullptr;
}

int Game::item_index_at(Vec2 p) const {
    const auto& items = level().items;
    for (int i = static_cast<int>(items.size()) - 1; i >= 0; --i)
        if (items[static_cast<std::size_t>(i)].pos == p) return i;
    return -1;
}

bool Game::blocked_for_monster(Vec2 p, Vec2 self) const {
    if (!level().map.walkable(p)) return true;
    if (p == hero_.a.pos) return true;
    for (const auto& m : level().monsters)
        if (m.a.alive && m.a.pos == p && m.a.pos != self) return true;
    return false;
}

Text Game::monster_name(const Monster& m) const {
    const auto& beasts = bestiary();
    const std::size_t i = static_cast<std::size_t>(m.species);
    return i < beasts.size() ? beasts[i].name : Text{"нечто", "something"};
}

RenderCell Game::render_at(Vec2 p) const {
    RenderCell cell;
    const Map& m = map();
    cell.visible = m.visible(p);
    cell.explored = m.explored(p);
    if (!cell.explored) return cell;

    // Walls are the brighter of the two: they are what gives a floor its shape,
    // and the first palette had them almost the same value as the floor, which
    // made rooms hard to read at small cell sizes. The three values come from
    // the belt, so a glance at the screen says which part of the dungeon this is.
    const ZoneTheme& theme = zone_theme_for_depth(depth_);
    cell.terrain = tile_sprite_key(m.at(p));
    switch (m.at(p)) {
        case Tile::Wall:       cell.glyph = '#'; cell.color = theme.wall_color; break;
        case Tile::Floor:      cell.glyph = '.'; cell.color = theme.floor_color; break;
        case Tile::StairsDown: cell.glyph = '>'; cell.color = "#e8d8a0"; break;
        case Tile::StairsUp:   cell.glyph = '<'; cell.color = "#e8d8a0"; break;
        case Tile::Door:       cell.glyph = '+'; cell.color = "#a97c50"; break;
        case Tile::OpenDoor:   cell.glyph = '\''; cell.color = "#a97c50"; break;
        case Tile::Water:      cell.glyph = '~'; cell.color = theme.liquid_color; break;
        case Tile::Chasm:      cell.glyph = ' '; cell.color = "#1a1a20"; break;
        case Tile::Altar:      cell.glyph = '_'; cell.color = "#c9b6e0"; break;
    }

    if (!cell.visible) return cell;  // remembered terrain only

    const int item = item_index_at(p);
    if (item >= 0) {
        const Item& it = level().items[static_cast<std::size_t>(item)];
        cell.glyph = item_glyph(it);
        cell.color = it.kind == ItemKind::Gold ? "#e0c060" : "#9fd0c0";
        cell.entity = item_sprite_key(it.kind);
    }

    if (const Monster* mon = monster_at(p)) {
        const auto& beasts = bestiary();
        const std::size_t i = static_cast<std::size_t>(mon->species);
        if (i < beasts.size()) {
            cell.glyph = beasts[i].glyph;
            cell.color = beasts[i].color;
            cell.entity = beasts[i].key;
        }
    }

    if (p == hero_.a.pos) {
        cell.glyph = '@';
        cell.color = "#ffffff";
        cell.entity = hero_sprite_key(hero_.cls);
    }
    return cell;
}

std::vector<Spell> Game::castable_spells() const {
    std::vector<Spell> out;
    for (const auto& t : spell_table())
        if (hero_.knows(t.spell) &&
            hero_.mana >= (hero_has(GpCheapSpell) ? std::max(1, t.cost * 2 / 3) : t.cost))
            out.push_back(t.spell);
    return out;
}

std::vector<Vec2> Game::spell_targets(Spell s) const {
    std::vector<Vec2> out;
    const SpellTemplate& t = spell_info(s);
    if (!t.needs_target) return out;
    for (const auto& m : level().monsters) {
        if (!m.a.alive || !map().visible(m.a.pos)) continue;
        if (chebyshev(hero_.a.pos, m.a.pos) > t.range) continue;
        if (has_line_of_sight(map(), hero_.a.pos, m.a.pos, t.range)) out.push_back(m.a.pos);
    }
    // Nearest first, so the aiming UI can preselect the obvious target.
    Vec2 origin = hero_.a.pos;
    std::sort(out.begin(), out.end(), [origin](Vec2 a, Vec2 b) {
        const int da = dist_sq(origin, a), db = dist_sq(origin, b);
        return da != db ? da < db : a < b;
    });
    return out;
}

void Game::message(const Text& t, Severity sev) {
    log_.push_back(LogEntry{t, sev, turn_});
    // The log is a ring in spirit: only the tail is ever displayed, and an
    // unbounded vector would grow without limit over a long run.
    if (log_.size() > 400) log_.erase(log_.begin(), log_.begin() + 200);
}

}  // namespace nav
