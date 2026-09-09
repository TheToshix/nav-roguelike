// SPDX-License-Identifier: MIT
//
// Achievements: the pure text round trip, and what a finished run earns.
#include <gtest/gtest.h>

#include <algorithm>

#include "nav/achievements.hpp"
#include "nav/game.hpp"
#include "nav/pathfind.hpp"

#include "support.hpp"

using namespace nav;

namespace {

bool has(const std::vector<std::string>& v, const char* key) {
    return std::find(v.begin(), v.end(), key) != v.end();
}

/// A game descended straight to the bottom and then won by killing Змей Горыныч
/// outright — no blow ever landed on the hero.
Game won_game(HeroClass cls, std::uint64_t seed) {
    GameConfig cfg;
    cfg.seed = seed;
    cfg.hero_class = cls;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    for (int d = 1; d < kMaxDepth; ++d) {
        g.mutable_hero().a.pos = g.level().exit;
        g.refresh_view();
        EXPECT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
    EXPECT_EQ(g.depth(), kMaxDepth);

    const int gorynych = species_index("gorynych");
    for (auto& m : g.mutable_level().monsters)
        if (m.species == gorynych) g.damage_monster(m, 999999, Text{"the test", "the test"});
    EXPECT_EQ(g.state(), RunState::Ascended);
    return g;
}

}  // namespace

TEST(Achievements, TheTableIsWholeAndBilingual) {
    ASSERT_FALSE(achievement_table().empty());
    for (const AchievementInfo& a : achievement_table()) {
        EXPECT_NE(a.key, nullptr);
        EXPECT_FALSE(a.name.ru.empty()) << a.key;
        EXPECT_FALSE(a.name.en.empty()) << a.key;
        EXPECT_FALSE(a.how.ru.empty()) << a.key;
        EXPECT_FALSE(a.how.en.empty()) << a.key;
    }
}

TEST(Achievements, TheStoreRoundTripsAndDropsUnknownKeys) {
    std::vector<std::string> unlocked = {"ascended", "speedrun"};
    const std::string blob = serialize_achievements(unlocked);

    std::vector<std::string> back;
    ASSERT_TRUE(parse_achievements(blob, back));
    EXPECT_TRUE(has(back, "ascended"));
    EXPECT_TRUE(has(back, "speedrun"));
    EXPECT_EQ(back.size(), 2u);

    // A key the table no longer knows is silently dropped, not an error.
    std::vector<std::string> after;
    EXPECT_TRUE(parse_achievements("NAVFEATS 1 2\nascended\nno_such_feat\n", after));
    EXPECT_EQ(after.size(), 1u);
    EXPECT_TRUE(has(after, "ascended"));
}

TEST(Achievements, GarbageIsRefused) {
    std::vector<std::string> out;
    EXPECT_FALSE(parse_achievements("", out));
    EXPECT_FALSE(parse_achievements("not a feats file", out));
    EXPECT_FALSE(parse_achievements("NAVFEATS 9 0\n", out)) << "a wrong version must be refused";
}

TEST(Achievements, MergeReportsOnlyWhatIsNew) {
    std::vector<std::string> unlocked = {"ascended"};
    const std::vector<std::string> fresh =
        merge_achievements(unlocked, {"ascended", "speedrun", "onfoot"});

    EXPECT_EQ(fresh.size(), 2u);
    EXPECT_TRUE(has(fresh, "speedrun"));
    EXPECT_TRUE(has(fresh, "onfoot"));
    EXPECT_FALSE(has(fresh, "ascended")) << "an already-held feat is not new";
    EXPECT_EQ(unlocked.size(), 3u);

    // Re-merging the same run adds nothing.
    EXPECT_TRUE(merge_achievements(unlocked, {"speedrun"}).empty());
}

TEST(Achievements, ADamagelessDescentEarnsNotAScratch) {
    GameConfig cfg;
    cfg.seed = 2024;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    for (int d = 1; d < 8; ++d) {
        g.mutable_hero().a.pos = g.level().exit;
        g.refresh_view();
        ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
    ASSERT_EQ(g.depth(), 8);
    EXPECT_GE(g.deepest_unhurt(), 8);
    EXPECT_TRUE(has(achievements_earned(g), "deathless8"));

    // One blow, and the "no damage" run is over — but deepest_unhurt stays.
    g.damage_hero(5, Text{"проверка", "the test"});
    EXPECT_FALSE(g.flawless());
    EXPECT_GE(g.deepest_unhurt(), 8) << "it is a high-water mark, not a live flag";
}

TEST(Achievements, AWinEarnsTheAscentAndTheClass) {
    Game g = won_game(HeroClass::Tat, 7777);
    const auto earned = achievements_earned(g);

    EXPECT_TRUE(has(earned, "ascended"));
    EXPECT_TRUE(has(earned, "won_tat"));
    EXPECT_FALSE(has(earned, "won_vityaz"));
    EXPECT_TRUE(has(earned, "flawless_win")) << "no blow ever landed";
    EXPECT_TRUE(has(earned, "onfoot")) << "the descent never ran or auto-explored";
}

TEST(Achievements, NothingIsEarnedForADeadRun) {
    GameConfig cfg;
    cfg.seed = 99;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    g.damage_hero(999999, Text{"проверка", "the test"});
    ASSERT_EQ(g.state(), RunState::Dead);

    const auto earned = achievements_earned(g);
    EXPECT_FALSE(has(earned, "ascended"));
    EXPECT_FALSE(has(earned, "flawless_win"));
    EXPECT_FALSE(has(earned, "won_vityaz"));
}
