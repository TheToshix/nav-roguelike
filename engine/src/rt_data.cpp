// SPDX-License-Identifier: MIT
//
// Weapons and heroes for the real-time game.
//
// Kept apart from the rules for the same reason data.cpp is: a balance pass
// should be one diff of numbers, readable without the code around it.
#include <cstring>
#include <utility>

#include "nav/rt.hpp"

namespace nav {
namespace rt {
namespace {

WeaponSpec ranged(const char* key, Text name, int damage, float interval, int energy, float knock,
                  float speed, int pellets, float spread, float life, float radius, bool pierce) {
    WeaponSpec w{key, std::move(name), false, damage, interval, energy, knock};
    w.speed = speed;
    w.pellets = pellets;
    w.spread = spread;
    w.life = life;
    w.radius = radius;
    w.pierce = pierce;
    return w;
}

WeaponSpec melee(const char* key, Text name, int damage, float interval, int energy, float knock,
                 float reach, float arc) {
    WeaponSpec w{key, std::move(name), true, damage, interval, energy, knock};
    w.reach = reach;
    w.arc = arc;
    return w;
}

}  // namespace

const std::vector<WeaponSpec>& weapons() {
    // Damage per second is kept in a narrow band across the lot — roughly
    // twenty-five to forty — so that the choice between them is about how
    // they fight, not which number is biggest. The free ones sit at the low
    // end; the ones that cost energy buy something for it: reach, a pierce,
    // a spread that clears a crowd. Blades sit at the top of the band for
    // free, and pay in the other currency: standing within a guardian's reach.
    static const std::vector<WeaponSpec> table = {
        //        key          name                                          dmg  every  energy knock  speed  n  spread life  radius pierce
        ranged("crossbow",   Text{"Самострел", "Crossbow"},                   5, 0.19f,  0,  3.5f, 13.0f, 1, 0.04f, 0.85f, 0.12f, false),
        ranged("bow",        Text{"Лук", "Bow"},                              3, 0.11f,  0,  2.0f, 16.0f, 1, 0.10f, 0.70f, 0.10f, false),
        ranged("sling",      Text{"Праща", "Sling"},                          3, 0.42f,  1,  3.0f, 11.0f, 4, 0.60f, 0.55f, 0.11f, false),
        ranged("staff",      Text{"Посох", "Staff"},                          9, 0.45f,  3,  2.0f,  8.0f, 1, 0.00f, 1.30f, 0.22f, true),
        ranged("ember",      Text{"Жар-посох", "Ember staff"},                4, 0.13f,  1,  1.5f, 10.0f, 2, 0.22f, 0.90f, 0.14f, false),
        ranged("thunder",    Text{"Громовой самострел", "Thunder crossbow"}, 14, 0.55f,  4,  6.0f, 20.0f, 1, 0.00f, 0.90f, 0.14f, true),
        //        key          name                                          dmg  every  energy knock  reach  arc
        melee("sword",       Text{"Меч", "Sword"},                           11, 0.30f,  0,  6.0f,  1.40f, 1.90f),
        melee("axe",         Text{"Секира", "Axe"},                          22, 0.60f,  0,  9.0f,  1.55f, 2.40f),
        melee("flail",       Text{"Кистень", "Flail"},                       14, 0.45f,  1,  7.0f,  1.35f, 6.2832f),
    };
    return table;
}

int weapon_index(const char* key) {
    const auto& t = weapons();
    for (std::size_t i = 0; i < t.size(); ++i)
        if (std::strcmp(t[i].key, key) == 0) return static_cast<int>(i);
    return -1;
}

const std::vector<HeroSpec>& heroes() {
    // Each class keeps the one thing its turn-based lore promises — the
    // herbalist's potions, the smith's edge, the champion's sweep — turned
    // into something that matters in a fight on a clock.
    static const std::vector<HeroSpec> table = {
        {HeroClass::Vityaz,  34, 12, 120, 1.00f, "crossbow",
         Text{"Щит возвращается вдвое быстрее", "The shield comes back twice as fast"}},
        {HeroClass::Vedun,   20,  6, 220, 1.00f, "staff",
         Text{"Сила копится сама", "Power gathers on its own"}},
        {HeroClass::Tat,     24,  8, 150, 1.15f, "bow",
         Text{"Быстрее всех, монет в полтора раза больше", "Fastest of all, and half again the coin"}},
        {HeroClass::Znahar,  26,  8, 160, 1.00f, "sling",
         Text{"Зелья лечат вдвое и возвращают силу", "Potions heal twice over and restore power"}},
        {HeroClass::Kuznets, 30, 12, 120, 0.95f, "axe",
         Text{"Любое оружие бьёт на четверть сильнее", "Any weapon hits a quarter harder"}},
        {HeroClass::Bogatyr, 42, 14, 100, 0.90f, "sword",
         Text{"Удар в полтора раза шире", "A swing half again as wide"}},
    };
    return table;
}

const HeroSpec& hero_spec(HeroClass cls) {
    for (const HeroSpec& h : heroes())
        if (h.cls == cls) return h;
    return heroes().front();
}

}  // namespace rt
}  // namespace nav
