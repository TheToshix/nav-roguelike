// SPDX-License-Identifier: MIT
//
// The victory epilogue — the counterpart to the post-mortem. A win owes an
// ending that is this win: these tests pin down that the text is chosen from
// what happened (class, whether the last guardian drew blood, how fast), that
// it is bilingual, and that the "unscathed on the last floor" flag it reads
// survives a save.
#include <gtest/gtest.h>

#include <set>
#include <string>

#include "nav/game.hpp"

#include "support.hpp"

using namespace nav;

namespace {

struct WinOptions {
    HeroClass cls{HeroClass::Vityaz};
    int hurt_on_depth{0};   ///< Take a small hit on this floor on the way down (0 = never).
    int hurt_on_bottom{0};  ///< Points of damage to take on floor 16 before the kill.
};

/// Descends straight to the bottom and kills Змей Горыныч outright.
Game win(const WinOptions& opt) {
    GameConfig cfg;
    cfg.seed = 20240607;
    cfg.hero_class = opt.cls;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    for (int d = 1; d < kMaxDepth; ++d) {
        if (d == opt.hurt_on_depth) {
            g.mutable_hero().a.max_hp = 500;
            g.mutable_hero().a.hp = 500;
            g.damage_hero(7, Text{"проверка", "the test"});
        }
        g.mutable_hero().a.pos = g.level().exit;
        g.refresh_view();
        EXPECT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
    EXPECT_EQ(g.depth(), kMaxDepth);

    if (opt.hurt_on_bottom > 0) {
        g.mutable_hero().a.max_hp = 500;
        g.mutable_hero().a.hp = 500;
        g.damage_hero(opt.hurt_on_bottom, Text{"Горыныч", "Gorynych"});
    }

    const int gorynych = species_index("gorynych");
    for (auto& m : g.mutable_level().monsters)
        if (m.species == gorynych) g.damage_monster(m, 999999, Text{"the test", "the test"});
    EXPECT_EQ(g.state(), RunState::Ascended);
    return g;
}

std::string joined(const Game& g) {
    std::string s;
    for (const Text& line : g.epilogue().lines) {
        s += line.ru;
        s += '\n';
    }
    return s;
}

}  // namespace

TEST(Endings, ThereIsNoEpilogueUntilTheRunIsWon) {
    GameConfig cfg;
    cfg.seed = 1;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    EXPECT_TRUE(g.epilogue().lines.empty()) << "a run in progress has no ending";

    // Kill the hero: still no epilogue — death has the post-mortem instead.
    g.mutable_hero().a.max_hp = 10;
    g.mutable_hero().a.hp = 10;
    g.damage_hero(999, Text{"проверка", "the test"});
    ASSERT_EQ(g.state(), RunState::Dead);
    EXPECT_TRUE(g.epilogue().lines.empty()) << "a death is not an epilogue";
}

TEST(Endings, EveryClassGetsItsOwnOpeningLineAndItIsBilingual) {
    std::set<std::string> openings_ru, openings_en;
    for (HeroClass c : {HeroClass::Vityaz, HeroClass::Vedun, HeroClass::Tat,
                        HeroClass::Znahar, HeroClass::Kuznets, HeroClass::Bogatyr}) {
        Game g = win({c, 0, 0});
        const Epilogue ep = g.epilogue();
        ASSERT_FALSE(ep.lines.empty());
        for (const Text& line : ep.lines) {
            EXPECT_FALSE(line.ru.empty());
            EXPECT_FALSE(line.en.empty());
        }
        openings_ru.insert(ep.lines.front().ru);
        openings_en.insert(ep.lines.front().en);
    }
    EXPECT_EQ(openings_ru.size(), 6u) << "two classes share an opening line";
    EXPECT_EQ(openings_en.size(), 6u);
}

TEST(Endings, HowTheLastFightWentPicksADifferentLine) {
    const std::string flawless = joined(win({HeroClass::Vityaz, 0, 0}));
    const std::string hurt_earlier = joined(win({HeroClass::Vityaz, 8, 0}));
    const std::string first_blood_below = joined(win({HeroClass::Vityaz, 0, 7}));
    const std::string battered = joined(win({HeroClass::Vityaz, 6, 7}));

    // All four share the class opening but diverge on the fight line.
    EXPECT_NE(flawless, hurt_earlier);
    EXPECT_NE(flawless, first_blood_below);
    EXPECT_NE(hurt_earlier, first_blood_below);
    EXPECT_NE(first_blood_below, battered);
}

TEST(Endings, TheUnscathedFinalFlagTracksTheLastFloorOnly) {
    EXPECT_TRUE(win({HeroClass::Vityaz, 0, 0}).unscathed_final());
    EXPECT_TRUE(win({HeroClass::Vityaz, 8, 0}).unscathed_final())
        << "a hit eight floors up is not the last guardian's doing";
    EXPECT_FALSE(win({HeroClass::Vityaz, 0, 7}).unscathed_final())
        << "the serpent drew blood and the flag should say so";
}

TEST(Endings, ASwiftDescentIsCalledOut) {
    // The forced descent takes a few hundred turns — well under the 15000 the
    // engine treats as swift, so the swift line must be there.
    const std::string text = joined(win({HeroClass::Tat, 0, 0}));
    EXPECT_NE(text.find("быстрее"), std::string::npos) << text;
}

TEST(Endings, AWinOnFootAddsItsOwnLine) {
    // win() only ever steps and descends — it never runs or auto-explores.
    Game g = win({HeroClass::Bogatyr, 0, 0});
    EXPECT_FALSE(g.used_run_or_explore());
    EXPECT_NE(joined(g).find("шаг"), std::string::npos) << joined(g);
}

TEST(Endings, TheUnscathedFinalFlagSurvivesSaveAndLoad) {
    // Save mid-run on the bottom floor, after the serpent has landed a blow but
    // before it falls: a resumed run must still know the last fight drew blood.
    GameConfig cfg;
    cfg.seed = 20240607;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    for (int d = 1; d < kMaxDepth; ++d) {
        g.mutable_hero().a.pos = g.level().exit;
        g.refresh_view();
        ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
    g.mutable_hero().a.max_hp = 500;
    g.mutable_hero().a.hp = 500;
    g.damage_hero(5, Text{"Горыныч", "Gorynych"});
    ASSERT_FALSE(g.unscathed_final());

    Game restored;
    ASSERT_TRUE(restored.load(g.save()));
    EXPECT_FALSE(restored.unscathed_final()) << "the flag was not saved";

    const int gorynych = species_index("gorynych");
    for (auto& m : restored.mutable_level().monsters)
        if (m.species == gorynych)
            restored.damage_monster(m, 999999, Text{"the test", "the test"});
    ASSERT_EQ(restored.state(), RunState::Ascended);
    EXPECT_NE(joined(restored).find("Пекл"), std::string::npos)
        << "expected the 'first blood in the Scorch' line";
}
