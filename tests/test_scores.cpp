// SPDX-License-Identifier: MIT
//
// The high-score table.
//
// Ordering, capping and the round trip through text — the three things a table
// can quietly get wrong and nobody notices until a good run vanishes.
#include <gtest/gtest.h>

#include "nav/game.hpp"
#include "nav/score.hpp"

#include "support.hpp"

using namespace nav;

namespace {

ScoreEntry make(int score, int deepest = 5, int turns = 1000) {
    ScoreEntry e;
    e.score = score;
    e.deepest = deepest;
    e.turns = turns;
    e.seed_text = "зерно";
    return e;
}

}  // namespace

TEST(Scores, TheBestRunComesFirst) {
    std::vector<ScoreEntry> table;
    insert_score(table, make(500));
    insert_score(table, make(1500));
    insert_score(table, make(1000));

    ASSERT_EQ(table.size(), 3u);
    EXPECT_EQ(table[0].score, 1500);
    EXPECT_EQ(table[1].score, 1000);
    EXPECT_EQ(table[2].score, 500);
}

TEST(Scores, EqualScoresAreSeparatedByDepthAndThenBySpeed) {
    std::vector<ScoreEntry> table;
    insert_score(table, make(1000, /*deepest=*/4, /*turns=*/900));
    insert_score(table, make(1000, /*deepest=*/7, /*turns=*/2000));
    insert_score(table, make(1000, /*deepest=*/7, /*turns=*/1200));

    ASSERT_EQ(table.size(), 3u);
    EXPECT_EQ(table[0].deepest, 7);
    EXPECT_EQ(table[0].turns, 1200) << "the faster of two equally deep runs should lead";
    EXPECT_EQ(table[2].deepest, 4);
}

TEST(Scores, TheTableStopsAtTenAndSaysWhenARunMissedIt) {
    std::vector<ScoreEntry> table;
    for (int i = 0; i < 12; ++i)
        insert_score(table, make(1000 + i * 10));
    EXPECT_EQ(table.size(), kScoreTableSize);

    EXPECT_EQ(insert_score(table, make(1)), -1) << "a poor run should not claim a place";
    EXPECT_EQ(table.size(), kScoreTableSize);

    const int at = insert_score(table, make(99999));
    EXPECT_EQ(at, 0) << "a record run should be told it is the best";
    EXPECT_EQ(table.size(), kScoreTableSize) << "and should not grow the table";
}

TEST(Scores, ATableSurvivesTheRoundTrip) {
    std::vector<ScoreEntry> table;
    for (int i = 0; i < 4; ++i) {
        ScoreEntry e = make(1000 + i * 7, 3 + i, 500 + i);
        e.cls = static_cast<HeroClass>(i % static_cast<int>(HeroClass::Count));
        e.level = 2 + i;
        e.kills = 10 * i;
        e.gold = 33 * i;
        e.won = (i == 1);
        // Seeds are whatever the player typed, spaces and Cyrillic included.
        e.seed_text = i == 2 ? "две дороги вниз" : "";
        insert_score(table, e);
    }

    std::vector<ScoreEntry> back;
    ASSERT_TRUE(parse_scores(serialize_scores(table), back));
    ASSERT_EQ(back.size(), table.size());
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(back[i].seed_text, table[i].seed_text) << "row " << i;
        EXPECT_EQ(back[i].score, table[i].score);
        EXPECT_EQ(back[i].deepest, table[i].deepest);
        EXPECT_EQ(back[i].turns, table[i].turns);
        EXPECT_EQ(back[i].level, table[i].level);
        EXPECT_EQ(back[i].kills, table[i].kills);
        EXPECT_EQ(back[i].gold, table[i].gold);
        EXPECT_EQ(back[i].won, table[i].won);
        EXPECT_EQ(static_cast<int>(back[i].cls), static_cast<int>(table[i].cls));
    }
}

TEST(Scores, NothingMalformedIsHalfRead) {
    std::vector<ScoreEntry> table;
    ScoreEntry e = make(700);
    e.seed_text = "навь";
    insert_score(table, e);
    const std::string good = serialize_scores(table);

    std::vector<ScoreEntry> out;
    EXPECT_FALSE(parse_scores("", out));
    EXPECT_TRUE(out.empty());
    EXPECT_FALSE(parse_scores("NOTNAV 1 0\n", out));
    EXPECT_FALSE(parse_scores("NAVSCORES 99 0\n", out));
    EXPECT_FALSE(parse_scores("NAVSCORES 1 999\n", out)) << "an oversized table is refused";

    // Every truncation that loses real data must be refused rather than
    // half-read. Cutting only the trailing newline is not a truncation: the
    // file is still exactly what it claims to be, and demanding a refusal there
    // would be the test insisting on a bug.
    for (std::size_t cut = 1; cut < good.size(); ++cut) {
        const std::string tail = good.substr(cut);
        if (tail.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        out.assign(3, ScoreEntry{});
        if (parse_scores(good.substr(0, cut), out))
            FAIL() << "a table cut at " << cut << " bytes was accepted";
        EXPECT_TRUE(out.empty()) << "a refused table left rows behind (cut at " << cut << ")";
    }
}

TEST(Scores, ARunTurnsIntoARecord) {
    GameConfig cfg;
    cfg.seed_text = "навь";
    cfg.seed = Rng::hash_seed(cfg.seed_text);
    cfg.hero_class = HeroClass::Tat;
    Game g;
    g.start(cfg);
    leave_crossroads(g);

    const ScoreEntry e = entry_from(g);
    EXPECT_EQ(e.seed_text, "навь") << "a record without its seed cannot be replayed";
    EXPECT_EQ(static_cast<int>(e.cls), static_cast<int>(HeroClass::Tat));
    EXPECT_EQ(e.deepest, g.hero().deepest);
    EXPECT_EQ(e.score, g.score());
    EXPECT_FALSE(e.won);
}
