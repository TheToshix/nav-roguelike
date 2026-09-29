// SPDX-License-Identifier: MIT
//
// Heroes and weapons in the real-time game.
#include <gtest/gtest.h>

#include <cmath>
#include <set>

#include "nav/data.hpp"
#include "nav/entity.hpp"
#include "nav/rt.hpp"

namespace nav {
namespace {

using rt::EventKind;
using rt::Input;
using rt::V;
using rt::World;

Map open_room() {
    Map m(20, 13);
    for (int y = 1; y <= 11; ++y)
        for (int x = 1; x <= 18; ++x) m.set({x, y}, Tile::Floor);
    return m;
}

World hero_in_room(HeroClass cls, V at = {10.5f, 6.5f}) {
    World w;
    w.start(1, cls);
    w.load_test_floor(open_room(), at);
    return w;
}

int count(const std::vector<rt::Event>& events, EventKind kind) {
    int n = 0;
    for (const auto& e : events)
        if (e.kind == kind) ++n;
    return n;
}

int upyr() { return species_index("upyr"); }

// --- Heroes ------------------------------------------------------------------------

TEST(RealtimeHeroes, EachStartsWithTheirOwnWeaponAndBody) {
    std::set<int> starting;
    for (const rt::HeroSpec& spec : rt::heroes()) {
        World w;
        w.start(1, spec.cls);
        EXPECT_EQ(w.hero().max_hp, spec.hp);
        EXPECT_EQ(w.hero().max_shield, spec.shield);
        EXPECT_EQ(w.hero().max_energy, spec.energy);
        ASSERT_GE(rt::weapon_index(spec.weapon), 0) << spec.weapon << " is not a weapon";
        EXPECT_EQ(w.hero().weapon(), rt::weapon_index(spec.weapon));
        EXPECT_EQ(w.hero().weapons[1], -1) << "the second hand starts empty";
        starting.insert(w.hero().weapon());
    }
    EXPECT_EQ(starting.size(), rt::heroes().size()) << "two classes set out with the same weapon";
}

TEST(RealtimeHeroes, EveryClassHasAPerkLine) {
    for (const rt::HeroSpec& spec : rt::heroes()) {
        EXPECT_FALSE(spec.perk.ru.empty());
        EXPECT_FALSE(spec.perk.en.empty());
    }
}

TEST(RealtimeHeroes, TheThiefOutrunsTheKnight) {
    World tat = hero_in_room(HeroClass::Tat, {3.5f, 6.5f});
    World vityaz = hero_in_room(HeroClass::Vityaz, {3.5f, 6.5f});
    for (int i = 0; i < 40; ++i) {
        tat.step(Input{1, 0, false});
        vityaz.step(Input{1, 0, false});
    }
    EXPECT_GT(tat.hero().pos.x, vityaz.hero().pos.x + 0.2f);
}

TEST(RealtimeHeroes, TheSorcerersPowerGathersOnItsOwn) {
    World w = hero_in_room(HeroClass::Vedun);
    w.mutable_hero().energy = 0;
    for (int i = 0; i < 120; ++i) w.step(Input{});
    EXPECT_GE(w.hero().energy, 5);

    World knight = hero_in_room(HeroClass::Vityaz);
    knight.mutable_hero().energy = 0;
    for (int i = 0; i < 120; ++i) knight.step(Input{});
    EXPECT_EQ(knight.hero().energy, 0) << "only the sorcerer's power comes back by itself";
}

TEST(RealtimeHeroes, TheSmithHitsAQuarterHarder) {
    const auto first_hit = [](HeroClass cls) {
        World w = hero_in_room(cls);
        w.give_weapon(rt::weapon_index("crossbow"));
        w.add_enemy(upyr(), {13.5f, 6.5f});
        for (int t = 0; t < 120; ++t) {
            w.step(Input{0, 0, true});
            for (const auto& e : w.take_events())
                if (e.kind == EventKind::EnemyHit) return e.amount;
        }
        return -1;
    };
    const int knight = first_hit(HeroClass::Vityaz);
    const int smith = first_hit(HeroClass::Kuznets);
    ASSERT_GT(knight, 0);
    EXPECT_GT(smith, knight);
}

TEST(RealtimeHeroes, TheChampionsSwingIsWider) {
    // An enemy seventy degrees off the line of the swing: outside a sword's
    // sweep in the knight's hand, inside it in the champion's.
    const auto reaches = [](HeroClass cls) {
        World w = hero_in_room(cls);
        w.give_weapon(rt::weapon_index("sword"));
        w.add_enemy(upyr(), {10.5f + 0.9f, 6.5f});   // straight ahead: this is what is aimed at
        const float a = 70.0f * 3.14159265f / 180.0f;
        rt::Enemy& side = w.add_enemy(upyr(), {10.5f + std::cos(a) * 1.25f, 6.5f + std::sin(a) * 1.25f});
        const int side_id = side.id;
        w.step(Input{0, 0, true});
        for (const auto& e : w.enemies())
            if (e.id == side_id) return e.hp < e.max_hp;
        return true;   // killed outright counts as reached
    };
    EXPECT_FALSE(reaches(HeroClass::Vityaz));
    EXPECT_TRUE(reaches(HeroClass::Bogatyr));
}

TEST(RealtimeHeroes, TheHerbalistsPotionsHealTwiceAndGivePower) {
    const auto healed = [](HeroClass cls) {
        World w = hero_in_room(cls);
        w.mutable_hero().hp = 2;
        w.mutable_hero().energy = 0;
        w.mutable_pickups().push_back(rt::Pickup{900, rt::PickupKind::Potion, w.hero().pos, 0});
        w.step(Input{});
        return std::make_pair(w.hero().hp - 2, w.hero().energy);
    };
    const auto knight = healed(HeroClass::Vityaz);
    const auto herbalist = healed(HeroClass::Znahar);
    EXPECT_GT(herbalist.first, knight.first);
    EXPECT_GT(herbalist.second, 0);
    EXPECT_EQ(knight.second, 0);
}

TEST(RealtimeHeroes, TheKnightsShieldComesBackSooner) {
    const auto back_after = [](HeroClass cls) {
        World w = hero_in_room(cls, {5.5f, 6.5f});
        w.add_enemy(upyr(), {6.1f, 6.5f}).awake = true;
        w.step(Input{});
        w.mutable_enemies().clear();
        const int after_blow = w.hero().shield;
        for (int t = 1; t < 600; ++t) {
            w.step(Input{});
            if (w.hero().shield > after_blow) return t;
        }
        return 9999;
    };
    EXPECT_LT(back_after(HeroClass::Vityaz), back_after(HeroClass::Kuznets));
}

// --- Weapons ------------------------------------------------------------------------

TEST(RealtimeWeapons, EveryWeaponKillsWhatItIsPointedAt) {
    for (int wi = 0; wi < static_cast<int>(rt::weapons().size()); ++wi) {
        const rt::WeaponSpec& spec = rt::weapons()[static_cast<std::size_t>(wi)];
        World w = hero_in_room(HeroClass::Vityaz);
        w.give_weapon(wi);
        w.mutable_hero().energy = w.mutable_hero().max_energy = 999;
        w.add_enemy(upyr(), {10.5f + (spec.melee ? 1.0f : 4.0f), 6.5f});   // asleep: it stands still
        bool died = false;
        for (int t = 0; t < 900 && !died; ++t) {
            w.step(Input{0, 0, true});
            died = count(w.take_events(), EventKind::EnemyDied) > 0;
        }
        EXPECT_TRUE(died) << spec.key << " never killed a standing target";
    }
}

TEST(RealtimeWeapons, AnEnergyWeaponSpendsItAndStopsWhenItRunsOut) {
    World w = hero_in_room(HeroClass::Vityaz);
    const int staff = rt::weapon_index("staff");
    w.give_weapon(staff);
    const int cost = rt::weapons()[static_cast<std::size_t>(staff)].energy;
    ASSERT_GT(cost, 0);
    w.mutable_hero().energy = cost * 2;
    int shots = 0, complaints = 0;
    for (int t = 0; t < 240; ++t) {
        w.step(Input{0, 0, true});
        const auto events = w.take_events();
        shots += count(events, EventKind::Shot);
        complaints += count(events, EventKind::NoEnergy);
    }
    EXPECT_EQ(shots, 2);
    EXPECT_EQ(w.hero().energy, 0);
    EXPECT_GT(complaints, 0) << "an empty weapon said nothing";
    EXPECT_LT(complaints, 20) << "an empty weapon complained every tick";
}

TEST(RealtimeWeapons, TheSlingThrowsAFan) {
    World w = hero_in_room(HeroClass::Znahar);
    w.step(Input{0, 0, true});
    const int pellets = rt::weapons()[static_cast<std::size_t>(rt::weapon_index("sling"))].pellets;
    ASSERT_EQ(static_cast<int>(w.bullets().size()), pellets);
    std::set<int> headings;
    for (const auto& b : w.bullets())
        headings.insert(static_cast<int>(std::lround(std::atan2(b.vel.y, b.vel.x) * 100)));
    EXPECT_EQ(static_cast<int>(headings.size()), pellets) << "the pellets fly the same way";
}

TEST(RealtimeWeapons, TheStaffGoesThroughALine) {
    World w = hero_in_room(HeroClass::Vedun, {3.5f, 6.5f});
    // Rooted to the spot, or the second steps round the first and off the
    // line; ids taken at once, since adding the second may move the first.
    rt::Enemy& first = w.add_enemy(upyr(), {6.5f, 6.5f});
    first.speed = 0;
    const int ida = first.id;
    rt::Enemy& second = w.add_enemy(upyr(), {8.5f, 6.5f});
    second.speed = 0;
    const int idb = second.id;
    w.step(Input{0, 0, true});
    for (int t = 0; t < 60; ++t) w.step(Input{});   // let the one bolt fly
    // Counted from the blows, not from what is left standing: one bolt may
    // well have killed the first.
    bool hit_a = false, hit_b = false;
    for (const auto& ev : w.take_events()) {
        if (ev.kind != EventKind::EnemyHit) continue;
        hit_a = hit_a || ev.ref == ida;
        hit_b = hit_b || ev.ref == idb;
    }
    EXPECT_TRUE(hit_a);
    EXPECT_TRUE(hit_b) << "a piercing bolt stopped at the first thing it met";
}

TEST(RealtimeWeapons, ABladeHitsWhatIsInFrontAndNotWhatIsBehind) {
    World w = hero_in_room(HeroClass::Bogatyr);
    const int idf = w.add_enemy(upyr(), {11.4f, 6.5f}).id;
    const int idb = w.add_enemy(upyr(), {9.3f, 6.5f}).id;
    w.step(Input{0, 0, true});
    for (const auto& e : w.enemies()) {
        if (e.id == idf) EXPECT_LT(e.hp, e.max_hp);
        if (e.id == idb) EXPECT_EQ(e.hp, e.max_hp) << "the swing reached behind the hero";
    }
}

TEST(RealtimeWeapons, ABladeCutsBoltsOutOfTheAir) {
    World w = hero_in_room(HeroClass::Bogatyr);
    rt::Bullet stone;
    stone.pos = {11.3f, 6.5f};
    stone.vel = {-5.0f, 0};
    stone.hero = false;
    stone.damage = 5;
    stone.life = 2.0f;
    w.mutable_bullets().push_back(stone);
    const int shield = w.hero().shield;
    w.step(Input{0, 0, true});   // facing right, into the stone
    EXPECT_TRUE(w.bullets().empty());
    EXPECT_EQ(w.hero().shield, shield);
}

// --- Picking up and swapping ---------------------------------------------------------

TEST(RealtimeWeapons, AWeaponUnderfootGoesIntoTheEmptyHand) {
    World w = hero_in_room(HeroClass::Vityaz);
    const int axe = rt::weapon_index("axe");
    w.mutable_pickups().push_back(rt::Pickup{900, rt::PickupKind::Weapon, w.hero().pos, axe});
    w.step(Input{0, 0, false, true});
    EXPECT_EQ(w.hero().weapons[1], axe);
    EXPECT_EQ(w.hero().hand, 1);
    EXPECT_TRUE(w.pickups().empty());
}

TEST(RealtimeWeapons, WithBothHandsFullTheOldWeaponIsLeftLying) {
    World w = hero_in_room(HeroClass::Vityaz);
    const int axe = rt::weapon_index("axe"), bow = rt::weapon_index("bow");
    w.give_weapon(axe);   // second hand, now in use
    ASSERT_EQ(w.hero().weapon(), axe);
    w.mutable_pickups().push_back(rt::Pickup{900, rt::PickupKind::Weapon, w.hero().pos, bow});
    w.step(Input{0, 0, false, true});
    EXPECT_EQ(w.hero().weapon(), bow);
    ASSERT_EQ(w.pickups().size(), 1u);
    EXPECT_EQ(w.pickups()[0].amount, axe) << "the weapon put down vanished";
}

TEST(RealtimeWeapons, WithNothingUnderfootTheHandsSwap) {
    World w = hero_in_room(HeroClass::Vityaz);
    const int first = w.hero().weapon();
    w.give_weapon(rt::weapon_index("sword"));
    ASSERT_EQ(w.hero().hand, 1);
    w.step(Input{0, 0, false, true});
    EXPECT_EQ(w.hero().hand, 0);
    EXPECT_EQ(w.hero().weapon(), first);
}

TEST(RealtimeWeapons, AWeaponIsNotPickedUpJustByWalkingOverIt) {
    // Taking one is a choice: walking across a worse weapon must not cost the
    // player the one in their hand.
    World w = hero_in_room(HeroClass::Vityaz);
    w.mutable_pickups().push_back(rt::Pickup{900, rt::PickupKind::Weapon, w.hero().pos, rt::weapon_index("bow")});
    for (int t = 0; t < 30; ++t) w.step(Input{});
    EXPECT_EQ(w.hero().weapons[1], -1);
    EXPECT_EQ(w.pickups().size(), 1u);
}

}  // namespace
}  // namespace nav
