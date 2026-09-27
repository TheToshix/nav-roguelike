// SPDX-License-Identifier: MIT
//
// The real-time game: one fixed step at a time. See rt.hpp for why it exists
// beside the turn-based engine rather than instead of it.
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

namespace {

Vec2 cell_of(V p) {
    return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y))};
}

V centre(Vec2 c) { return {static_cast<float>(c.x) + 0.5f, static_cast<float>(c.y) + 0.5f}; }

float dist(V a, V b) { return length(a - b); }

float frand(Rng& rng) { return static_cast<float>(rng.unit()); }

/// What never walks the floors in real time. Guardians need mechanics of their
/// own before they can be fought on a clock; the neutral and the skittish are
/// built around a turn-based dance of approach and flight; and a creature that
/// never leaves its cell is a turret, which wants its own design too.
constexpr std::uint32_t kNotSpawned = AiBoss | AiMiniBoss | AiNeutral | AiSkittish | AiStationary;

/// How far the hero sees. Enemies outside it are neither drawn nor aimed at.
constexpr int kSight = 9;

}  // namespace

void World::emit(EventKind kind, V at, int amount, int ref) {
    // A frontend drains this every frame. A test that never does must not be
    // able to grow it without bound.
    if (events_.size() >= 512) return;
    events_.push_back(Event{kind, at.x, at.y, amount, ref});
}

std::vector<Event> World::take_events() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

// --- Lifecycle -------------------------------------------------------------------

void World::start(std::uint64_t seed, HeroClass cls) {
    rng_ = Rng(seed);
    const ClassTemplate& tpl = class_info(cls);
    hero_ = Hero{};
    hero_.cls = cls;
    hero_.max_hp = hero_.hp = tpl.hp;
    // The shield is a third of the health and comes back on its own: the
    // thing that lets a player take a hit, back off, and try the room again
    // instead of dying of attrition to every scratch.
    hero_.max_shield = hero_.shield = std::max(4, tpl.hp / 3);
    hero_.damage = 2 + tpl.attack / 2;
    dead_ = won_ = false;
    time_ = 0;
    next_id_ = 1;
    events_.clear();
    enter_floor(1);
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
    bullets_.clear();
    pickups_.clear();

    Vec2 start = gen.entrance;
    if (walk_blocked(start)) {
        const auto cells = map_.walkable_cells();
        if (!cells.empty()) start = cells.front();
    }
    hero_.pos = centre(start);
    // A breath on arrival, to see the floor before anything on it can bite.
    hero_.invulnerable = 1.0f;

    build_rooms(gen.rooms);
    populate();
    hero_cell_ = {-1, -1};
    refresh_view();
}

void World::load_test_floor(const Map& map, V hero_pos, const std::vector<Rect>& rooms) {
    map_ = map;
    exit_ = {-1, -1};
    enemies_.clear();
    bullets_.clear();
    pickups_.clear();
    events_.clear();
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

Enemy& World::add_enemy(int species, V pos, int room) {
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
    enemies_.push_back(e);
    return enemies_.back();
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
    emit(EventKind::RoomCleared, centre(room.interior.center()), 0, r);
    hero_cell_ = {-1, -1};
    refresh_view();
}

// --- Harm ---------------------------------------------------------------------

void World::hurt_hero(int amount) {
    if (dead_ || hero_.invulnerable > 0 || amount <= 0) return;
    hero_.invulnerable = kInvulnerable;
    hero_.shield_delay = kShieldDelay;
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

void World::hurt_enemy(Enemy& e, int amount, V push) {
    if (e.hp <= 0) return;
    e.hp -= amount;
    e.hurt = 0.12f;
    e.awake = true;
    e.knock = e.knock + push * 3.5f;
    emit(EventKind::EnemyHit, e.pos, amount, e.id);
    if (e.hp <= 0) kill_enemy(e);
}

void World::kill_enemy(Enemy& e) {
    ++hero_.kills;
    // The species rides in `amount`, so the frontend can colour the burst by
    // the creature without keeping its own copy of who was where.
    emit(EventKind::EnemyDied, e.pos, e.species, e.id);
    if (rng_.chance(55))
        pickups_.push_back(Pickup{next_id_++, PickupKind::Coin, e.pos, 1 + depth_ / 4});
    if (rng_.chance(6))
        pickups_.push_back(Pickup{next_id_++, PickupKind::Potion, e.pos + V{0.25f, 0.1f}, 0});
}

// --- Decisions -----------------------------------------------------------------

const Enemy* World::pick_target() const {
    // Aim is automatic, as it is in the games this plays like: on a phone the
    // thumb that steers cannot also aim, and the choice that matters is where
    // to stand, not which pixel to point at.
    const Enemy* best = nullptr;
    float best_d = kAutoAimRange;
    for (const Enemy& e : enemies_) {
        if (e.hp <= 0) continue;
        const Vec2 ec = cell_of(e.pos);
        if (!map_.visible(ec)) continue;
        const float d = dist(e.pos, hero_.pos);
        if (d >= best_d) continue;
        if (!has_line_of_sight(map_, hero_cell_, ec, 16)) continue;
        best = &e;
        best_d = d;
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

// --- The clock -----------------------------------------------------------------

void World::step(const Input& in) {
    if (dead_ || won_) return;
    time_ += kDt;

    // --- The hero ----------------------------------------------------------
    V dir{in.move_x, in.move_y};
    const float dl = length(dir);
    if (dl > 1.0f) dir = dir * (1.0f / dl);
    hero_.moving = dl > 0.05f;
    if (hero_.moving) hero_.facing = normalized(dir);
    const bool wading = map_.at(cell_of(hero_.pos)) == Tile::Water;
    move_circle(hero_.pos, dir * (kHeroSpeed * (wading ? kWaterSlow : 1.0f) * kDt), hero_.radius);
    refresh_view();

    hero_.invulnerable = std::max(0.0f, hero_.invulnerable - kDt);
    if (hero_.shield_delay > 0) {
        hero_.shield_delay -= kDt;
    } else if (hero_.shield < hero_.max_shield) {
        hero_.shield_tick -= kDt;
        if (hero_.shield_tick <= 0) {
            ++hero_.shield;
            hero_.shield_tick = kShieldTick;
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

    // --- The weapon --------------------------------------------------------------
    hero_.fire_cd = std::max(0.0f, hero_.fire_cd - kDt);
    const Enemy* target = pick_target();
    hero_.aim = target ? normalized(target->pos - hero_.pos) : hero_.facing;
    hero_.firing = in.fire;
    if (in.fire && hero_.fire_cd <= 0) {
        hero_.fire_cd = kFireInterval;
        const V muzzle = hero_.pos + hero_.aim * 0.45f;
        bullets_.push_back(Bullet{muzzle, hero_.aim * kBulletSpeed, 0.12f, hero_.damage, true, kBulletLife});
        emit(EventKind::Shot, muzzle);
        if (target) hero_.facing = hero_.aim;   // turn to face what you shoot at
    }

    // --- Enemies -----------------------------------------------------------------
    for (Enemy& e : enemies_) {
        if (e.hp <= 0) continue;
        e.hurt = std::max(0.0f, e.hurt - kDt);
        e.attack_cd = std::max(0.0f, e.attack_cd - kDt);
        e.fire_cd -= kDt;
        e.knock = e.knock * std::max(0.0f, 1.0f - 10.0f * kDt);

        const Vec2 ec = cell_of(e.pos);
        const float d = dist(e.pos, hero_.pos);
        const bool sees = d < 12.0f && has_line_of_sight(map_, ec, hero_cell_, 16);
        if (!e.awake) {
            // A room's guards wake when its doors shut; anything outside a
            // room wakes when it sees the hero close by.
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
                // while it circles if the hero closes in and edging in if the
                // hero backs off. Circling at whatever distance it happened to
                // start from left it hovering at arm's length, which is where a
                // thrower has least reason to be.
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

        // A guard that chased the hero out of its room is no longer that
        // room's guard. Left counted, it would be shut outside the moment the
        // hero walked in — and a room waiting on a creature it has just locked
        // out never opens again.
        if (e.room >= 0) {
            const Room& own = rooms_[static_cast<std::size_t>(e.room)];
            if (!own.locked && !own.interior.contains(cell_of(e.pos))) e.room = -1;
        }

        if (!e.awake) continue;
        if (!e.ranged && d < hero_.radius + e.radius + 0.08f && e.attack_cd <= 0) {
            e.attack_cd = kMeleeCooldown;
            hurt_hero(e.damage);
        }
        if (e.ranged && sees && d < 10.0f && e.fire_cd <= 0) {
            e.fire_cd = 1.4f + frand(rng_) * 0.8f;
            if (rng_.chance(35)) e.strafe = -e.strafe;
            const V aim = normalized(hero_.pos - e.pos);
            bullets_.push_back(Bullet{e.pos + aim * 0.4f, aim * kEnemyBulletSpeed, 0.14f, e.damage, false, 2.6f});
            emit(EventKind::EnemyShot, e.pos, 0, e.id);
        }
    }

    // Enemies shoulder one another apart rather than stacking into one cell.
    for (std::size_t i = 0; i < enemies_.size(); ++i) {
        for (std::size_t j = i + 1; j < enemies_.size(); ++j) {
            Enemy& a = enemies_[i];
            Enemy& b = enemies_[j];
            if (!a.awake && !b.awake) continue;
            const V d = b.pos - a.pos;
            const float l = length(d);
            const float least = a.radius + b.radius;
            if (l >= least || l < 1e-4f) continue;
            const V push = d * ((least - l) / l * 0.5f);
            move_circle(a.pos, push * -1.0f, a.radius);
            move_circle(b.pos, push, b.radius);
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
                b.life = 0;
                break;
            }
            if (b.hero) {
                for (Enemy& e : enemies_) {
                    if (e.hp <= 0 || dist(e.pos, b.pos) >= e.radius + b.radius) continue;
                    hurt_enemy(e, b.damage, normalized(b.vel));
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
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                                  [](const Enemy& e) { return e.hp <= 0; }),
                   enemies_.end());

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
        const float d = dist(p.pos, hero_.pos);
        // Coins drift to a hero who comes near: collecting them is a reward,
        // not a chore of walking over every one.
        if (p.kind == PickupKind::Coin && d < kPickupMagnet && d > 1e-3f)
            p.pos = p.pos + normalized(hero_.pos - p.pos) * std::min(d, 8.0f * kDt);
        if (dist(p.pos, hero_.pos) >= hero_.radius + 0.25f) continue;
        if (p.kind == PickupKind::Coin) {
            hero_.coins += p.amount;
        } else {
            // A potion is left lying for later if there is nothing to heal.
            if (hero_.hp >= hero_.max_hp) continue;
            p.amount = std::max(1, hero_.max_hp * 3 / 10);
            hero_.hp = std::min(hero_.max_hp, hero_.hp + p.amount);
        }
        emit(EventKind::Pickup, p.pos, p.amount, static_cast<int>(p.kind));
        p.amount = -1;
    }
    pickups_.erase(std::remove_if(pickups_.begin(), pickups_.end(),
                                  [](const Pickup& p) { return p.amount < 0; }),
                   pickups_.end());

    // --- The way down ----------------------------------------------------------------
    if (!dead_ && hero_cell_ == exit_ && !in_locked_room()) {
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
