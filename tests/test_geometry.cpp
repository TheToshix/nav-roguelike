// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>

#include <set>

#include "nav/geometry.hpp"
#include "nav/text.hpp"

using namespace nav;

TEST(Geometry, ChebyshevCountsDiagonalStepsAsOne) {
    EXPECT_EQ(chebyshev({0, 0}, {3, 3}), 3);
    EXPECT_EQ(chebyshev({0, 0}, {5, 2}), 5);
    EXPECT_EQ(chebyshev({4, 4}, {4, 4}), 0);
    EXPECT_EQ(chebyshev({-2, -2}, {1, 1}), 3);
}

TEST(Geometry, ManhattanCountsDiagonalStepsAsTwo) {
    EXPECT_EQ(manhattan({0, 0}, {3, 3}), 6);
    EXPECT_EQ(manhattan({0, 0}, {0, 4}), 4);
}

TEST(Geometry, StepTowardsIsClampedToOneCell) {
    EXPECT_EQ(step_towards({0, 0}, {10, 0}), (Vec2{1, 0}));
    EXPECT_EQ(step_towards({0, 0}, {-10, -10}), (Vec2{-1, -1}));
    EXPECT_EQ(step_towards({5, 5}, {5, 5}), (Vec2{0, 0}));
}

TEST(Geometry, DirectionsAreEightDistinctUnitVectors) {
    std::set<std::pair<int, int>> seen;
    for (Vec2 d : directions8()) {
        EXPECT_LE(std::abs(d.x), 1);
        EXPECT_LE(std::abs(d.y), 1);
        EXPECT_FALSE(d.x == 0 && d.y == 0);
        seen.insert({d.x, d.y});
    }
    EXPECT_EQ(seen.size(), 8u);
}

TEST(Geometry, RectEdgesAreInclusive) {
    const Rect r{2, 3, 4, 5};
    EXPECT_EQ(r.left(), 2);
    EXPECT_EQ(r.right(), 5);
    EXPECT_EQ(r.top(), 3);
    EXPECT_EQ(r.bottom(), 7);
    EXPECT_EQ(r.area(), 20);
    EXPECT_TRUE(r.contains({2, 3}));
    EXPECT_TRUE(r.contains({5, 7}));
    EXPECT_FALSE(r.contains({6, 7}));
    EXPECT_FALSE(r.contains({1, 3}));
}

TEST(Geometry, RectIntersectionRespectsPadding) {
    const Rect a{0, 0, 4, 4};   // covers 0..3
    const Rect b{4, 0, 4, 4};   // covers 4..7 — adjacent, not overlapping
    EXPECT_FALSE(a.intersects(b));
    EXPECT_TRUE(a.intersects(b, 1)) << "padding of one must catch touching rooms";

    const Rect c{2, 2, 4, 4};
    EXPECT_TRUE(a.intersects(c));
}

TEST(Geometry, ShrunkNeverProducesNegativeExtents) {
    const Rect tiny{0, 0, 2, 2};
    const Rect s = tiny.shrunk(5);
    EXPECT_GE(s.w, 0);
    EXPECT_GE(s.h, 0);
}

TEST(Geometry, Vec2OrderingIsTotalAndStable) {
    EXPECT_TRUE((Vec2{1, 0} < Vec2{0, 1}));   // y dominates
    EXPECT_FALSE((Vec2{0, 1} < Vec2{1, 0}));
    EXPECT_TRUE((Vec2{0, 0} < Vec2{1, 0}));
    EXPECT_FALSE((Vec2{2, 2} < Vec2{2, 2}));
}

// --- Bilingual text -------------------------------------------------------

TEST(TextFormat, SubstitutesEachLanguageIndependently) {
    const Text pattern{"{} бьёт {}.", "{} hits {}."};
    const Text result = format(pattern, Text{"Упырь", "Upyr"}, Text{"тебя", "you"});
    EXPECT_EQ(result.ru, "Упырь бьёт тебя.");
    EXPECT_EQ(result.en, "Upyr hits you.");
}

TEST(TextFormat, LeavesExtraPlaceholdersAloneAndIgnoresExtraArguments) {
    EXPECT_EQ(format(Text{"{} и {}", "{} and {}"}, Text{"а", "a"}).ru, "а и {}");
    EXPECT_EQ(format(Text{"{}", "{}"}, Text{"а", "a"}, Text{"б", "b"}).ru, "а");
}

TEST(TextFormat, HandlesNumbersAndEmptyPatterns) {
    EXPECT_EQ(format(Text{"урон {}", "damage {}"}, num(42)).en, "damage 42");
    EXPECT_TRUE(format(Text{"", ""}, num(1)).empty());
}

TEST(TextFormat, GetSelectsTheRequestedLanguage) {
    const Text t{"да", "yes"};
    EXPECT_EQ(t.get(Lang::Ru), "да");
    EXPECT_EQ(t.get(Lang::En), "yes");
}
