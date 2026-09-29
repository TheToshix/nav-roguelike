// SPDX-License-Identifier: MIT
//
// The real-time game: one fixed step at a time. See rt.hpp for why it exists
// beside the turn-based engine rather than instead of it. The guardians live in
// rt_bosses.cpp, the weapon and hero tables in rt_data.cpp.
#include "nav/rt.hpp"

#include <algorithm>
#include <cmath>

#include "nav/fov.hpp"
#include "nav/mapgen.hpp"

namespace nav {
namespace rt {

float length(V v) { return std::sqrt(v.x * v.x + v.y * v.y); }

V normalized(V v) {
    const float l = length(v);
    return l > 1e-6f ? V{v.x / l, v.y / l} : V{0, 0};
}

V rotated(V v, float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

namespace {

Vec2 cell_of(V p) {
    return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y))};
}

V centre(Vec2 c) { return {static_cast<float>(c.x) + 0.5f, static_cast<float>(c.y) + 0.5f}; }

float dist(V a, V b) { return length(a - b); }

float frand(Rng& rng) { return static_cast<float>(rng.unit()); }

constexpr float kPi = 3.14159265f;

/// What never walks the floors as an ordinary creature. Guardians are placed
/// on their own floors by place_boss; the neutral and the skittish are built
/// around a turn-based dance of approach and flight; a creature that never
/// leaves its cell is a turret, which wants its own design.
constexpr std::uint32_t kNotSpawned = AiBoss | AiMiniBoss | AiNeutral | AiSkittish | AiStationary;

/// How far the hero sees. Enemies outside it are neither drawn nor aimed at.
constexpr int kSight = 9;

}  // namespace

void World::emit(EventKind kind, V at, int amount, int ref, float a, float b) {
    // A frontend drains this every frame. A test that never does must not be
    // able to grow it without bound.
    if (events_.size() >= 512) return;
    Event e;
    e.kind = kind;
    e.x = at.x;
    e.y = at.y;
    e.amount = amount;
    e.ref = ref;
    e.a = a;
    e.b = b;
    events_.push_back(e);
}

std::vector<Event> World::take_events() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

// --- Lifecycle -------------------------------------------------------------------

void World::start(std::uint64_t seed, HeroClass cls) {
    rng_ = Rng(seed);
    const HeroSpec& spec = hero_spec(cls);
    hero_ = Hero{};
    hero_.cls = cls;
    hero_.max_hp = hero_.hp = spec.hp;
    // The shield comes back on its own: the thing that lets a player take a
    // hit, back off, and try the room again instead of dying of attrition to
    // every scratch.
    hero_.max_shield = hero_.shield = spec.shield;
    hero_.max_energy = hero_.energy = spec.energy;
    hero_.weapons[0] = weapon_index(spec.weapon);
    hero_.weapons[1] = -1;
    hero_.hand = 0;
    dead_ = won_ = false;
    time_ = 0;
    next_id_ = 1;
    events_.clear();
    enter_floor(1);
}

void World::give_weapon(int weapon) {
    if (weapon < 0 || weapon >= static_cast<int>(weapons().size())) return;
    if (hero_.weapons[1] < 0) {
        hero_.weapons[1] = weapon;
        hero_.hand = 1;
    } else {
        hero_.weapons[hero_.hand] = weapon;
    }
}

void World::enter_floor(int depth) {
    depth_ = depth;
    const ZoneTheme& theme = zone_theme_for_depth(depth);

    MapGenConfig mg;
    mg.max_depth = 4 + (depth >= 5 ? 1 : 0);
    mg.place_altar = false;
    mg.place_stairs_up = false;   // there is no going back up on a clock
    mg.caves = theme.caves;
    mg.water_chance = theme.water_chance;
    mg.chasm_chance = theme.chasm_chance;
    mg.door_chance = theme.door_chance;

    GeneratedLevel gen = generate_level(rng_, mg, depth);
    map_ = std::move(gen.map);
    // Doors stand open. On a clock, a closed door is a wall with a pause in
    // it; the only doors that shut are the ones a fight slams behind you.
    for (Tile& t : map_.raw_tiles())
        if (t == Tile::Door) t = Tile::OpenDoor;
    exit_ = gen.exit;

    enemies_.clear();
    spawned_.clear();
    bullets_.clear();
    pickups_.clear();
    telegraphs_.clear();
    hazards_.clear();
    boss_id_ = 0;

    Vec2 start = gen.entrance;
    if (walk_blocked(start)) {
        const auto cells = map_.walkable_cells();
        if (!cells.empty()) start = cells.front();
    }
    hero_.pos = centre(start);
    // A breath on arrival, to see the floor before anything on it can bite.
    hero_.invulnerable = 1.0f;
    hero_.frozen = hero_.burning = hero_.blind = 0;

    build_rooms(gen.rooms);
    populate();
    place_boss();
    hero_cell_ = {-1, -1};
    refresh_view();
}

void World::load_test_floor(const Map& map, V hero_pos, const std::vector<Rect>& rooms) {
    map_ = map;
    exit_ = {-1, -1};
    enemies_.clear();
    spawned_.clear();
    bullets_.clear();
    pickups_.clear();
    events_.clear();
    telegraphs_.clear();
    hazards_.clear();
    boss_id_ = 0;
    hero_.pos = hero_pos;
    hero_.invulnerable = 0;
    build_rooms(rooms);
    hero_cell_ = {-1, -1};
    refresh_view();
}

void World::build_rooms(const std::vector<Rect>& interiors) {
    rooms_.clear();
    for (const Rect& r : interiors) {
        Room room;
        room.interior = r;
        // Every open cell of the ring around the interior is a way in. A
        // corridor that runs along a room's side opens several of them, and
        // each has to shut, or the lock is a suggestion.
        const auto consider = [&](Vec2 c) {
            if (!map_.in_bounds(c) || walk_blocked(c)) return;
            room.doorways.push_back(c);
        };
        for (int x = r.left() - 1; x <= r.right() + 1; ++x) {
            consider({x, r.top() - 1});
            consider({x, r.bottom() + 1});
        }
        for (int y = r.top(); y <= r.bottom(); ++y) {
            consider({r.left() - 1, y});
            consider({r.right() + 1, y});
        }
        rooms_.push_back(std::move(room));
    }
}

Enemy World::make_enemy(int species, V pos, int room) {
    const Species& sp = bestiary()[static_cast<std::size_t>(species)];
    Enemy e;
    e.id = next_id_++;
    e.species = species;
    e.pos = pos;
    e.max_hp = e.hp = std::max(1, sp.hp);
    e.speed = kEnemySpeed * static_cast<float>(sp.speed) / 100.0f;
    e.damage = std::max(1, sp.attack / 2 + 1);
    e.ranged = (sp.ai & AiRanged) != 0;
    e.room = room;
    // Staggered, or a room of archers fires as one and the volley reads as a
    // single bullet.
    e.fire_cd = 0.6f + frand(rng_) * 1.2f;
    e.strafe = rng_.chance(50) ? 1.0f : -1.0f;
    return e;
}

Enemy& World::add_enemy(int species, V pos, int room) {
    enemies_.push_back(make_enemy(species, pos, room));
    return enemies_.back();
}

Enemy& World::spawn_now(int species, V pos, int owner) {
    // Mid-step, the enemies vector is being walked; new arrivals wait here
    // and join it once the walk is done.
    Enemy e = make_enemy(species, pos, -1);
    e.owner = owner;
    e.awake = true;
    spawned_.push_back(e);
    return spawned_.back();
}

void World::populate() {
    // The belt's own spawn table, so the mire fills with the mire's creatures.
    std::vector<int> weights;
    weights.reserve(bestiary().size());
    for (const Species& s : bestiary())
        weights.push_back((s.ai & kNotSpawned) ? 0 : spawn_weight(s, depth_));

    const Vec2 start = cell_of(hero_.pos);
    const auto spawn = [&](Vec2 c, int room) {
        const int pick = rng_.weighted(weights);
        if (pick >= 0) add_enemy(pick, centre(c), room);
    };

    if (!rooms_.empty()) {
        for (std::size_t i = 0; i < rooms_.size(); ++i) {
            Room& room = rooms_[i];
            // The room the hero arrives in is empty: the first thing a floor
            // does should not be to shut someone in before they have looked.
            if (room.interior.contains(start)) {
                room.cleared = true;
                continue;
            }
            std::vector<Vec2> cells;
            for (int y = room.interior.top() + 1; y <= room.interior.bottom() - 1; ++y)
                for (int x = room.interior.left() + 1; x <= room.interior.right() - 1; ++x)
                    if (!walk_blocked({x, y}) && Vec2{x, y} != exit_) cells.push_back({x, y});
            const int want = std::min(7, 2 + depth_ / 3 + room.interior.area() / 24);
            const int n = std::min(want, static_cast<int>(cells.size()));
            if (n == 0) {
                room.cleared = true;
                continue;
            }
            rng_.shuffle(cells);
            for (int k = 0; k < n; ++k) spawn(cells[static_cast<std::size_t>(k)], static_cast<int>(i));
        }
        return;
    }

    // Caves have no rooms to lock, so their creatures lie in wait across the
    // floor instead, kept a fair distance from where the hero comes in.
    std::vector<Vec2> cells;
    for (Vec2 c : map_.walkable_cells())
        if (chebyshev(c, start) > 8 && c != exit_) cells.push_back(c);
    rng_.shuffle(cells);
    const int n = std::min(static_cast<int>(cells.size()), 10 + depth_ * 2);
    for (int k = 0; k < n; ++k) spawn(cells[static_cast<std::size_t>(k)], -1);
}

// --- Terrain -------------------------------------------------------------------

bool World::walk_blocked(Vec2 c) const {
    if (!map_.in_bounds(c)) return true;
    const Tile t = map_.at(c);
    return t == Tile::Wall || t == Tile::Chasm || t == Tile::Door;
}

bool World::shot_blocked(Vec2 c) const {
    if (!map_.in_bounds(c)) return true;
    const Tile t = map_.at(c);
    // A bullet flies over a chasm; it does not fly through a wall or a door
    // that a fight has slammed.
    return t == Tile::Wall || t == Tile::Door;
}

void World::move_circle(V& pos, V delta, float radius) const {
    // Sub-steps no longer than a fifth of a cell, so nothing fast enough to
    // cross a wall between two ticks ever does.
    const float len = length(delta);
    const int steps = std::max(1, static_cast<int>(std::ceil(len / 0.2f)));
    const V part = delta * (1.0f / static_cast<float>(steps));
    for (int s = 0; s < steps; ++s) {
        pos = pos + part;
        for (int pass = 0; pass < 2; ++pass) {
            const int x0 = static_cast<int>(std::floor(pos.x - radius));
            const int x1 = static_cast<int>(std::floor(pos.x + radius));
            const int y0 = static_cast<int>(std::floor(pos.y - radius));
            const int y1 = static_cast<int>(std::floor(pos.y + radius));
            for (int cy = y0; cy <= y1; ++cy) {
                for (int cx = x0; cx <= x1; ++cx) {
                    if (!walk_blocked({cx, cy})) continue;
                    const float left = static_cast<float>(cx), top = static_cast<float>(cy);
                    // Pushed out along the line to the wall's nearest point.
                    // That is what makes a body slide along a wall rather than
                    // stop dead the moment it grazes one.
                    const V q{std::clamp(pos.x, left, left + 1.0f), std::clamp(pos.y, top, top + 1.0f)};
                    const V d = pos - q;
                    const float dl = length(d);
                    if (dl >= radius) continue;
                    if (dl > 1e-5f) {
                        pos = pos + d * ((radius - dl) / dl);
                        continue;
                    }
                    // The centre itself is inside the wall: out by the
                    // shortest way.
                    const float to_left = pos.x - left, to_right = left + 1.0f - pos.x;
                    const float to_top = pos.y - top, to_bottom = top + 1.0f - pos.y;
                    const float least = std::min({to_left, to_right, to_top, to_bottom});
                    if (least == to_left) pos.x = left - radius;
                    else if (least == to_right) pos.x = left + 1.0f + radius;
                    else if (least == to_top) pos.y = top - radius;
                    else pos.y = top + 1.0f + radius;
                }
            }
        }
    }
}

void World::refresh_view() {
    const Vec2 c = cell_of(hero_.pos);
    if (c == hero_cell_) return;
    hero_cell_ = c;
    map_.clear_visible();
    compute_fov(map_, c, kSight);
    // Enemies walk the flow field downhill to the hero: one breadth-first pass
    // per cell the hero crosses serves every monster on the floor.
    flow_.build(map_, {c}, [this](Vec2 p) { return !walk_blocked(p); });
    ++map_revision_;
}

bool World::sees_hero_from(V from) const {
    return has_line_of_sight(map_, cell_of(from), hero_cell_, 20);
}

int World::room_of(Vec2 c) const {
    for (std::size_t i = 0; i < rooms_.size(); ++i)
        if (rooms_[i].interior.contains(c)) return static_cast<int>(i);
    return -1;
}

bool World::in_locked_room() const {
    for (const Room& r : rooms_)
        if (r.locked) return true;
    return false;
}

const Enemy* World::boss() const {
    if (boss_id_ == 0) return nullptr;
    for (const Enemy& e : enemies_)
        if (e.id == boss_id_ && e.hp > 0) return &e;
    return nullptr;
}

bool World::exit_open() const { return !in_locked_room() && boss() == nullptr; }

void World::lock_room(int r) {
    Room& room = rooms_[static_cast<std::size_t>(r)];
    room.saved.clear();
    for (Vec2 c : room.doorways) {
        room.saved.push_back(map_.at(c));
        map_.set(c, Tile::Door);
    }
    room.locked = true;
    for (Enemy& e : enemies_)
        if (e.room == r) e.awake = true;
    emit(EventKind::RoomLocked, centre(room.interior.center()),
         static_cast<int>(room.doorways.size()), r);
    hero_cell_ = {-1, -1};   // the terrain changed under the flow field
    refresh_view();
}

void World::unlock_room(int r) {
    Room& room = rooms_[static_cast<std::size_t>(r)];
    for (std::size_t i = 0; i < room.doorways.size(); ++i) {
        const Tile was = room.saved[i];
        map_.set(room.doorways[i], was == Tile::Door ? Tile::OpenDoor : was);
    }
    room.locked = false;
    room.cleared = true;
    const V middle = centre(room.interior.center());
    emit(EventKind::RoomCleared, middle, 0, r);
    // A reward for the room. A guardian's hall pays out when the guardian
    // falls, not here.
    if (!room.boss) {
        if (rng_.chance(28)) {
            std::vector<int> pool;
            for (int w = 0; w < static_cast<int>(weapons().size()); ++w)
                if (w != hero_.weapons[0] && w != hero_.weapons[1]) pool.push_back(w);
            if (!pool.empty()) drop(PickupKind::Weapon, middle, pool[static_cast<std::size_t>(rng_.below(static_cast<int>(pool.size())))]);
        } else {
            drop(PickupKind::Energy, middle, 12);
        }
    }
    hero_cell_ = {-1, -1};
    refresh_view();
}

void World::drop(PickupKind kind, V at, int amount) {
    pickups_.push_back(Pickup{next_id_++, kind, at, amount});
}

// --- Harm ---------------------------------------------------------------------

void World::hurt_hero(int amount) {
    if (dead_ || hero_.invulnerable > 0 || amount <= 0) return;
    hero_.invulnerable = kInvulnerable;
    hero_.shield_delay = hero_.cls == HeroClass::Vityaz ? kShieldDelay * 0.5f : kShieldDelay;
    const int absorbed = std::min(hero_.shield, amount);
    if (absorbed > 0) {
        hero_.shield -= absorbed;
        emit(EventKind::ShieldHit, hero_.pos, absorbed);
        if (hero_.shield == 0) emit(EventKind::ShieldBroken, hero_.pos);
    }
    const int rest = amount - absorbed;
    if (rest > 0) {
        hero_.hp = std::max(0, hero_.hp - rest);
        emit(EventKind::HeroHit, hero_.pos, rest);
    }
    if (hero_.hp <= 0) {
        dead_ = true;
        emit(EventKind::HeroDied, hero_.pos);
    }
}

void World::burn_hero() {
    // Fire does not wait for the invulnerable window: standing in it is a
    // choice the player keeps making, and it should keep costing.
    hero_.shield_delay = kShieldDelay;
    if (hero_.shield > 0) {
        --hero_.shield;
        emit(EventKind::ShieldHit, hero_.pos, 1);
    } else {
        hero_.hp = std::max(0, hero_.hp - 1);
        emit(EventKind::HeroHit, hero_.pos, 1);
        if (hero_.hp <= 0 && !dead_) {
            dead_ = true;
            emit(EventKind::HeroDied, hero_.pos);
        }
    }
}

void World::hurt_enemy(Enemy& e, int amount, V push) {
    if (e.hp <= 0 || e.hidden) return;
    int dealt = static_cast<int>(std::lround(static_cast<float>(amount) * e.armour));
    if (e.armour < 1.0f) {
        dealt = std::max(1, dealt);
        emit(EventKind::Shielded, e.pos, dealt, e.id);
    }
    e.hp -= dealt;
    e.hurt = 0.12f;
    e.awake = true;
    // Guardians barely budge and huts not at all: a boss knocked across its
    // own hall by a crossbow is a boss the player stops respecting.
    const float heft = e.role == Role::Boss ? 0.2f
                     : (e.role == Role::Hut || e.role == Role::Needle) ? 0.0f : 1.0f;
    e.knock = e.knock + push * heft;
    emit(EventKind::EnemyHit, e.pos, dealt, e.id);

    if (e.role == Role::Boss) {
        // Phases are even shares of the health bar, and only ever rise.
        const int want = std::clamp(1 + (e.max_hp - std::max(0, e.hp)) * e.phases / e.max_hp, 1, e.phases);
        if (want > e.phase && e.hp > 0) {
            e.phase = want;
            e.state = 0;
            e.timer = 0.6f;
            emit(EventKind::BossPhase, e.pos, e.phase, e.id);
        }
    }
    if (e.hp <= 0) kill_enemy(e);
}

void World::kill_enemy(Enemy& e) {
    // Кощей does not die while his needle is whole.
    if (e.role == Role::Boss && e.species == species_index(species_key::kKoschei)) {
        for (Enemy& n : enemies_) {
            if (n.role != Role::Needle || n.owner != e.id || n.hp <= 0) continue;
            e.hp = e.max_hp * 3 / 5;
            ++e.revives;
            e.state = 0;
            e.timer = 1.2f;
            emit(EventKind::Revived, e.pos, e.revives, e.id);
            if (n.hidden) {
                // Found the first time he rises: without this the needle is a
                // puzzle nobody has been told exists.
                n.hidden = false;
                emit(EventKind::NeedleShown, n.pos, 0, n.id);
            }
            const int upyr = species_index("upyr");
            if (upyr >= 0)
                for (int k = 0; k < 2; ++k)
                    spawn_now(upyr, e.pos + V{k == 0 ? -1.2f : 1.2f, 0.4f}, e.id);
            return;
        }
    }

    ++hero_.kills;
    // The species rides in `amount`, so the frontend can colour the burst by
    // the creature without keeping its own copy of who was where.
    emit(EventKind::EnemyDied, e.pos, e.species, e.id);

    switch (e.role) {
        case Role::Boss: {
            emit(EventKind::BossDied, e.pos, 0, e.species);
            // Everything it brought goes with it.
            for (Enemy& other : enemies_)
                if (other.owner == e.id && other.hp > 0) {
                    other.hp = 0;
                    emit(EventKind::EnemyDied, other.pos, other.species, other.id);
                }
            for (Enemy& other : spawned_)
                if (other.owner == e.id) other.hp = 0;
            std::vector<int> pool;
            for (int w = 0; w < static_cast<int>(weapons().size()); ++w)
                if (w != hero_.weapons[0] && w != hero_.weapons[1]) pool.push_back(w);
            if (!pool.empty())
                drop(PickupKind::Weapon, e.pos, pool[static_cast<std::size_t>(rng_.below(static_cast<int>(pool.size())))]);
            drop(PickupKind::Potion, e.pos + V{0.6f, 0.2f}, 0);
            for (int k = 0; k < 6; ++k)
                drop(PickupKind::Coin, e.pos + V{frand(rng_) * 1.6f - 0.8f, frand(rng_) * 1.6f - 0.8f}, 1 + depth_ / 3);
            drop(PickupKind::Energy, e.pos + V{-0.6f, 0.2f}, 30);
            return;
        }
        case Role::Needle:
            emit(EventKind::NeedleBroken, e.pos, 0, e.id);
            return;
        case Role::Hut: {
            int left = 0;
            for (const Enemy& other : enemies_)
                if (other.role == Role::Hut && other.hp > 0 && other.id != e.id) ++left;
            emit(EventKind::HutFell, e.pos, left, e.id);
            return;
        }
        case Role::Illusion:
            return;
        case Role::Normal:
            break;
    }

    // Anything its guardian summoned dies without paying out, or a boss that
    // calls for help becomes a coin fountain.
    if (e.owner != 0) return;
    if (rng_.chance(55)) {
        int coins = 1 + depth_ / 4;
        if (hero_.cls == HeroClass::Tat) coins = (coins * 3 + 1) / 2;
        drop(PickupKind::Coin, e.pos, coins);
    }
    if (rng_.chance(40)) drop(PickupKind::Energy, e.pos + V{-0.2f, 0.15f}, 6 + depth_ / 3);
    if (rng_.chance(6)) drop(PickupKind::Potion, e.pos + V{0.25f, 0.1f}, 0);
}

// --- Decisions -----------------------------------------------------------------

const Enemy* World::pick_target() const {
    // Aim is automatic, as it is in the games this plays like: on a phone the
    // thumb that steers cannot also aim, and the choice that matters is where
    // to stand, not which pixel to point at. Blinded by Вий, the hero can only
    // find what is right in front of them.
    //
    // What a guardian hides behind — Кощей's needle, Яга's huts — counts as
    // half as far as it is: nearest-first would otherwise keep the sights on
    // the guardian, whose blows then do nothing, and the one thing the fight
    // asks for could not be pointed at without walking right up to it.
    const Enemy* best = nullptr;
    const float range = hero_.blind > 0 ? 3.0f : kAutoAimRange;
    float best_score = range;
    for (const Enemy& e : enemies_) {
        if (e.hp <= 0 || e.hidden) continue;
        const Vec2 ec = cell_of(e.pos);
        if (!map_.visible(ec)) continue;
        const float d = dist(e.pos, hero_.pos);
        if (d >= range) continue;
        const bool key = e.role == Role::Needle || e.role == Role::Hut;
        const float score = key ? d * 0.5f : d;
        if (score >= best_score) continue;
        if (!has_line_of_sight(map_, hero_cell_, ec, 16)) continue;
        best = &e;
        best_score = score;
    }
    return best;
}

V World::steer(const Enemy& e) const {
    const Vec2 c = cell_of(e.pos);
    // Close and in plain sight: straight at the hero, which is smoother than
    // any path through cell centres.
    if (dist(e.pos, hero_.pos) < 4.0f && has_line_of_sight(map_, c, hero_cell_, 8))
        return normalized(hero_.pos - e.pos);

    const int here = flow_.at(c);
    if (here >= DijkstraMap::kUnreachable) return {0, 0};
    Vec2 best = c;
    int best_cost = here;
    for (Vec2 d : directions8()) {
        const Vec2 n = c + d;
        if (walk_blocked(n)) continue;
        // No cutting corners: a diagonal needs both of its sides open, or the
        // body grinds against the very wall it is trying to go round.
        if (d.x != 0 && d.y != 0 &&
            (walk_blocked({c.x + d.x, c.y}) || walk_blocked({c.x, c.y + d.y})))
            continue;
        const int cost = flow_.at(n);
        if (cost < best_cost) {
            best_cost = cost;
            best = n;
        }
    }
    if (best == c) return normalized(hero_.pos - e.pos);
    return normalized(centre(best) - e.pos);
}

void World::shoot(V from, V dir, float speed, int damage, float radius) {
    Bullet b;
    b.pos = from + dir * 0.45f;
    b.vel = dir * speed;
    b.radius = radius;
    b.damage = damage;
    b.hero = false;
    b.life = 3.0f;
    bullets_.push_back(std::move(b));
}

void World::ring(V from, int count, float speed, int damage, float offset) {
    for (int i = 0; i < count; ++i) {
        const float a = offset + 2.0f * kPi * static_cast<float>(i) / static_cast<float>(count);
        shoot(from, V{std::cos(a), std::sin(a)}, speed, damage);
    }
}

void World::think_ordinary(Enemy& e, float d, bool sees) {
    if (!e.awake) {
        // A room's guards wake when its doors shut; anything outside a room
        // wakes when it sees the hero close by.
        const bool wakes = e.room >= 0 ? rooms_[static_cast<std::size_t>(e.room)].locked
                                       : (sees && d < 7.0f);
        if (wakes) e.awake = true;
    }

    V vel = e.knock;
    if (e.awake) {
        const Species& sp = bestiary()[static_cast<std::size_t>(e.species)];
        V want = steer(e);
        if (e.ranged && sees && d < kRangedRange + 3.0f) {
            // A thrower circles the hero at the range it likes, edging out
            // while it circles if the hero closes in and edging in if the hero
            // backs off.
            const V to = normalized(hero_.pos - e.pos);
            const V around{-to.y * e.strafe, to.x * e.strafe};
            const float pull = std::clamp((d - kRangedRange) / 2.0f, -1.0f, 1.0f);
            want = normalized(around + to * pull);
        }
        if ((sp.ai & AiCoward) && e.hp * 3 < e.max_hp) want = normalized(e.pos - hero_.pos);
        vel = vel + want * e.speed;
        e.facing = V{hero_.pos.x >= e.pos.x ? 1.0f : -1.0f, 0};
    }
    move_circle(e.pos, vel * kDt, e.radius);

    // A guard that chased the hero out of its room is no longer that room's
    // guard. Left counted, it would be shut outside the moment the hero walked
    // in — and a room waiting on a creature it has locked out never opens.
    if (e.room >= 0) {
        const Room& own = rooms_[static_cast<std::size_t>(e.room)];
        if (!own.locked && !own.interior.contains(cell_of(e.pos))) e.room = -1;
    }

    if (!e.awake) return;
    if (!e.ranged && d < hero_.radius + e.radius + 0.08f && e.attack_cd <= 0) {
        e.attack_cd = kMeleeCooldown;
        hurt_hero(e.damage);
    }
    if (e.ranged && sees && d < 10.0f && e.fire_cd <= 0) {
        e.fire_cd = 1.4f + frand(rng_) * 0.8f;
        if (rng_.chance(35)) e.strafe = -e.strafe;
        const V aim = normalized(hero_.pos - e.pos);
        Bullet b;
        b.pos = e.pos + aim * 0.4f;
        b.vel = aim * kEnemyBulletSpeed;
        b.radius = 0.14f;
        b.damage = e.damage;
        b.hero = false;
        b.life = 2.6f;
        bullets_.push_back(std::move(b));
        emit(EventKind::EnemyShot, e.pos, 0, e.id);
    }
}

// --- The hero's hands ------------------------------------------------------------

void World::interact() {
    // The weapon underfoot, if there is one: into the empty hand, or swapped
    // for the one in use, which is left lying where the new one was.
    Pickup* best = nullptr;
    float best_d = kPickupReach;
    for (Pickup& p : pickups_) {
        if (p.kind != PickupKind::Weapon || p.amount < 0) continue;
        const float d = dist(p.pos, hero_.pos);
        if (d < best_d) {
            best_d = d;
            best = &p;
        }
    }
    if (best) {
        const int taken = best->amount;
        if (hero_.weapons[1] < 0) {
            hero_.weapons[1] = taken;
            hero_.hand = 1;
            best->amount = -1;
        } else {
            best->amount = hero_.weapons[hero_.hand];
            hero_.weapons[hero_.hand] = taken;
        }
        hero_.fire_cd = 0.15f;
        emit(EventKind::WeaponTaken, hero_.pos, 0, taken);
        return;
    }
    if (hero_.weapons[1] >= 0) {
        hero_.hand ^= 1;
        hero_.fire_cd = 0.15f;
        emit(EventKind::WeaponSwapped, hero_.pos, 0, hero_.weapon());
    }
}

void World::use_weapon(const Enemy* target) {
    const int wi = hero_.weapon();
    if (wi < 0 || hero_.fire_cd > 0) return;
    const WeaponSpec& w = weapons()[static_cast<std::size_t>(wi)];
    if (w.energy > hero_.energy) {
        hero_.fire_cd = 0.4f;   // one complaint, not sixty a second
        emit(EventKind::NoEnergy, hero_.pos);
        return;
    }
    hero_.energy -= w.energy;
    hero_.fire_cd = w.interval;
    int damage = w.damage;
    if (hero_.cls == HeroClass::Kuznets) damage = (damage * 5 + 3) / 4;
    if (target) hero_.facing = hero_.aim;   // turn to face what you strike at

    if (w.melee) {
        float arc = w.arc * (hero_.cls == HeroClass::Bogatyr ? 1.5f : 1.0f);
        arc = std::min(arc, 2.0f * kPi);
        const float aim = std::atan2(hero_.aim.y, hero_.aim.x);
        emit(EventKind::Swing, hero_.pos, static_cast<int>(w.reach * 100.0f), wi, aim, arc);
        const auto in_arc = [&](V p, float reach) {
            const V to = p - hero_.pos;
            if (length(to) > reach) return false;
            if (arc >= 2.0f * kPi - 1e-3f) return true;
            float diff = std::atan2(to.y, to.x) - aim;
            while (diff > kPi) diff -= 2.0f * kPi;
            while (diff < -kPi) diff += 2.0f * kPi;
            return std::fabs(diff) <= arc * 0.5f;
        };
        for (Enemy& e : enemies_) {
            if (e.hp <= 0 || e.hidden) continue;
            if (!in_arc(e.pos, w.reach + e.radius)) continue;
            hurt_enemy(e, damage, normalized(e.pos - hero_.pos) * w.knock);
        }
        // A blade cuts what is flying at it. Without this a melee hero in a
        // room of throwers has no answer but to take every stone.
        for (Bullet& b : bullets_) {
            if (b.hero || b.life <= 0) continue;
            if (!in_arc(b.pos, w.reach + 0.2f)) continue;
            b.life = 0;
            emit(EventKind::BulletWall, b.pos, 0, 1);
        }
        return;
    }

    const V muzzle = hero_.pos + hero_.aim * 0.45f;
    for (int p = 0; p < w.pellets; ++p) {
        float off = 0;
        if (w.pellets > 1) off = -w.spread * 0.5f + w.spread * static_cast<float>(p) / static_cast<float>(w.pellets - 1);
        else if (w.spread > 0) off = (frand(rng_) - 0.5f) * w.spread;
        Bullet b;
        b.pos = muzzle;
        b.vel = rotated(hero_.aim, off) * w.speed;
        b.radius = w.radius;
        b.damage = damage;
        b.hero = true;
        b.life = w.life;
        b.pierce = w.pierce;
        b.knock = w.knock;
        bullets_.push_back(std::move(b));
    }
    emit(EventKind::Shot, muzzle, 0, wi);
}

// --- The clock -----------------------------------------------------------------

void World::step(const Input& in) {
    if (dead_ || won_) return;
    time_ += kDt;
    telegraphs_.clear();

    // --- The hero ----------------------------------------------------------
    const HeroSpec& spec = hero_spec(hero_.cls);
    V dir{in.move_x, in.move_y};
    const float dl = length(dir);
    if (dl > 1.0f) dir = dir * (1.0f / dl);
    if (hero_.frozen > 0) dir = {0, 0};   // Морозко's cold: the legs will not move
    hero_.moving = length(dir) > 0.05f;
    if (hero_.moving) hero_.facing = normalized(dir);
    const bool wading = map_.at(cell_of(hero_.pos)) == Tile::Water;
    move_circle(hero_.pos, dir * (kHeroSpeed * spec.speed * (wading ? kWaterSlow : 1.0f) * kDt),
                hero_.radius);
    refresh_view();

    hero_.invulnerable = std::max(0.0f, hero_.invulnerable - kDt);
    hero_.frozen = std::max(0.0f, hero_.frozen - kDt);
    hero_.blind = std::max(0.0f, hero_.blind - kDt);
    if (hero_.burning > 0) {
        hero_.burning -= kDt;
        hero_.burn_tick -= kDt;
        if (hero_.burn_tick <= 0) {
            hero_.burn_tick = 0.5f;
            burn_hero();
        }
    }
    const bool vityaz = hero_.cls == HeroClass::Vityaz;
    if (hero_.shield_delay > 0) {
        hero_.shield_delay -= kDt;
    } else if (hero_.shield < hero_.max_shield) {
        hero_.shield_tick -= kDt;
        if (hero_.shield_tick <= 0) {
            ++hero_.shield;
            hero_.shield_tick = vityaz ? kShieldTick * 0.5f : kShieldTick;
        }
    }
    if (hero_.cls == HeroClass::Vedun && hero_.energy < hero_.max_energy) {
        hero_.energy_tick -= kDt;
        if (hero_.energy_tick <= 0) {
            ++hero_.energy;
            hero_.energy_tick = 1.0f / 3.0f;
        }
    }

    // --- The room the hero is standing in ------------------------------------
    const int here = room_of(hero_cell_);
    if (here >= 0) {
        Room& room = rooms_[static_cast<std::size_t>(here)];
        if (!room.cleared && !room.locked) {
            bool guarded = false;
            for (const Enemy& e : enemies_)
                if (e.room == here && e.hp > 0) { guarded = true; break; }
            if (!guarded) {
                room.cleared = true;
            } else {
                // Shut only once the hero is clear of every doorway, or the
                // door would close on them.
                bool clear = true;
                for (Vec2 d : room.doorways)
                    if (dist(centre(d), hero_.pos) < hero_.radius + 0.75f) { clear = false; break; }
                if (clear) lock_room(here);
            }
        }
    }

    // --- Hands ---------------------------------------------------------------------
    if (in.interact) interact();
    hero_.fire_cd = std::max(0.0f, hero_.fire_cd - kDt);
    const Enemy* target = pick_target();
    hero_.aim = target ? normalized(target->pos - hero_.pos) : hero_.facing;
    hero_.firing = in.fire;
    if (in.fire) use_weapon(target);

    // --- Enemies -----------------------------------------------------------------
    for (Enemy& e : enemies_) {
        if (e.hp <= 0) continue;
        e.hurt = std::max(0.0f, e.hurt - kDt);
        e.attack_cd = std::max(0.0f, e.attack_cd - kDt);
        e.fire_cd -= kDt;
        e.knock = e.knock * std::max(0.0f, 1.0f - 10.0f * kDt);
        const float d = dist(e.pos, hero_.pos);
        const bool sees = d < 12.0f && sees_hero_from(e.pos);
        if (e.role == Role::Normal) think_ordinary(e, d, sees);
        else think_boss(e, d, sees);
    }
    for (Enemy& e : spawned_) enemies_.push_back(e);
    spawned_.clear();

    // Enemies shoulder one another apart rather than stacking into one cell.
    for (std::size_t i = 0; i < enemies_.size(); ++i) {
        for (std::size_t j = i + 1; j < enemies_.size(); ++j) {
            Enemy& a = enemies_[i];
            Enemy& b = enemies_[j];
            if (!a.awake && !b.awake) continue;
            if (a.hidden || b.hidden || a.hp <= 0 || b.hp <= 0) continue;
            const V d = b.pos - a.pos;
            const float l = length(d);
            const float least = a.radius + b.radius;
            if (l >= least || l < 1e-4f) continue;
            const V push = d * ((least - l) / l * 0.5f);
            // Huts and needles are planted; only the other one gives way.
            const bool a_fixed = a.role == Role::Hut || a.role == Role::Needle;
            const bool b_fixed = b.role == Role::Hut || b.role == Role::Needle;
            if (!a_fixed) move_circle(a.pos, push * (b_fixed ? -2.0f : -1.0f), a.radius);
            if (!b_fixed) move_circle(b.pos, push * (a_fixed ? 2.0f : 1.0f), b.radius);
        }
    }

    // --- Bullets -------------------------------------------------------------------
    for (Bullet& b : bullets_) {
        b.life -= kDt;
        const V delta = b.vel * kDt;
        const int parts = std::max(1, static_cast<int>(std::ceil(length(delta) / 0.25f)));
        for (int k = 0; k < parts && b.life > 0; ++k) {
            b.pos = b.pos + delta * (1.0f / static_cast<float>(parts));
            if (shot_blocked(cell_of(b.pos))) {
                emit(EventKind::BulletWall, b.pos, 0, b.hero ? 1 : 0);
                if (b.ember) {
                    Hazard h;
                    h.pos = b.pos - normalized(b.vel) * 0.4f;
                    hazards_.push_back(h);
                }
                b.life = 0;
                break;
            }
            if (b.hero) {
                for (Enemy& e : enemies_) {
                    if (e.hp <= 0 || e.hidden || dist(e.pos, b.pos) >= e.radius + b.radius) continue;
                    if (b.pierce) {
                        if (std::find(b.struck.begin(), b.struck.end(), e.id) != b.struck.end()) continue;
                        b.struck.push_back(e.id);
                        hurt_enemy(e, b.damage, normalized(b.vel) * b.knock);
                        continue;
                    }
                    hurt_enemy(e, b.damage, normalized(b.vel) * b.knock);
                    b.life = 0;
                    break;
                }
            } else if (dist(hero_.pos, b.pos) < hero_.radius + b.radius) {
                hurt_hero(b.damage);
                b.life = 0;
            }
        }
    }
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                  [](const Bullet& b) { return b.life <= 0; }),
                   bullets_.end());
    for (Enemy& e : spawned_) enemies_.push_back(e);   // a Кощей risen mid-volley
    spawned_.clear();
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                                  [](const Enemy& e) { return e.hp <= 0; }),
                   enemies_.end());

    // --- Ground that burns ---------------------------------------------------------
    for (Hazard& h : hazards_) {
        h.life -= kDt;
        if (dist(h.pos, hero_.pos) < h.radius + hero_.radius) hero_.burning = std::max(hero_.burning, 1.2f);
    }
    hazards_.erase(std::remove_if(hazards_.begin(), hazards_.end(),
                                  [](const Hazard& h) { return h.life <= 0; }),
                   hazards_.end());

    // --- A cleared room opens -----------------------------------------------------
    for (std::size_t r = 0; r < rooms_.size(); ++r) {
        if (!rooms_[r].locked) continue;
        bool guarded = false;
        for (const Enemy& e : enemies_)
            if (e.room == static_cast<int>(r)) { guarded = true; break; }
        if (!guarded) unlock_room(static_cast<int>(r));
    }

    // --- Pickups ---------------------------------------------------------------------
    for (Pickup& p : pickups_) {
        if (p.amount < 0 || p.kind == PickupKind::Weapon) continue;   // weapons wait for a hand
        const float d = dist(p.pos, hero_.pos);
        // Coins and energy drift to a hero who comes near: collecting them is
        // a reward, not a chore of walking over every one.
        if (p.kind != PickupKind::Potion && d < kPickupMagnet && d > 1e-3f)
            p.pos = p.pos + normalized(hero_.pos - p.pos) * std::min(d, 8.0f * kDt);
        if (dist(p.pos, hero_.pos) >= hero_.radius + 0.25f) continue;
        switch (p.kind) {
            case PickupKind::Coin:
                hero_.coins += p.amount;
                break;
            case PickupKind::Energy:
                if (hero_.energy >= hero_.max_energy) continue;
                hero_.energy = std::min(hero_.max_energy, hero_.energy + p.amount);
                break;
            case PickupKind::Potion: {
                // A potion is left lying for later if there is nothing to heal.
                if (hero_.hp >= hero_.max_hp) continue;
                const bool herbalist = hero_.cls == HeroClass::Znahar;
                p.amount = std::max(1, hero_.max_hp * 3 / 10) * (herbalist ? 2 : 1);
                hero_.hp = std::min(hero_.max_hp, hero_.hp + p.amount);
                hero_.burning = 0;
                if (herbalist) hero_.energy = std::min(hero_.max_energy, hero_.energy + 25);
                break;
            }
            case PickupKind::Weapon:
                break;
        }
        emit(EventKind::Pickup, p.pos, p.amount, static_cast<int>(p.kind));
        p.amount = -1;
    }
    pickups_.erase(std::remove_if(pickups_.begin(), pickups_.end(),
                                  [](const Pickup& p) { return p.amount < 0; }),
                   pickups_.end());

    // --- The way down ----------------------------------------------------------------
    if (!dead_ && hero_cell_ == exit_ && exit_open()) {
        if (depth_ >= kLastDepth) {
            won_ = true;
            emit(EventKind::Victory, hero_.pos);
        } else {
            emit(EventKind::Descended, hero_.pos, depth_ + 1);
            enter_floor(depth_ + 1);
        }
    }
}

}  // namespace rt
}  // namespace nav
