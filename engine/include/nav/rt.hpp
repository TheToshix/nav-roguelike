// SPDX-License-Identifier: MIT
//
// The real-time game.
//
// Everything in the turn-based engine assumes that the world waits for the
// hero: an action is a turn, energy decides who moves next, and a boss's
// warning arrives "one turn early". This is the other model — the one Soul
// Knight and its kind are built on — where the world runs on a clock and the
// player steers a hero through it continuously.
//
// It keeps the property the whole project stands on: no input and no output.
// The frontend hands in what the stick and the fire button are doing, calls
// `step` at a fixed sixty times a second, and reads the world back. That is
// what keeps it testable without a screen, and what lets one C++ core run in
// the browser and on a phone through the same WebAssembly build.
//
// What it borrows rather than rebuilds: the dungeon generator and its belts,
// the bestiary and its spawn weights, field of view, line of sight, and the
// Dijkstra flow field that already sends monsters after the hero.
//
// Generation stays deterministic — a seed still means a dungeon. The
// simulation uses floating point freely, so a replay of inputs is not promised
// to agree bit for bit across platforms. Nothing needs it to yet, and the
// promise that matters, the same floor for the same seed, is untouched.
#pragma once
#include <cstdint>
#include <vector>

#include "nav/data.hpp"
#include "nav/map.hpp"
#include "nav/pathfind.hpp"
#include "nav/rng.hpp"
#include "nav/types.hpp"

namespace nav {
namespace rt {

/// World units are cells: cell (x, y) covers [x, x+1) × [y, y+1).
struct V {
    float x{0}, y{0};
};
inline V operator+(V a, V b) { return {a.x + b.x, a.y + b.y}; }
inline V operator-(V a, V b) { return {a.x - b.x, a.y - b.y}; }
inline V operator*(V a, float k) { return {a.x * k, a.y * k}; }
float length(V v);
V normalized(V v);

/// One step of the simulation. Fixed, so that nothing about the rules
/// depends on how fast the device happens to draw.
inline constexpr float kDt = 1.0f / 60.0f;

// --- Tuning ------------------------------------------------------------------
//
// Every number that decides how the game feels lives here and nowhere else,
// so a pass of tuning is a diff of this block.
inline constexpr float kHeroSpeed = 4.8f;         ///< Cells per second.
inline constexpr float kHeroRadius = 0.30f;
inline constexpr float kWaterSlow = 0.6f;         ///< Speed multiplier in water.
inline constexpr float kFireInterval = 0.19f;     ///< Seconds between shots.
inline constexpr float kBulletSpeed = 13.0f;
inline constexpr float kBulletLife = 0.85f;
inline constexpr float kAutoAimRange = 9.0f;
inline constexpr float kInvulnerable = 0.55f;     ///< After a hit, before the next.
inline constexpr float kShieldDelay = 2.5f;       ///< Unhurt this long, the shield returns...
inline constexpr float kShieldTick = 0.3f;        ///< ...one point this often.
inline constexpr float kEnemySpeed = 2.3f;        ///< At the bestiary's speed of 100.
inline constexpr float kEnemyRadius = 0.34f;
inline constexpr float kEnemyBulletSpeed = 5.5f;
inline constexpr float kRangedRange = 4.5f;       ///< Where a thrower likes to circle.
inline constexpr float kMeleeCooldown = 0.9f;
inline constexpr float kPickupMagnet = 2.2f;
inline constexpr int kLastDepth = 16;

struct Input {
    float move_x{0}, move_y{0};   ///< Where the stick points; length at most 1.
    bool fire{false};             ///< Held, not tapped: the weapon repeats.
};

/// Things the frontend wants to hear about once — a hit, a death, a door
/// slamming — so it can shake, flash and spark without diffing two frames.
enum class EventKind : std::uint8_t {
    Shot,
    EnemyShot,
    BulletWall,
    EnemyHit,
    EnemyDied,
    HeroHit,
    ShieldHit,
    ShieldBroken,
    Pickup,
    RoomLocked,
    RoomCleared,
    Descended,
    HeroDied,
    Victory,
};

struct Event {
    EventKind kind{EventKind::Shot};
    float x{0}, y{0};
    int amount{0};
    int ref{0};   ///< An enemy id, a pickup kind — whatever the event is about.
};

struct Enemy {
    int id{0};          ///< Stable for the enemy's life, unlike its index.
    int species{0};
    V pos, knock, facing{1, 0};
    float radius{kEnemyRadius};
    int hp{1}, max_hp{1};
    float speed{kEnemySpeed};
    int damage{1};
    bool ranged{false};
    bool awake{false};
    float attack_cd{0}, fire_cd{0}, hurt{0}, strafe{1};
    int room{-1};       ///< The room it guards; -1 in caves and corridors.
};

struct Bullet {
    V pos, vel;
    float radius{0.12f};
    int damage{1};
    bool hero{true};
    float life{kBulletLife};
};

enum class PickupKind : std::uint8_t { Coin, Potion };

struct Pickup {
    int id{0};
    PickupKind kind{PickupKind::Coin};
    V pos;
    int amount{1};
};

struct Hero {
    HeroClass cls{HeroClass::Vityaz};
    V pos, facing{1, 0}, aim{1, 0};
    float radius{kHeroRadius};
    int hp{1}, max_hp{1};
    int shield{0}, max_shield{0};
    float shield_delay{0}, shield_tick{0};
    float fire_cd{0}, invulnerable{0};
    int damage{4};
    int coins{0}, kills{0};
    bool moving{false}, firing{false};
};

/// A room of the generated floor, and the doors that shut behind the hero.
struct Room {
    Rect interior;
    std::vector<Vec2> doorways;    ///< Walkable cells on the ring around it.
    std::vector<Tile> saved;       ///< What those cells were before the lock.
    bool locked{false};
    bool cleared{false};
};

class World {
public:
    void start(std::uint64_t seed, HeroClass cls = HeroClass::Vityaz);
    void step(const Input& in);

    const Map& map() const { return map_; }
    const Hero& hero() const { return hero_; }
    const std::vector<Enemy>& enemies() const { return enemies_; }
    const std::vector<Bullet>& bullets() const { return bullets_; }
    const std::vector<Pickup>& pickups() const { return pickups_; }
    const std::vector<Room>& rooms() const { return rooms_; }
    int depth() const { return depth_; }
    bool dead() const { return dead_; }
    bool won() const { return won_; }
    float time() const { return time_; }
    Vec2 exit() const { return exit_; }
    /// Bumps whenever the terrain or what the hero can see changes, so a
    /// frontend knows when its cached picture of the floor is stale.
    int map_revision() const { return map_revision_; }
    bool in_locked_room() const;

    /// Hands over everything that happened since the last call.
    std::vector<Event> take_events();

    // --- For tests ---------------------------------------------------------
    Hero& mutable_hero() { return hero_; }
    std::vector<Enemy>& mutable_enemies() { return enemies_; }
    std::vector<Pickup>& mutable_pickups() { return pickups_; }
    /// Replaces the floor with a hand-built one: a test can then ask exactly
    /// the question it means, instead of hunting a seed for the right room.
    void load_test_floor(const Map& map, V hero_pos, const std::vector<Rect>& rooms = {});
    /// Places one creature of `species`. The floor's own spawning goes
    /// through here too, so a test's enemy is built exactly like a real one.
    Enemy& add_enemy(int species, V pos, int room = -1);
    void enter_floor(int depth);

private:
    bool walk_blocked(Vec2 c) const;
    bool shot_blocked(Vec2 c) const;
    void move_circle(V& pos, V delta, float radius) const;
    void refresh_view();
    void build_rooms(const std::vector<Rect>& interiors);
    void populate();
    int room_of(Vec2 c) const;
    void lock_room(int r);
    void unlock_room(int r);
    void hurt_hero(int amount);
    void hurt_enemy(Enemy& e, int amount, V push);
    void kill_enemy(Enemy& e);
    const Enemy* pick_target() const;
    V steer(const Enemy& e) const;
    void emit(EventKind kind, V at, int amount = 0, int ref = 0);

    Rng rng_;
    Map map_;
    Hero hero_;
    std::vector<Enemy> enemies_;
    std::vector<Bullet> bullets_;
    std::vector<Pickup> pickups_;
    std::vector<Room> rooms_;
    std::vector<Event> events_;
    DijkstraMap flow_;
    Vec2 hero_cell_{-1, -1};
    Vec2 exit_{-1, -1};
    int depth_{1};
    int next_id_{1};
    int map_revision_{0};
    float time_{0};
    bool dead_{false};
    bool won_{false};
};

}  // namespace rt
}  // namespace nav
