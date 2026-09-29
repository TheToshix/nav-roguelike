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
//
// The file is split three ways: rt.cpp is the clock and everything ordinary,
// rt_data.cpp the weapons and heroes, rt_bosses.cpp the eight guardians.
#pragma once
#include <cstdint>
#include <vector>

#include "nav/data.hpp"
#include "nav/map.hpp"
#include "nav/pathfind.hpp"
#include "nav/rng.hpp"
#include "nav/text.hpp"
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
V rotated(V v, float radians);

/// One step of the simulation. Fixed, so that nothing about the rules
/// depends on how fast the device happens to draw.
inline constexpr float kDt = 1.0f / 60.0f;

// --- Tuning ------------------------------------------------------------------
//
// Every number that decides how the game feels lives here or in the weapon
// and hero tables in rt_data.cpp, so a pass of tuning is a diff of those.
inline constexpr float kHeroSpeed = 4.8f;         ///< Cells per second.
inline constexpr float kHeroRadius = 0.30f;
inline constexpr float kWaterSlow = 0.6f;         ///< Speed multiplier in water.
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
inline constexpr float kPickupReach = 0.8f;       ///< How close "standing on it" is.
inline constexpr int kLastDepth = 16;

// Kept for the tests written against the first cut, which had one weapon.
inline constexpr float kFireInterval = 0.19f;
inline constexpr float kBulletSpeed = 13.0f;
inline constexpr float kBulletLife = 0.85f;

struct Input {
    float move_x{0}, move_y{0};   ///< Where the stick points; length at most 1.
    bool fire{false};             ///< Held, not tapped: the weapon repeats.
    /// Pressed, not held: true for exactly one step. Takes the weapon lying
    /// underfoot, or swaps hands when there is nothing there.
    bool interact{false};
};

// --- Weapons and heroes (rt_data.cpp) --------------------------------------------

struct WeaponSpec {
    const char* key;      ///< Stable name the frontend draws and labels by.
    Text name;
    bool melee;
    int damage;
    float interval;       ///< Seconds between shots or swings.
    int energy;           ///< Spent per shot; 0 is free.
    float knock;          ///< How hard a hit pushes.
    // Ranged.
    float speed{0};
    int pellets{1};
    float spread{0};      ///< Total fan, radians.
    float life{0};        ///< Seconds a bolt flies.
    float radius{0.12f};
    bool pierce{false};   ///< Flies on through what it hits.
    // Melee.
    float reach{0};
    float arc{0};         ///< Total sweep, radians.
};

const std::vector<WeaponSpec>& weapons();
int weapon_index(const char* key);

/// How a class plays in real time. The turn-based ClassTemplate numbers
/// describe a different game; these are chosen for this one, and each class
/// keeps the one thing its lore promises.
struct HeroSpec {
    HeroClass cls;
    int hp, shield, energy;
    float speed;          ///< Multiplier on kHeroSpeed.
    const char* weapon;   ///< Starting weapon key.
    Text perk;            ///< One line, for the choosing screen.
};

const std::vector<HeroSpec>& heroes();
const HeroSpec& hero_spec(HeroClass cls);

// --- Events ----------------------------------------------------------------------

/// Things the frontend wants to hear about once — a hit, a death, a door
/// slamming — so it can shake, flash and spark without diffing two frames.
/// New kinds go at the end: the page numbers them.
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
    Swing,          ///< amount: aim in degrees; ref: weapon index.
    NoEnergy,
    WeaponTaken,    ///< ref: weapon index.
    WeaponSwapped,  ///< ref: weapon index now in hand.
    BossAppears,    ///< ref: species.
    BossPhase,      ///< amount: the new phase.
    BossDied,       ///< ref: species.
    GazeHit,
    GazeMissed,
    Frozen,
    Revived,        ///< Кощей rose again.
    NeedleShown,
    NeedleBroken,
    Burrowed,
    Emerged,
    HutFell,        ///< amount: huts left standing.
    Shielded,       ///< A hit on a guarded boss, soaked by what guards it.
};

struct Event {
    EventKind kind{EventKind::Shot};
    float x{0}, y{0};
    int amount{0};
    int ref{0};   ///< An enemy id, a pickup kind — whatever the event is about.
    float a{0}, b{0};   ///< A swing's aim and sweep, in radians.
};

/// A warning drawn on the ground before a guardian's attack lands. Every
/// dangerous thing a guardian does is announced by one of these first — the
/// player is owed the chance to read it.
enum class TelegraphKind : std::uint8_t {
    Ring,     ///< Circle at (x, y), radius a.
    Line,     ///< From (x, y) to (a, b): a gaze, a charge.
    Spot,     ///< Circle on the ground at (x, y), radius a: something is coming up here.
    Cone,     ///< From (x, y), aimed at angle a, half-width b.
};

struct Telegraph {
    TelegraphKind kind{TelegraphKind::Ring};
    float x{0}, y{0}, a{0}, b{0};
    float progress{0};   ///< 0 as the warning starts, 1 as the attack lands.
};

// --- Bodies ----------------------------------------------------------------------

enum class Role : std::uint8_t { Normal, Boss, Hut, Needle, Illusion };

struct Enemy {
    int id{0};          ///< Stable for the enemy's life, unlike its index.
    int species{0};
    Role role{Role::Normal};
    V pos, knock, facing{1, 0};
    float radius{kEnemyRadius};
    int hp{1}, max_hp{1};
    float speed{kEnemySpeed};
    int damage{1};
    bool ranged{false};
    bool awake{false};
    float attack_cd{0}, fire_cd{0}, hurt{0}, strafe{1};
    int room{-1};       ///< The room it guards; -1 in caves and corridors.
    // Guardians.
    int phase{1}, phases{1};
    int state{0};
    float timer{0}, timer2{0};
    float armour{1.0f};   ///< Damage taken is multiplied by this.
    bool hidden{false};   ///< Under the ground, or not yet found: not drawn, not hit.
    int revives{0};
    V target;
    float angle{0};       ///< Where a breath is aimed, locked as it is announced.
    int owner{0};         ///< For huts' children and illusions: whose they are.
};

struct Bullet {
    V pos, vel;
    float radius{0.12f};
    int damage{1};
    bool hero{true};
    float life{kBulletLife};
    bool pierce{false};
    bool ember{false};   ///< Leaves fire where it lands.
    float knock{3.5f};
    std::vector<int> struck;   ///< What a piercing bolt has already hit.
};

enum class PickupKind : std::uint8_t { Coin, Potion, Energy, Weapon };

struct Pickup {
    int id{0};
    PickupKind kind{PickupKind::Coin};
    V pos;
    int amount{1};   ///< Coins, energy — or, for a weapon, its index.
};

/// Ground that hurts: the fire a guardian's breath leaves behind.
struct Hazard {
    V pos;
    float radius{0.6f};
    float life{3.0f};
};

struct Hero {
    HeroClass cls{HeroClass::Vityaz};
    V pos, facing{1, 0}, aim{1, 0};
    float radius{kHeroRadius};
    int hp{1}, max_hp{1};
    int shield{0}, max_shield{0};
    int energy{0}, max_energy{0};
    float shield_delay{0}, shield_tick{0}, energy_tick{0};
    float fire_cd{0}, invulnerable{0};
    int damage{4};   ///< Unused since weapons carry their own; kept for old tests.
    int weapons[2]{-1, -1};
    int hand{0};     ///< Which of the two is in use.
    float frozen{0}, burning{0}, blind{0}, burn_tick{0};
    int coins{0}, kills{0};
    bool moving{false}, firing{false};

    int weapon() const { return weapons[hand]; }
};

/// A room of the generated floor, and the doors that shut behind the hero.
struct Room {
    Rect interior;
    std::vector<Vec2> doorways;    ///< Walkable cells on the ring around it.
    std::vector<Tile> saved;       ///< What those cells were before the lock.
    bool locked{false};
    bool cleared{false};
    bool boss{false};
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
    const std::vector<Telegraph>& telegraphs() const { return telegraphs_; }
    const std::vector<Hazard>& hazards() const { return hazards_; }
    int depth() const { return depth_; }
    bool dead() const { return dead_; }
    bool won() const { return won_; }
    float time() const { return time_; }
    Vec2 exit() const { return exit_; }
    /// Bumps whenever the terrain or what the hero can see changes, so a
    /// frontend knows when its cached picture of the floor is stale.
    int map_revision() const { return map_revision_; }
    bool in_locked_room() const;
    /// The floor's guardian, while it lives; null otherwise.
    const Enemy* boss() const;
    /// Whether the stairs will take the hero: not while a room is shut, and
    /// not while the floor's guardian lives.
    bool exit_open() const;

    /// Hands over everything that happened since the last call.
    std::vector<Event> take_events();

    // --- For tests, and for the page's floor jump ------------------------------
    Hero& mutable_hero() { return hero_; }
    std::vector<Enemy>& mutable_enemies() { return enemies_; }
    std::vector<Pickup>& mutable_pickups() { return pickups_; }
    std::vector<Bullet>& mutable_bullets() { return bullets_; }
    /// Replaces the floor with a hand-built one: a test can then ask exactly
    /// the question it means, instead of hunting a seed for the right room.
    void load_test_floor(const Map& map, V hero_pos, const std::vector<Rect>& rooms = {});
    /// Places one creature of `species`. The floor's own spawning goes
    /// through here too, so a test's enemy is built exactly like a real one.
    Enemy& add_enemy(int species, V pos, int room = -1);
    /// Places a guardian, with whatever it brings — huts, a needle.
    Enemy& add_boss(int species, V pos, int room = -1);
    void enter_floor(int depth);
    /// Puts `weapon` in the hand, as if it had been picked up.
    void give_weapon(int weapon);

private:
    bool walk_blocked(Vec2 c) const;
    bool shot_blocked(Vec2 c) const;
    void move_circle(V& pos, V delta, float radius) const;
    void refresh_view();
    void build_rooms(const std::vector<Rect>& interiors);
    void populate();
    void place_boss();
    int room_of(Vec2 c) const;
    void lock_room(int r);
    void unlock_room(int r);
    void hurt_hero(int amount);
    void hurt_enemy(Enemy& e, int amount, V push);
    void kill_enemy(Enemy& e);
    const Enemy* pick_target() const;
    V steer(const Enemy& e) const;
    void emit(EventKind kind, V at, int amount = 0, int ref = 0, float a = 0, float b = 0);
    Enemy make_enemy(int species, V pos, int room);
    Enemy& spawn_now(int species, V pos, int owner);
    void burn_hero();
    void use_weapon(const Enemy* target);
    void interact();
    void drop(PickupKind kind, V at, int amount);
    void think_ordinary(Enemy& e, float d, bool sees);
    void think_boss(Enemy& e, float d, bool sees);
    void shoot(V from, V dir, float speed, int damage, float radius = 0.14f);
    void ring(V from, int count, float speed, int damage, float offset = 0);
    bool sees_hero_from(V from) const;

    Rng rng_;
    Map map_;
    Hero hero_;
    std::vector<Enemy> enemies_;
    std::vector<Enemy> spawned_;   ///< Summoned mid-step; joined after the loop.
    std::vector<Bullet> bullets_;
    std::vector<Pickup> pickups_;
    std::vector<Room> rooms_;
    std::vector<Event> events_;
    std::vector<Telegraph> telegraphs_;
    std::vector<Hazard> hazards_;
    DijkstraMap flow_;
    Vec2 hero_cell_{-1, -1};
    Vec2 exit_{-1, -1};
    int depth_{1};
    int next_id_{1};
    int map_revision_{0};
    int boss_id_{0};
    float time_{0};
    bool dead_{false};
    bool won_{false};
};

}  // namespace rt
}  // namespace nav
