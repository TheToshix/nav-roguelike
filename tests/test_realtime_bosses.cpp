// SPDX-License-Identifier: MIT
//
// The eight guardians, on a clock.
//
// Two kinds of test. The small ones each pin one mechanic and its answer —
// the gaze that a wall stops, the needle that ends the rising. The big one
// fights every guardian to the death with a scripted hero, because the worst
// thing a boss can be is unkillable, and nothing short of fighting it proves
// it is not.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "nav/data.hpp"
#include "nav/entity.hpp"
#include "nav/rt.hpp"

namespace nav {
namespace {

using rt::EventKind;
using rt::Input;
using rt::Role;
using rt::V;
using rt::World;

int count(const std::vector<rt::Event>& events, EventKind kind) {
    int n = 0;
    for (const auto& e : events)
        if (e.kind == kind) ++n;
    return n;
}

Map open_room(int w = 22, int h = 13) {
    Map m(w, h);
    for (int y = 1; y < h - 1; ++y)
        for (int x = 1; x < w - 1; ++x) m.set({x, y}, Tile::Floor);
    return m;
}

World with_boss(const char* key, V boss_at, V hero_at, Map map = open_room()) {
    World w;
    w.start(1);
    w.load_test_floor(map, hero_at);
    rt::Enemy& b = w.add_boss(species_index(key), boss_at);
    b.awake = true;
    return w;
}

rt::Enemy* find(World& w, Role role) {
    for (auto& e : w.mutable_enemies())
        if (e.role == role && e.hp > 0) return &e;
    return nullptr;
}

/// A spot for the hero near a guardian: open ground, off the stairs, and —
/// in a hall — inside it and clear of every doorway. A doorway matters: the
/// doors will not shut on a hero standing in one, and a hall's guardian does
/// not wake until they do.
V stand_near(const World& w, const rt::Enemy& boss) {
    const Map& m = w.map();
    const auto open = [&](Vec2 c) {
        if (!m.in_bounds(c) || c == w.exit()) return false;
        const Tile t = m.at(c);
        return t != Tile::Wall && t != Tile::Chasm && t != Tile::Door;
    };
    Rect area{static_cast<int>(boss.pos.x) - 6, static_cast<int>(boss.pos.y) - 6, 13, 13};
    if (boss.room >= 0) {
        const Rect in = w.rooms()[static_cast<std::size_t>(boss.room)].interior;
        area = Rect{in.left() + 1, in.top() + 1, in.w - 2, in.h - 2};
    }
    const V want = boss.pos + V{2.5f, 0};
    V best = boss.pos;
    float best_d = 1e9f;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
            if (!open({x, y})) continue;
            const V c{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            if (rt::length(c - boss.pos) < 1.5f) continue;
            const float d = rt::length(c - want);
            if (d < best_d) { best_d = d; best = c; }
        }
    return best;
}

std::vector<int> guardian_floors() {
    std::vector<int> out;
    for (int d = 1; d <= rt::kLastDepth; ++d)
        if (boss_for_depth(d)) out.push_back(d);
    return out;
}

// --- Where they are ---------------------------------------------------------------

TEST(RealtimeBosses, EveryGuardianWaitsOnItsOwnFloor) {
    ASSERT_EQ(guardian_floors().size(), 8u);
    for (int depth : guardian_floors()) {
        World w;
        w.start(3);
        w.enter_floor(depth);
        const rt::Enemy* boss = w.boss();
        ASSERT_NE(boss, nullptr) << "floor " << depth << " has no guardian";
        EXPECT_EQ(boss->species, species_index(boss_for_depth(depth))) << "floor " << depth;
        EXPECT_FALSE(w.exit_open()) << "floor " << depth << ": the stairs are open with the guardian alive";
        if (boss->room >= 0) {
            const auto& room = w.rooms()[static_cast<std::size_t>(boss->room)];
            EXPECT_TRUE(room.boss);
            EXPECT_TRUE(room.interior.contains(w.exit())) << "floor " << depth << ": the guardian is not guarding the way down";
        }
    }
}

TEST(RealtimeBosses, OrdinaryFloorsHaveNone) {
    World w;
    w.start(3);
    w.enter_floor(2);
    EXPECT_EQ(w.boss(), nullptr);
    EXPECT_TRUE(w.exit_open());
}

TEST(RealtimeBosses, TheStairsWaitForTheGuardianToFall) {
    World w;
    w.start(5);
    w.enter_floor(4);
    const Vec2 exit = w.exit();
    w.mutable_hero().pos = {static_cast<float>(exit.x) + 0.5f, static_cast<float>(exit.y) + 0.5f};
    w.step(Input{});
    EXPECT_EQ(w.depth(), 4) << "went down past a living guardian";
}

// --- Every guardian dies ---------------------------------------------------------

/// A scripted hero who cannot die: circles, keeps the trigger down, and — the
/// one piece of knowledge the fight demands — walks towards Кощей's needle
/// once it shows, near enough for the sights to find it. A guardian this cannot kill in four minutes of game time is broken.
void fight_to_the_death(int depth) {
    World w;
    w.start(11, HeroClass::Vityaz);
    w.enter_floor(depth);
    w.give_weapon(rt::weapon_index("thunder"));
    const rt::Enemy* start = w.boss();
    ASSERT_NE(start, nullptr);
    const int boss_species = start->species;

    // Into the hall, beside the guardian.
    w.mutable_hero().pos = stand_near(w, *start);
    auto& ordinary = w.mutable_enemies();
    ordinary.erase(std::remove_if(ordinary.begin(), ordinary.end(),
                                  [](const rt::Enemy& e) { return e.role == Role::Normal && e.owner == 0; }),
                   ordinary.end());

    bool fell = false;
    for (int t = 0; t < 240 * 60 && !fell; ++t) {
        rt::Hero& h = w.mutable_hero();
        h.hp = h.max_hp;
        h.energy = h.max_energy;
        V go{std::cos(static_cast<float>(t) * 0.02f), std::sin(static_cast<float>(t) * 0.031f)};
        if (const rt::Enemy* needle = find(w, Role::Needle);
            needle && !needle->hidden && rt::length(needle->pos - h.pos) > 3.0f)
            go = rt::normalized(needle->pos - h.pos);
        w.step(Input{go.x, go.y, true});
        fell = count(w.take_events(), EventKind::BossDied) > 0;
        ASSERT_FALSE(w.dead());
    }
    EXPECT_TRUE(fell) << "the guardian of floor " << depth << " (species " << boss_species
                      << ") outlasted four minutes of fire";
    EXPECT_EQ(w.boss(), nullptr);
    for (int t = 0; t < 30; ++t) w.step(Input{});
    EXPECT_TRUE(w.exit_open()) << "floor " << depth << ": the stairs stayed shut after the guardian fell";
    for (const auto& e : w.enemies())
        EXPECT_NE(e.role, Role::Hut) << "a hut outlived its mistress";
}

TEST(RealtimeBosses, TouchingAGuardianThrowsTheHeroClear) {
    // Кощей walks straight at the hero, which makes him the one to test a
    // bump on: without the throw he stands in contact and grinds.
    World w = with_boss(species_key::kKoschei, {10.5f, 6.5f}, {11.3f, 6.5f});
    rt::Enemy* k = find(w, Role::Boss);
    k->timer = 10.0f;   // no charge during the test
    const int full = w.hero().hp + w.hero().shield;
    w.step(Input{});
    const int lost = full - (w.hero().hp + w.hero().shield);
    EXPECT_GT(lost, 0);
    EXPECT_LE(lost, std::max(2, k->damage / 2));
    EXPECT_GT(rt::length(w.hero().pos - k->pos), 1.2f) << "the hero was left touching the guardian";
}

TEST(RealtimeBosses, NoGuardianKillsTheFrailestHeroInThreeBlows) {
    int frailest = 1 << 30;
    for (const auto& h : rt::heroes()) frailest = std::min(frailest, h.hp + h.shield);
    for (int depth : guardian_floors()) {
        World w;
        w.start(4);
        w.enter_floor(depth);
        ASSERT_NE(w.boss(), nullptr);
        EXPECT_GT(frailest, 3 * w.boss()->damage) << "floor " << depth;
    }
}

TEST(RealtimeBosses, EveryGuardianCanBeKilled) {
    for (int depth : guardian_floors()) {
        SCOPED_TRACE(testing::Message() << "floor " << depth);
        fight_to_the_death(depth);
    }
}

// --- Вий -----------------------------------------------------------------------------

TEST(RealtimeBosses, ViysGazeFindsAHeroInTheOpen) {
    World w = with_boss(species_key::kViy, {4.5f, 6.5f}, {14.5f, 6.5f});
    rt::Enemy* viy = find(w, Role::Boss);
    viy->state = 1;
    viy->timer = 0.05f;
    for (int t = 0; t < 10; ++t) w.step(Input{});
    const auto events = w.take_events();
    EXPECT_EQ(count(events, EventKind::GazeHit), 1);
    EXPECT_GT(w.hero().blind, 0.0f);
}

TEST(RealtimeBosses, AWallBetweenIsTheAnswerToTheGaze) {
    Map m = open_room();
    for (int y = 4; y <= 8; ++y) m.set({10, y}, Tile::Wall);
    World w = with_boss(species_key::kViy, {4.5f, 6.5f}, {14.5f, 6.5f}, m);
    rt::Enemy* viy = find(w, Role::Boss);
    viy->state = 1;
    viy->timer = 0.05f;
    for (int t = 0; t < 10; ++t) w.step(Input{});
    const auto events = w.take_events();
    EXPECT_EQ(count(events, EventKind::GazeHit), 0);
    EXPECT_EQ(count(events, EventKind::GazeMissed), 1) << "hiding in time went unacknowledged";
    EXPECT_EQ(w.hero().blind, 0.0f);
}

TEST(RealtimeBosses, TheGazeIsAnnouncedBeforeItLands) {
    World w = with_boss(species_key::kViy, {4.5f, 6.5f}, {14.5f, 6.5f});
    rt::Enemy* viy = find(w, Role::Boss);
    viy->state = 0;
    viy->timer = 0.01f;
    bool warned = false;
    for (int t = 0; t < 180; ++t) {
        w.step(Input{});
        if (!w.telegraphs().empty()) warned = true;
        if (count(w.take_events(), EventKind::GazeHit) > 0) {
            EXPECT_TRUE(warned) << "the gaze landed with no line drawn first";
            return;
        }
    }
    FAIL() << "the gaze never came";
}

// --- Кощей -----------------------------------------------------------------------------

TEST(RealtimeBosses, KoscheiRisesWhileHisNeedleIsWhole) {
    World w = with_boss(species_key::kKoschei, {5.5f, 6.5f}, {7.5f, 6.5f});
    rt::Enemy* koschei = find(w, Role::Boss);
    rt::Enemy* needle = find(w, Role::Needle);
    ASSERT_NE(needle, nullptr);
    EXPECT_TRUE(needle->hidden) << "the needle is found, not handed over";
    koschei->hp = 1;
    bool rose = false;
    for (int t = 0; t < 60 && !rose; ++t) {
        w.step(Input{0, 0, true});
        const auto events = w.take_events();
        rose = count(events, EventKind::Revived) > 0;
        ASSERT_EQ(count(events, EventKind::BossDied), 0) << "Кощей died with his needle whole";
    }
    ASSERT_TRUE(rose);
    koschei = find(w, Role::Boss);
    ASSERT_NE(koschei, nullptr);
    EXPECT_EQ(koschei->hp, koschei->max_hp * 3 / 5);
    EXPECT_FALSE(find(w, Role::Needle)->hidden) << "he rose, and the needle stayed hidden";
}

TEST(RealtimeBosses, BrokenNeedleBrokenSpell) {
    World w = with_boss(species_key::kKoschei, {5.5f, 6.5f}, {7.5f, 6.5f});
    rt::Enemy* needle = find(w, Role::Needle);
    needle->hidden = false;
    needle->hp = 1;
    // Beside the needle, with Кощей well out of the way.
    w.mutable_hero().pos = needle->pos + V{-1.2f, 0};
    find(w, Role::Boss)->pos = {2.5f, 2.5f};
    bool broke = false;
    for (int t = 0; t < 60 && !broke; ++t) {
        w.step(Input{0, 0, true});
        broke = count(w.take_events(), EventKind::NeedleBroken) > 0;
    }
    ASSERT_TRUE(broke);
    rt::Enemy* koschei = find(w, Role::Boss);
    koschei->hp = 1;
    w.mutable_hero().pos = koschei->pos + V{1.5f, 0};
    bool died = false;
    for (int t = 0; t < 120 && !died; ++t) {
        w.mutable_hero().hp = w.hero().max_hp;
        w.step(Input{0, 0, true});
        const auto events = w.take_events();
        EXPECT_EQ(count(events, EventKind::Revived), 0) << "rose with no needle to rise by";
        died = count(events, EventKind::BossDied) > 0;
    }
    EXPECT_TRUE(died);
}

// --- Баба-Яга ------------------------------------------------------------------------

TEST(RealtimeBosses, BabaYagaBarelyFeelsABlowWhileAHutStands) {
    World w = with_boss(species_key::kBabaYaga, {11.5f, 6.5f}, {8.5f, 6.5f});
    int huts = 0;
    for (const auto& e : w.enemies())
        if (e.role == Role::Hut) ++huts;
    EXPECT_EQ(huts, 3);

    const auto hit_on_yaga = [](World& world) {
        for (int t = 0; t < 120; ++t) {
            world.mutable_hero().hp = world.hero().max_hp;
            world.step(Input{0, 0, true});
            for (const auto& e : world.take_events()) {
                if (e.kind != EventKind::EnemyHit) continue;
                const rt::Enemy* b = world.boss();
                if (b && e.ref == b->id) return e.amount;
            }
        }
        return -1;
    };
    // Huts and imps out of the aim, so the first thing hit is her.
    auto& all = w.mutable_enemies();
    for (auto& e : all)
        if (e.role == Role::Hut) e.pos = {20.0f, 1.5f};
    const int guarded = hit_on_yaga(w);

    all.erase(std::remove_if(all.begin(), all.end(), [](const rt::Enemy& e) { return e.role == Role::Hut; }),
              all.end());
    const int bare = hit_on_yaga(w);
    ASSERT_GT(guarded, 0);
    EXPECT_GE(bare, guarded * 3) << "the huts made little difference to what reached her";
}

// --- Морозко ---------------------------------------------------------------------

TEST(RealtimeBosses, MorozkoFreezesWhoeverIsInsideTheRing) {
    World w = with_boss(species_key::kMorozko, {8.5f, 6.5f}, {10.0f, 6.5f});
    rt::Enemy* morozko = find(w, Role::Boss);
    morozko->state = 1;
    morozko->timer = 0.05f;
    for (int t = 0; t < 10; ++t) w.step(Input{});
    EXPECT_GT(w.hero().frozen, 0.0f);
    const float x = w.hero().pos.x;
    w.step(Input{1, 0, false});
    EXPECT_FLOAT_EQ(w.hero().pos.x, x) << "frozen legs walked";
}

TEST(RealtimeBosses, StepOutOfTheRingAndTheColdMisses) {
    World w = with_boss(species_key::kMorozko, {4.5f, 6.5f}, {15.5f, 6.5f});
    rt::Enemy* morozko = find(w, Role::Boss);
    morozko->state = 1;
    morozko->timer = 0.05f;
    for (int t = 0; t < 10; ++t) w.step(Input{});
    EXPECT_EQ(w.hero().frozen, 0.0f);
}

// --- Полоз -----------------------------------------------------------------------

TEST(RealtimeBosses, PolozhCannotBeHitUnderground) {
    World w = with_boss(species_key::kPolozh, {12.5f, 6.5f}, {8.5f, 6.5f});
    rt::Enemy* polozh = find(w, Role::Boss);
    polozh->state = 1;
    polozh->hidden = true;
    polozh->timer = 5.0f;
    const int id = polozh->id;
    for (int t = 0; t < 30; ++t) {
        w.step(Input{0, 0, true});
        for (const auto& e : w.take_events())
            ASSERT_FALSE(e.kind == EventKind::EnemyHit && e.ref == id) << "a bolt found him in the rock";
    }
}

TEST(RealtimeBosses, WherePolozhWillSurfaceIsMarkedFirst) {
    World w = with_boss(species_key::kPolozh, {4.5f, 6.5f}, {14.5f, 6.5f});
    rt::Enemy* polozh = find(w, Role::Boss);
    polozh->state = 2;
    polozh->hidden = true;
    polozh->target = w.hero().pos;
    polozh->timer = 0.8f;
    w.step(Input{});
    ASSERT_EQ(w.telegraphs().size(), 1u);
    EXPECT_EQ(w.telegraphs()[0].kind, rt::TelegraphKind::Spot);
}

// --- Змей Горыныч -----------------------------------------------------------------

TEST(RealtimeBosses, GorynychBreathesOneStreamPerHead) {
    const auto streams = [](int phase) {
        World w = with_boss(species_key::kGorynych, {5.5f, 6.5f}, {15.5f, 6.5f});
        rt::Enemy* g = find(w, Role::Boss);
        g->phase = phase;
        g->state = 2;
        g->timer = 1.0f;
        g->timer2 = 0;
        w.mutable_bullets().clear();
        w.step(Input{});
        int fire = 0;
        for (const auto& b : w.bullets())
            if (!b.hero) ++fire;
        return fire;
    };
    EXPECT_EQ(streams(1), 3);
    EXPECT_EQ(streams(2), 2);
    EXPECT_EQ(streams(3), 1);
}

// --- Every guardian -------------------------------------------------------------

TEST(RealtimeBosses, AGuardianAnnouncesItselfWhenTheFightBegins) {
    World w;
    w.start(1);
    w.load_test_floor(open_room(), {13.5f, 6.5f});   // eight cells: inside the nine it wakes at
    w.add_boss(species_index(species_key::kMara), {5.5f, 6.5f});   // asleep, in the open
    bool announced = false;
    for (int t = 0; t < 30 && !announced; ++t) {
        w.step(Input{});
        announced = count(w.take_events(), EventKind::BossAppears) > 0;
    }
    EXPECT_TRUE(announced);
}

TEST(RealtimeBosses, PhasesOnlyEverRise) {
    World w = with_boss(species_key::kViy, {12.5f, 6.5f}, {8.5f, 6.5f});
    rt::Enemy* viy = find(w, Role::Boss);
    viy->hp = viy->max_hp / 3;   // deep into the fight
    for (int t = 0; t < 60; ++t) w.step(Input{0, 0, true});
    viy = find(w, Role::Boss);
    ASSERT_NE(viy, nullptr);
    const int reached = viy->phase;
    EXPECT_GE(reached, 2);
    viy->hp = viy->max_hp;   // healed back to full, somehow
    for (int t = 0; t < 30; ++t) w.step(Input{0, 0, true});
    EXPECT_EQ(find(w, Role::Boss)->phase, reached) << "healing handed back an easier pattern";
}

TEST(RealtimeBosses, EveryDangerousAttackIsAnnounced) {
    // The design rule, checked on the rule's own terms: whatever else a
    // guardian does, the warnings it draws are well-formed and fill from
    // nothing to full rather than appearing already complete. Each guardian
    // is met in its last phase, where it has the most to throw.
    for (int depth : guardian_floors()) {
        World w;
        w.start(2);
        w.enter_floor(depth);
        const rt::Enemy* b = w.boss();
        ASSERT_NE(b, nullptr);
        w.mutable_hero().pos = stand_near(w, *b);
        for (auto& e : w.mutable_enemies()) {
            e.awake = true;
            if (e.role == Role::Boss) e.phase = e.phases;
        }
        float lowest = 1.0f;
        for (int t = 0; t < 12 * 60; ++t) {
            rt::Hero& h = w.mutable_hero();
            h.hp = h.max_hp;
            w.step(Input{std::cos(static_cast<float>(t) * 0.03f), std::sin(static_cast<float>(t) * 0.05f), false});
            for (const auto& tg : w.telegraphs()) {
                EXPECT_GE(tg.progress, 0.0f);
                EXPECT_LE(tg.progress, 1.0f);
                lowest = std::min(lowest, tg.progress);
            }
        }
        EXPECT_LT(lowest, 0.2f) << "floor " << depth << ": no warning was ever seen starting";
    }
}

}  // namespace
}  // namespace nav
