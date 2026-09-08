// SPDX-License-Identifier: MIT
//
// The generator underpins every reproducibility claim the project makes, so it
// is tested harder than its size suggests.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <set>
#include <vector>

#include "nav/rng.hpp"

using nav::Rng;

TEST(Rng, SameSeedProducesSameSequence) {
    Rng a(12345), b(12345);
    for (int i = 0; i < 1000; ++i) EXPECT_EQ(a.next(), b.next()) << "diverged at draw " << i;
}

TEST(Rng, DifferentSeedsDiverge) {
    Rng a(1), b(2);
    int same = 0;
    for (int i = 0; i < 100; ++i)
        if (a.next() == b.next()) ++same;
    EXPECT_LT(same, 3) << "two seeds produced a suspiciously similar stream";
}

TEST(Rng, RangeStaysWithinBounds) {
    Rng r(7);
    for (int i = 0; i < 20000; ++i) {
        const int v = r.range(-5, 5);
        ASSERT_GE(v, -5);
        ASSERT_LE(v, 5);
    }
}

TEST(Rng, RangeCoversEveryValue) {
    Rng r(99);
    std::set<int> seen;
    for (int i = 0; i < 5000; ++i) seen.insert(r.range(1, 6));
    EXPECT_EQ(seen.size(), 6u) << "a die roll never produced some faces";
}

TEST(Rng, InvertedRangeReturnsLowBoundInsteadOfMisbehaving) {
    Rng r(3);
    EXPECT_EQ(r.range(10, 4), 10);
    EXPECT_EQ(r.range(0, 0), 0);
}

TEST(Rng, BelowRejectsNonPositiveBounds) {
    Rng r(3);
    EXPECT_EQ(r.below(0), 0);
    EXPECT_EQ(r.below(-4), 0);
}

TEST(Rng, ChanceHonoursTheExtremes) {
    Rng r(11);
    for (int i = 0; i < 200; ++i) {
        EXPECT_FALSE(r.chance(0));
        EXPECT_TRUE(r.chance(100));
    }
}

TEST(Rng, ChanceIsRoughlyCalibrated) {
    Rng r(21);
    int hits = 0;
    const int trials = 40000;
    for (int i = 0; i < trials; ++i)
        if (r.chance(30)) ++hits;
    const double rate = static_cast<double>(hits) / trials;
    EXPECT_NEAR(rate, 0.30, 0.02);
}

TEST(Rng, WeightedRespectsZeroWeights) {
    Rng r(5);
    // Only index 2 may ever be chosen.
    const std::vector<int> weights{0, 0, 7, 0};
    for (int i = 0; i < 500; ++i) EXPECT_EQ(r.weighted(weights), 2);
}

TEST(Rng, WeightedReturnsMinusOneWhenNothingIsEligible) {
    Rng r(5);
    EXPECT_EQ(r.weighted({0, 0, 0}), -1);
    EXPECT_EQ(r.weighted({}), -1);
}

TEST(Rng, WeightedIsProportional) {
    Rng r(31);
    const std::vector<int> weights{1, 3};  // expect roughly 25% / 75%
    int first = 0;
    const int trials = 20000;
    for (int i = 0; i < trials; ++i)
        if (r.weighted(weights) == 0) ++first;
    EXPECT_NEAR(static_cast<double>(first) / trials, 0.25, 0.02);
}

TEST(Rng, ShuffleKeepsEveryElement) {
    Rng r(17);
    std::vector<int> v;
    for (int i = 0; i < 50; ++i) v.push_back(i);
    r.shuffle(v);

    std::vector<int> sorted = v;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < 50; ++i) EXPECT_EQ(sorted[static_cast<std::size_t>(i)], i);
    EXPECT_NE(v, sorted) << "a 50-element shuffle returned the identity permutation";
}

TEST(Rng, StateCanBeCapturedAndRestored) {
    Rng r(2024);
    for (int i = 0; i < 37; ++i) r.next();

    std::array<std::uint64_t, 4> snapshot{};
    for (int i = 0; i < 4; ++i) snapshot[static_cast<std::size_t>(i)] = r.state()[i];

    const std::uint64_t expected = r.next();

    Rng restored(1);
    restored.set_state(snapshot.data());
    EXPECT_EQ(restored.next(), expected) << "restoring the state did not resume the sequence";
}

TEST(Rng, HashSeedIsStableAndNonZero) {
    EXPECT_EQ(Rng::hash_seed("кощей"), Rng::hash_seed("кощей"));
    EXPECT_NE(Rng::hash_seed("кощей"), Rng::hash_seed("кащей"));
    EXPECT_NE(Rng::hash_seed(""), 0u) << "an empty seed must not collapse the generator";
}

TEST(Rng, UnitStaysInTheHalfOpenInterval) {
    Rng r(64);
    for (int i = 0; i < 10000; ++i) {
        const double v = r.unit();
        ASSERT_GE(v, 0.0);
        ASSERT_LT(v, 1.0);
    }
}

TEST(Rng, DiceAddsTheBonusAndStaysInRange) {
    Rng r(8);
    for (int i = 0; i < 5000; ++i) {
        const int v = r.dice(3, 6, 2);
        ASSERT_GE(v, 5);   // 3 * 1 + 2
        ASSERT_LE(v, 20);  // 3 * 6 + 2
    }
}
