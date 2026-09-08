// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>

#include "nav/entity.hpp"

using namespace nav;

namespace {

Actor make_actor(int hp = 40, int speed = 100) {
    Actor a;
    a.hp = a.max_hp = hp;
    a.attack = 6;
    a.defence = 2;
    a.speed = speed;
    return a;
}

}  // namespace

TEST(Effects, AddingAnEffectMakesItActive) {
    Actor a = make_actor();
    EXPECT_FALSE(a.has(Effect::Poison));
    a.add_effect(Effect::Poison, 5, 3);
    EXPECT_TRUE(a.has(Effect::Poison));
    EXPECT_EQ(a.effect_power(Effect::Poison), 3);
}

TEST(Effects, ReapplyingKeepsTheLongerDurationAndStrongerPower) {
    Actor a = make_actor();
    a.add_effect(Effect::Poison, 10, 2);
    a.add_effect(Effect::Poison, 4, 5);
    ASSERT_EQ(a.effects.size(), 1u) << "the same effect must not be stored twice";
    EXPECT_EQ(a.effects[0].turns, 10) << "a shorter reapplication must not cut the duration";
    EXPECT_EQ(a.effects[0].power, 5) << "a stronger reapplication must raise the power";
}

TEST(Effects, ClearRemovesOnlyTheNamedEffect) {
    Actor a = make_actor();
    a.add_effect(Effect::Poison, 5, 1);
    a.add_effect(Effect::Haste, 5, 1);
    a.clear_effect(Effect::Poison);
    EXPECT_FALSE(a.has(Effect::Poison));
    EXPECT_TRUE(a.has(Effect::Haste));
}

TEST(Effects, AnExpiredEffectNoLongerCounts) {
    Actor a = make_actor();
    a.add_effect(Effect::Burn, 0, 4);
    EXPECT_FALSE(a.has(Effect::Burn)) << "a zero-turn effect is already over";
    EXPECT_EQ(a.effect_power(Effect::Burn), 0);
}

TEST(Effects, PowerReportsTheStrongestActiveInstance) {
    Actor a = make_actor();
    a.effects.push_back(ActiveEffect{Effect::Might, 3, 2});
    a.effects.push_back(ActiveEffect{Effect::Might, 5, 7});
    EXPECT_EQ(a.effect_power(Effect::Might), 7);
}

TEST(Effects, HasteAndSlowScaleTheSpeed) {
    Actor a = make_actor(40, 100);
    EXPECT_EQ(a.effective_speed(), 100);

    a.add_effect(Effect::Haste, 5, 1);
    EXPECT_EQ(a.effective_speed(), 150);

    a.clear_effect(Effect::Haste);
    a.add_effect(Effect::Slow, 5, 1);
    EXPECT_EQ(a.effective_speed(), 66);
}

TEST(Effects, SpeedNeverFallsFarEnoughToStallTheScheduler) {
    // A creature with zero energy gain would never take a turn, and the turn
    // loop would spin until its guard tripped.
    Actor a = make_actor(40, 30);
    a.add_effect(Effect::Slow, 5, 1);
    EXPECT_GE(a.effective_speed(), 25);
}

TEST(Effects, FreezingPreventsActing) {
    Actor a = make_actor();
    EXPECT_TRUE(a.can_act());
    a.add_effect(Effect::Freeze, 3, 1);
    EXPECT_FALSE(a.can_act());
    a.clear_effect(Effect::Freeze);
    EXPECT_TRUE(a.can_act());
}

TEST(Effects, TheDeadCannotAct) {
    Actor a = make_actor(5);
    a.damage(10);
    EXPECT_FALSE(a.alive);
    EXPECT_FALSE(a.can_act());
}

// --- Damage and healing ----------------------------------------------------

TEST(Actor, DamageClampsAtZeroAndFlagsDeath) {
    Actor a = make_actor(10);
    a.damage(4);
    EXPECT_EQ(a.hp, 6);
    EXPECT_TRUE(a.alive);

    a.damage(99);
    EXPECT_EQ(a.hp, 0) << "health must never go negative";
    EXPECT_FALSE(a.alive);
}

TEST(Actor, ExactlyLethalDamageKills) {
    Actor a = make_actor(10);
    a.damage(10);
    EXPECT_EQ(a.hp, 0);
    EXPECT_FALSE(a.alive);
}

TEST(Actor, HealingIsCappedAtTheMaximum) {
    Actor a = make_actor(30);
    a.damage(20);
    ASSERT_EQ(a.hp, 10);
    a.heal(5);
    EXPECT_EQ(a.hp, 15);
    a.heal(1000);
    EXPECT_EQ(a.hp, 30) << "healing must not exceed the maximum";
}

// --- Hero-specific state ---------------------------------------------------

TEST(Hero, LearningASpellIsRecordedAndIndependent) {
    Hero h;
    h.spells.assign(static_cast<std::size_t>(Spell::Count), 0);
    EXPECT_FALSE(h.knows(Spell::FireArrow));

    h.learn(Spell::FireArrow);
    EXPECT_TRUE(h.knows(Spell::FireArrow));
    EXPECT_FALSE(h.knows(Spell::Lightning));
}

TEST(Hero, LearningGrowsTheSpellListWhenItIsTooShort) {
    Hero h;  // spells starts empty
    h.learn(Spell::Ward);
    EXPECT_TRUE(h.knows(Spell::Ward));
    EXPECT_FALSE(h.knows(Spell::Heal));
}
