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
    enter_level(1, true);

    message(Text{"Ты спускаешься в Навь. Назад дороги нет.",
                 "You descend into Nav. There is no road back."},
            Severity::System);
    message(format(Text{"{} — глубина 1.", "{} — depth 1."}, tpl.name), Severity::Info);
}

// ---------------------------------------------------------------------------
// Level management
// ---------------------------------------------------------------------------

void Game::ensure_level(int depth) {
    if (depth < 1 || depth > kMaxDepth) return;
    Level& lvl = levels_[static_cast<std::size_t>(depth)];
    if (lvl.generated) return;

    MapGenConfig mg;
    mg.width = cfg_.map_width;
    mg.height = cfg_.map_height;
    // Deeper floors get more rooms and more hazards.
    mg.max_depth = 4 + (depth >= 5 ? 1 : 0);
    mg.place_altar = (depth % 3 == 0);
    mg.place_stairs_up = depth > 1;
    mg.chasm_chance = 15 + depth * 3;

    GeneratedLevel gen = generate_level(rng_, mg, depth);
    lvl.map = std::move(gen.map);
    lvl.entrance = gen.entrance;
    lvl.exit = gen.exit;
    lvl.generated = true;

    // The bottom floor has no way further down.
    if (depth == kMaxDepth) lvl.map.set(lvl.exit, Tile::Floor);

    populate(lvl, depth);
}

void Game::enter_level(int depth, bool descending) {
    ensure_level(depth);
    depth_ = std::clamp(depth, 1, kMaxDepth);
    Level& lvl = mutable_level();

    hero_.a.pos = descending ? lvl.entrance : lvl.exit;
    if (!lvl.map.walkable(hero_.a.pos)) hero_.a.pos = random_free_spot(lvl);
    hero_.deepest = std::max(hero_.deepest, depth_);

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

    const int count = 6 + depth + rng_.below(4);
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
        }
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
        case ActionType::None:      break;
    }

    if (!consumed) return false;

    recompute_fov();
    advance_until_hero_turn();
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

int Game::hero_attack() const {
    int atk = hero_.a.attack;
    const auto& inv = hero_.inv;
    if (inv.weapon >= 0 && inv.weapon < static_cast<int>(inv.items.size()))
        atk += inv.items[static_cast<std::size_t>(inv.weapon)].total_power();
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
    if (inv.armor >= 0 && inv.armor < static_cast<int>(inv.items.size()))
        def += inv.items[static_cast<std::size_t>(inv.armor)].total_power();
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
    return speed;
}

int Game::score() const {
    int s = hero_.gold + hero_.deepest * 120 + hero_.kills * 12 + hero_.level * 60;
    if (state_ == RunState::Ascended) s += 5000;
    return s;
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
    // made rooms hard to read at small cell sizes.
    switch (m.at(p)) {
        case Tile::Wall:       cell.glyph = '#'; cell.color = "#7d6c58"; break;
        case Tile::Floor:      cell.glyph = '.'; cell.color = "#453d34"; break;
        case Tile::StairsDown: cell.glyph = '>'; cell.color = "#e8d8a0"; break;
        case Tile::StairsUp:   cell.glyph = '<'; cell.color = "#e8d8a0"; break;
        case Tile::Door:       cell.glyph = '+'; cell.color = "#a97c50"; break;
        case Tile::OpenDoor:   cell.glyph = '\''; cell.color = "#a97c50"; break;
        case Tile::Water:      cell.glyph = '~'; cell.color = "#4d7fa8"; break;
        case Tile::Chasm:      cell.glyph = ' '; cell.color = "#1a1a20"; break;
        case Tile::Altar:      cell.glyph = '_'; cell.color = "#c9b6e0"; break;
    }

    if (!cell.visible) return cell;  // remembered terrain only

    const int item = item_index_at(p);
    if (item >= 0) {
        const Item& it = level().items[static_cast<std::size_t>(item)];
        cell.glyph = item_glyph(it);
        cell.color = it.kind == ItemKind::Gold ? "#e0c060" : "#9fd0c0";
    }

    if (const Monster* mon = monster_at(p)) {
        const auto& beasts = bestiary();
        const std::size_t i = static_cast<std::size_t>(mon->species);
        if (i < beasts.size()) { cell.glyph = beasts[i].glyph; cell.color = beasts[i].color; }
    }

    if (p == hero_.a.pos) { cell.glyph = '@'; cell.color = "#ffffff"; }
    return cell;
}

std::vector<Spell> Game::castable_spells() const {
    std::vector<Spell> out;
    for (const auto& t : spell_table())
        if (hero_.knows(t.spell) && hero_.mana >= t.cost) out.push_back(t.spell);
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
