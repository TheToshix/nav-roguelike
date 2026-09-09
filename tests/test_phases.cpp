// SPDX-License-Identifier: MIT
//
// Boss phases.
//
// A phase is a promise that the second half of a fight is not the first half
// with a smaller number, so these tests are about *behaviour changing*, not
// about a counter incrementing. The phase itself is checked for the properties
// that make it safe — monotonic, bounded, reached at the right health — and
// then each boss is checked for the thing it starts doing when it turns.
#include <gtest/gtest.h>

#include <cstring>

#include "nav/game.hpp"

#include "support.hpp"

using namespace nav;

namespace {

/// An empty floor and an unkillable hero, so what is measured is the boss.
class PhaseArena {
public:
    explicit PhaseArena(std::uint64_t seed = 4242) {
        GameConfig cfg;
        cfg.seed = seed;
        cfg.hero_class = HeroClass::Vityaz;
        game.start(cfg);
        leave_crossroads(game);

        Level& lvl = game.mutable_level();
        lvl.monsters.clear();
        lvl.items.clear();
        for (int y = 1; y < lvl.map.height() - 1; ++y)
            for (int x = 1; x < lvl.map.width() - 1; ++x) {
                const Tile t = lvl.map.at({x, y});
                if (t != Tile::StairsUp && t != Tile::StairsDown) lvl.map.set({x, y}, Tile::Floor);
            }
        game.mutable_hero().a.pos = {20, 15};
        game.mutable_hero().a.max_hp = 1000000;
        game.mutable_hero().a.hp = 1000000;
        game.mutable_hero().nutrition = 1000000;
        game.refresh_view();
    }

    Monster& spawn(const char* key, Vec2 pos) {
        const int index = species_index(key);
        EXPECT_GE(index, 0) << key;
        const Species& sp = bestiary()[static_cast<std::size_t>(index)];
        Monster m{};
        m.species = index;
        m.a.pos = pos;
        m.a.hp = m.a.max_hp = sp.hp;
        m.a.attack = sp.attack;
        m.a.defence = sp.defence;
        m.a.speed = sp.speed;
        m.awake = true;
        game.mutable_level().monsters.push_back(m);
        game.refresh_view();
        return game.mutable_level().monsters.back();
    }

    Monster* find(const char* key) {
        const int index = species_index(key);
        for (auto& m : game.mutable_level().monsters)
            if (m.species == index) return &m;
        return nullptr;
    }

    /// Cuts a boss down to a fraction of its health the way a player would —
    /// through the damage path — so the phase machinery runs.
    void wound_to(const char* key, double fraction) {
        Monster* m = find(key);
        ASSERT_NE(m, nullptr) << key;
        const int want = std::max(1, static_cast<int>(m->a.max_hp * fraction));
        while (m->a.hp > want) {
            game.damage_monster(*m, std::min(7, m->a.hp - want), Text{"проверка", "the test"});
            m = find(key);
            ASSERT_NE(m, nullptr) << key;
        }
    }

    void wait(int turns) {
        for (int i = 0; i < turns; ++i) game.perform(Action{ActionType::Wait, {}, -1, {}});
    }

    Game game;
};

int species_phases(const char* key) {
    const int i = species_index(key);
    return i < 0 ? 0 : bestiary()[static_cast<std::size_t>(i)].phases;
}

}  // namespace

// ---------------------------------------------------------------------------
// The phase machinery itself
// ---------------------------------------------------------------------------

TEST(Phases, EveryBossHasMoreThanOneAndEveryOrdinaryMonsterHasExactlyOne) {
    for (const Species& s : bestiary()) {
        if (s.ai & AiBoss) {
            EXPECT_GE(s.phases, 2) << s.key << " is a boss with a single pattern";
            EXPECT_LE(s.phases, 3) << s.key << " has more phases than the code can announce";
        } else {
            EXPECT_EQ(s.phases, 1) << s.key << " is not a boss and should not have phases";
        }
    }
}

TEST(Phases, EveryPhaseAnnouncesItself) {
    // A pattern that changes without a line in the log reads as the game
    // cheating, so the table has to cover every phase every boss can reach.
    for (const Species& s : bestiary()) {
        if (!(s.ai & AiBoss)) continue;
        for (int phase = 2; phase <= s.phases; ++phase) {
            const Text line = boss_phase_line(s.key, phase);
            EXPECT_FALSE(line.ru.empty()) << s.key << " phase " << phase << " (ru)";
            EXPECT_FALSE(line.en.empty()) << s.key << " phase " << phase << " (en)";
        }
    }
}

TEST(Phases, TheyTurnAtEvenFractionsOfTheHealthBar) {
    PhaseArena a;
    a.spawn("koschei", {24, 15});          // three phases
    ASSERT_EQ(species_phases("koschei"), 3);

    EXPECT_EQ(a.find("koschei")->phase, 1);
    a.wound_to("koschei", 0.70);
    EXPECT_EQ(a.find("koschei")->phase, 1) << "turned early";
    a.wound_to("koschei", 0.60);
    EXPECT_EQ(a.find("koschei")->phase, 2);
    a.wound_to("koschei", 0.40);
    EXPECT_EQ(a.find("koschei")->phase, 2);
    a.wound_to("koschei", 0.25);
    EXPECT_EQ(a.find("koschei")->phase, 3);
}

TEST(Phases, ATwoPhaseBossTurnsAtHalf) {
    PhaseArena a;
    a.spawn("morozko", {24, 15});
    ASSERT_EQ(species_phases("morozko"), 2);

    a.wound_to("morozko", 0.60);
    EXPECT_EQ(a.find("morozko")->phase, 1);
    a.wound_to("morozko", 0.45);
    EXPECT_EQ(a.find("morozko")->phase, 2);
}

TEST(Phases, HealingABossNeverGivesBackAPatternAlreadyBeaten) {
    // Кощей heals himself; Водяной heals in water. Without this the player
    // would be forced to re-beat a phase they have already solved, which is the
    // difference between a hard fight and a tedious one.
    PhaseArena a;
    a.spawn("koschei", {24, 15});
    a.wound_to("koschei", 0.20);
    ASSERT_EQ(a.find("koschei")->phase, 3);

    Monster* m = a.find("koschei");
    m->a.hp = m->a.max_hp;
    a.game.damage_monster(*m, 1, Text{"проверка", "the test"});
    EXPECT_EQ(a.find("koschei")->phase, 3) << "a healed boss slid back to an earlier pattern";
}

TEST(Phases, ADoctoredSaveCannotInventAPhase) {
    PhaseArena a;
    a.spawn("morozko", {24, 15});
    const std::string good = a.game.save();

    // Морозко has two phases; a save claiming a third has no code behind it.
    std::string bad = good;
    const std::size_t at = bad.rfind(" 2 ");
    ASSERT_NE(at, std::string::npos);
    Game g;
    // Rather than guess at the offset, walk every "1" that could be the phase
    // field and check that no rewrite to 9 is ever accepted.
    for (std::size_t i = 0; i + 1 < good.size(); ++i) {
        if (good[i] != '1' || good[i + 1] != ' ') continue;
        std::string doctored = good;
        doctored[i] = '9';
        if (g.load(doctored)) {
            for (const auto& m : g.monsters())
                EXPECT_LE(m.phase, 3) << "a phase of 9 survived the loader";
        }
    }
}

// ---------------------------------------------------------------------------
// What each boss actually starts doing
// ---------------------------------------------------------------------------

TEST(Phases, ViyOpensHisEyesMoreOftenAsHeIsWorn) {
    // The counter-play never changes — get out of sight — but the window to hit
    // him between gazes has to shrink, or the fight has no second half.
    auto gazes_in = [](double fraction, int turns) {
        PhaseArena a(99);
        a.spawn("viy", {24, 15});
        if (fraction < 1.0) a.wound_to("viy", fraction);
        a.game.clear_log();
        a.wait(turns);
        int count = 0;
        for (const auto& e : a.game.log())
            if (e.text.ru.find("Поднимите мне веки") != std::string::npos) ++count;
        return count;
    };

    const int early = gazes_in(1.0, 24);
    const int late = gazes_in(0.20, 24);
    EXPECT_GT(early, 0) << "Виy never opened his eyes at all";
    EXPECT_GT(late, early) << "the third phase is no faster than the first";
}

TEST(Phases, BabaYagaRaisesAFreshHutWhenSheIsNearlyDone) {
    PhaseArena a;
    a.spawn("babayaga", {24, 15});
    const int hut = species_index("izbushka");
    ASSERT_GE(hut, 0);

    auto huts = [&] {
        int n = 0;
        for (const auto& m : a.game.monsters())
            if (m.a.alive && m.species == hut) ++n;
        return n;
    };
    ASSERT_EQ(huts(), 0) << "the arena starts clear";

    a.wound_to("babayaga", 0.25);
    ASSERT_EQ(a.find("babayaga")->phase, 3);
    EXPECT_EQ(huts(), 0) << "she raises the hut on her own turn, not the instant she is hurt";

    a.wait(1);
    EXPECT_EQ(huts(), 1) << "the third phase should put a hut back between her and the hero";

    a.wait(3);
    EXPECT_EQ(huts(), 1) << "and only the one — the hut is raised once, not every turn";
}

TEST(Phases, TheFieryPolozStopsKeepingItsDistance) {
    // In its first phase distance is safety; in its second it burrows, and the
    // player who learned to kite has to learn something else.
    auto closed_in = [](double fraction) {
        PhaseArena a(1234);
        a.spawn("polozh", {34, 15});     // fourteen cells away
        if (fraction < 1.0) a.wound_to("polozh", fraction);
        int best = 99;
        for (int i = 0; i < 40; ++i) {
            a.wait(1);
            Monster* m = a.find("polozh");
            if (!m) break;
            best = std::min(best, chebyshev(m->a.pos, a.game.hero().a.pos));
        }
        return best;
    };
    EXPECT_LE(closed_in(0.30), 2) << "the burrowing phase never arrived beside the hero";
}

TEST(Phases, GorynychBreathesMoreOftenWithEveryHeadItLoses) {
    auto breaths = [](double fraction, int turns) {
        PhaseArena a(77);
        a.spawn("gorynych", {26, 15});
        if (fraction < 1.0) a.wound_to("gorynych", fraction);
        a.game.clear_log();
        a.wait(turns);
        int count = 0;
        for (const auto& e : a.game.log())
            if (e.text.ru.find("выдыхает пламя") != std::string::npos) ++count;
        return count;
    };
    const int three_heads = breaths(1.0, 30);
    const int one_head = breaths(0.20, 30);
    EXPECT_GT(three_heads, 0) << "the fire never came at all";
    EXPECT_GT(one_head, three_heads) << "losing heads did not make him more dangerous";
}

TEST(Phases, KoscheiStartsDrainingInsteadOfStriking) {
    PhaseArena a;
    a.spawn("koschei", {21, 15});        // adjacent
    a.wound_to("koschei", 0.50);
    ASSERT_EQ(a.find("koschei")->phase, 2);

    const int before = a.find("koschei")->a.hp;
    a.game.clear_log();
    a.wait(6);
    EXPECT_GT(a.find("koschei")->a.hp, before)
        << "the draining phase should close his own wounds with the hero's";
}

TEST(Phases, MiniBossesGuardTheThirdFloorOfEveryBelt) {
    // Halfway down, not at the mouth: the belt needs two floors to teach its
    // monsters before it asks the player to fight something with a mechanic.
    const int expected[] = {3, 7, 11, 15};
    for (int depth : expected) {
        const char* key = boss_for_depth(depth);
        ASSERT_NE(key, nullptr) << "depth " << depth;
        const int i = species_index(key);
        ASSERT_GE(i, 0);
        EXPECT_TRUE(bestiary()[static_cast<std::size_t>(i)].ai & AiMiniBoss)
            << key << " sits on a mid-belt floor but is not marked as a lesser guardian";
    }

    // And the belt masters are not.
    for (int depth : {4, 8, 12, 16}) {
        const char* key = boss_for_depth(depth);
        ASSERT_NE(key, nullptr) << "depth " << depth;
        const int i = species_index(key);
        EXPECT_FALSE(bestiary()[static_cast<std::size_t>(i)].ai & AiMiniBoss) << key;
    }
}

TEST(Phases, MaraWillNotBeCornered) {
    // Her whole fight is that she is never where you swung. Standing next to
    // her has to stop working, or she is a slow ranged monster.
    PhaseArena a(555);
    a.spawn("mara", {21, 15});           // adjacent to the hero at (20,15)
    int adjacent_turns = 0;
    for (int i = 0; i < 30; ++i) {
        a.wait(1);
        const Monster* m = a.find("mara");
        if (!m) break;
        if (chebyshev(m->a.pos, a.game.hero().a.pos) <= 1) ++adjacent_turns;
    }
    EXPECT_LT(adjacent_turns, 15) << "Мара stood next to the hero for half the fight";
}

TEST(Phases, TheWardingCircleIsTheAnswerToMara) {
    auto confused_turns = [](bool wear_the_set) {
        PhaseArena a(606);
        if (wear_the_set) {
            Inventory& inv = a.game.mutable_hero().inv;
            const auto& gear = gear_table();
            auto put = [&](const char* key, int Inventory::*slot) {
                for (std::size_t i = 0; i < gear.size(); ++i) {
                    if (std::strcmp(gear[i].key, key) != 0) continue;
                    Item it{};
                    it.kind = gear[i].kind;
                    it.subtype = static_cast<int>(i);
                    it.power = gear[i].power;
                    it.identified = true;
                    inv.add(it);
                    inv.*slot = static_cast<int>(inv.items.size()) - 1;
                    return;
                }
            };
            put("rogatina", &Inventory::weapon);
            put("sorochka", &Inventory::armor);
            put("nauzy", &Inventory::amulet);
        }
        a.spawn("mara", {24, 15});
        int count = 0;
        for (int i = 0; i < 60; ++i) {
            a.wait(1);
            if (a.game.hero().a.has(Effect::Confusion)) ++count;
        }
        return count;
    };
    const int bare = confused_turns(false);
    const int warded = confused_turns(true);
    EXPECT_GT(bare, 0) << "Мара never confused an unprotected hero at all";
    EXPECT_EQ(warded, 0) << "the warding circle let the delusion through";
}

TEST(Phases, VodyanoyMendsHimselfInTheWater) {
    PhaseArena a(707);
    Monster& m = a.spawn("vodyanoy", {24, 15});
    a.game.mutable_level().map.set({24, 15}, Tile::Water);
    a.game.refresh_view();
    m.a.hp = m.a.max_hp / 2;
    const int before = m.a.hp;

    a.wait(10);
    const Monster* after = a.find("vodyanoy");
    ASSERT_NE(after, nullptr);
    EXPECT_GT(after->a.hp, before) << "standing in his own element healed him not at all";
}

TEST(Phases, MorozkoFreezesWithAWarningFirst) {
    PhaseArena a(808);
    a.spawn("morozko", {25, 15});
    a.game.clear_log();
    a.wait(12);

    bool warned = false, struck = false;
    for (const auto& e : a.game.log()) {
        if (e.text.ru.find("Тепло ли тебе") != std::string::npos) warned = true;
        if (e.text.ru.find("Стужа хватает") != std::string::npos) {
            EXPECT_TRUE(warned) << "the cold arrived before the question did";
            struck = true;
        }
    }
    EXPECT_TRUE(struck) << "Морозко never used his one attack in twelve turns";
}
