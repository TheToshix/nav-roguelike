// SPDX-License-Identifier: MIT
//
// Whole-game tests. Instead of asserting on one expected outcome, these play
// long randomised runs and check that a set of invariants holds after *every*
// turn — the cheapest way to catch a rule that only misbehaves in a state no
// hand-written scenario would think to build.
#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>

#include "nav/fov.hpp"
#include "nav/game.hpp"

#include "support.hpp"

using namespace nav;

namespace {

/// Invariants that must hold at every moment of every run.
void check_invariants(const Game& g, const char* where) {
    const Hero& h = g.hero();

    ASSERT_GE(h.a.hp, 0) << where << ": health went negative";
    ASSERT_LE(h.a.hp, h.a.max_hp) << where << ": health exceeded the maximum";
    ASSERT_GE(h.mana, 0) << where << ": power went negative";
    ASSERT_LE(h.mana, h.max_mana) << where << ": power exceeded the maximum";
    ASSERT_GE(h.gold, 0) << where << ": negative gold";
    ASSERT_GE(h.level, 1) << where;
    // Zero is the crossroads, which a run may climb back to from the first
    // floor; anything below it is a bug.
    ASSERT_GE(g.depth(), kLobbyDepth) << where;
    ASSERT_LE(g.depth(), kMaxDepth) << where << ": descended past the bottom floor";
    ASSERT_LE(h.inv.items.size(), Inventory::kCapacity) << where << ": the pack overflowed";

    ASSERT_TRUE(g.map().in_bounds(h.a.pos)) << where << ": the hero left the map";
    if (h.a.alive)
        ASSERT_TRUE(g.map().walkable(h.a.pos)) << where << ": the hero is standing inside a wall";

    // Equipment indices must always address a real item of the right kind.
    const auto& inv = h.inv;
    const int size = static_cast<int>(inv.items.size());
    ASSERT_TRUE(inv.weapon == -1 || (inv.weapon >= 0 && inv.weapon < size)) << where;
    ASSERT_TRUE(inv.armor == -1 || (inv.armor >= 0 && inv.armor < size)) << where;
    ASSERT_TRUE(inv.amulet == -1 || (inv.amulet >= 0 && inv.amulet < size)) << where;
    if (inv.weapon >= 0)
        ASSERT_EQ(inv.items[static_cast<std::size_t>(inv.weapon)].kind, ItemKind::Weapon) << where;
    if (inv.armor >= 0)
        ASSERT_EQ(inv.items[static_cast<std::size_t>(inv.armor)].kind, ItemKind::Armor) << where;
    if (inv.amulet >= 0)
        ASSERT_EQ(inv.items[static_cast<std::size_t>(inv.amulet)].kind, ItemKind::Amulet) << where;

    for (const auto& it : inv.items) ASSERT_GT(it.count, 0) << where << ": an empty stack survived";

    // No two living creatures may share a cell, and none may be inside a wall.
    std::set<std::pair<int, int>> occupied;
    for (const auto& m : g.monsters()) {
        ASSERT_TRUE(m.a.alive) << where << ": a corpse was left in the monster list";
        ASSERT_GT(m.a.hp, 0) << where << ": a monster is alive at zero health";
        ASSERT_TRUE(g.map().walkable(m.a.pos)) << where << ": a monster is inside a wall";
        ASSERT_NE(m.a.pos, h.a.pos) << where << ": a monster is standing on the hero";
        const auto key = std::make_pair(m.a.pos.x, m.a.pos.y);
        ASSERT_TRUE(occupied.insert(key).second)
            << where << ": two monsters share cell (" << m.a.pos.x << "," << m.a.pos.y << ")";
        ASSERT_GE(m.species, 0) << where;
        ASSERT_LT(m.species, static_cast<int>(bestiary().size())) << where;
    }

    for (const auto& it : g.floor_items())
        ASSERT_TRUE(g.map().walkable(it.pos)) << where << ": an item is buried in a wall";
}

/// Plays a run with a mixed policy that touches every action type.
Game play(std::uint64_t seed, int steps, bool verify_each_turn) {
    GameConfig cfg;
    cfg.seed = seed;
    cfg.hero_class = static_cast<HeroClass>(seed % 3);

    Game g;
    g.start(cfg);
    leave_crossroads(g);
    Rng policy(seed * 31 + 7);

    for (int i = 0; i < steps && g.state() == RunState::Playing; ++i) {
        const int roll = policy.below(100);
        if (roll < 55) {
            g.perform(Action{ActionType::Move,
                             directions8()[static_cast<std::size_t>(policy.below(8))], -1, {}});
        } else if (roll < 62) {
            g.perform(Action{ActionType::PickUp, {}, -1, {}});
        } else if (roll < 70 && !g.hero().inv.items.empty()) {
            g.perform(Action{ActionType::UseItem, {},
                             policy.below(static_cast<int>(g.hero().inv.items.size())), {}});
        } else if (roll < 74 && !g.hero().inv.items.empty()) {
            g.perform(Action{ActionType::DropItem, {},
                             policy.below(static_cast<int>(g.hero().inv.items.size())), {}});
        } else if (roll < 82) {
            const auto spells = g.castable_spells();
            if (!spells.empty()) {
                const Spell s =
                    spells[static_cast<std::size_t>(policy.below(static_cast<int>(spells.size())))];
                const auto targets = g.spell_targets(s);
                g.perform(Action{ActionType::CastSpell, {}, static_cast<int>(s),
                                 targets.empty() ? Vec2{-1, -1} : targets.front()});
            }
        } else if (roll < 88) {
            g.perform(Action{ActionType::Descend, {}, -1, {}});
        } else if (roll < 91) {
            g.perform(Action{ActionType::Ascend, {}, -1, {}});
        } else if (roll < 94) {
            g.perform(Action{ActionType::Pray, {}, -1, {}});
        } else {
            g.perform(Action{ActionType::Wait, {}, -1, {}});
        }

        if (verify_each_turn) {
            check_invariants(g, "mid-run");
            if (::testing::Test::HasFatalFailure()) return g;
        }
    }
    return g;
}

}  // namespace

TEST(Integration, InvariantsHoldThroughoutLongRandomisedRuns) {
    for (std::uint64_t seed = 0; seed < 12; ++seed) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        play(seed + 100, 1500, /*verify_each_turn=*/true);
        if (::testing::Test::HasFatalFailure()) return;
    }
}

TEST(Integration, ManySeedsSurviveWithoutInvariantChecksPerTurn) {
    // A cheaper, wider sweep: the point is that nothing crashes, hangs or ends
    // in an impossible state.
    for (std::uint64_t seed = 0; seed < 60; ++seed) {
        Game g = play(seed + 5000, 800, /*verify_each_turn=*/false);
        SCOPED_TRACE("seed " + std::to_string(seed));
        check_invariants(g, "end of run");
        if (::testing::Test::HasFatalFailure()) return;
    }
}

TEST(Integration, TheSameSeedAndActionsProduceTheSameRun) {
    // The reproducibility promise: a seed plus a list of keystrokes is enough
    // to replay a bug report exactly.
    for (std::uint64_t seed : {1ULL, 42ULL, 12345ULL}) {
        const Game a = play(seed, 600, false);
        const Game b = play(seed, 600, false);
        EXPECT_EQ(a.save(), b.save()) << "two identical runs of seed " << seed << " diverged";
    }
}

TEST(Integration, DifferentSeedsProduceDifferentRuns) {
    EXPECT_NE(play(1, 400, false).save(), play(2, 400, false).save());
}

TEST(Integration, TheTurnCounterOnlyEverMovesForward) {
    GameConfig cfg;
    cfg.seed = 24;
    Game g;
    g.start(cfg);
    leave_crossroads(g);

    int previous = g.turn();
    Rng policy(24);
    for (int i = 0; i < 800 && g.state() == RunState::Playing; ++i) {
        g.perform(Action{ActionType::Move,
                         directions8()[static_cast<std::size_t>(policy.below(8))], -1, {}});
        ASSERT_GE(g.turn(), previous) << "the clock ran backwards";
        previous = g.turn();
    }
}

TEST(Integration, ARefusedActionNeverAdvancesTheClock) {
    GameConfig cfg;
    cfg.seed = 25;
    Game g;
    g.start(cfg);
    leave_crossroads(g);

    const int turn = g.turn();
    EXPECT_FALSE(g.perform(Action{ActionType::None, {}, -1, {}}));
    EXPECT_FALSE(g.perform(Action{ActionType::UseItem, {}, 9999, {}}));
    EXPECT_FALSE(g.perform(Action{ActionType::Move, {0, 0}, -1, {}}));
    EXPECT_EQ(g.turn(), turn);
}

TEST(Integration, EveryFloorCanBeGeneratedAndIsConnected) {
    // Walk the whole dungeon by force, checking each floor as it appears.
    GameConfig cfg;
    cfg.seed = 31415;
    Game g;
    g.start(cfg);
    leave_crossroads(g);

    for (int depth = 1; depth <= kMaxDepth; ++depth) {
        ASSERT_EQ(g.depth(), depth);
        EXPECT_TRUE(is_fully_connected(g.map(), g.hero().a.pos))
            << "floor " << depth << " has an unreachable region";
        EXPECT_FALSE(g.map().walkable_cells().empty()) << "floor " << depth << " is solid rock";

        if (depth == kMaxDepth) break;
        g.mutable_hero().a.pos = g.level().exit;
        ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}))
            << "could not descend from floor " << depth;
    }
}

TEST(Integration, EveryBossFloorActuallyContainsItsBoss) {
    GameConfig cfg;
    cfg.seed = 27182;
    Game g;
    g.start(cfg);
    leave_crossroads(g);

    for (int depth = 1; depth <= kMaxDepth; ++depth) {
        const char* expected = boss_for_depth(depth);
        if (expected) {
            bool found = false;
            for (const auto& m : g.monsters()) {
                const auto& sp = bestiary()[static_cast<std::size_t>(m.species)];
                if (std::string(sp.key) == expected) found = true;
            }
            EXPECT_TRUE(found) << "floor " << depth << " is missing its boss (" << expected << ")";
        }
        if (depth == kMaxDepth) break;
        g.mutable_hero().a.pos = g.level().exit;
        ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
}

TEST(Integration, TheBottomFloorHasNoStairsDown) {
    GameConfig cfg;
    cfg.seed = 999;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    for (int depth = 1; depth < kMaxDepth; ++depth) {
        g.mutable_hero().a.pos = g.level().exit;
        ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
    ASSERT_EQ(g.depth(), kMaxDepth);

    for (int y = 0; y < g.map().height(); ++y)
        for (int x = 0; x < g.map().width(); ++x)
            EXPECT_NE(g.map().at({x, y}), Tile::StairsDown)
                << "there is a way down from the bottom floor at (" << x << "," << y << ")";
}

TEST(Integration, MonstersOnlyEverSpawnWithinTheirDepthWindow) {
    for (std::uint64_t seed = 0; seed < 8; ++seed) {
        GameConfig cfg;
        cfg.seed = seed + 700;
        Game g;
        g.start(cfg);
        leave_crossroads(g);

        for (int depth = 1; depth <= kMaxDepth; ++depth) {
            for (const auto& m : g.monsters()) {
                const auto& sp = bestiary()[static_cast<std::size_t>(m.species)];
                if (sp.weight == 0) continue;  // bosses are placed by hand
                EXPECT_GE(depth, sp.min_depth) << sp.key << " appeared on floor " << depth;
                EXPECT_LE(depth, sp.max_depth) << sp.key << " appeared on floor " << depth;
            }
            if (depth == kMaxDepth) break;
            g.mutable_hero().a.pos = g.level().exit;
            ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
        }
    }
}

TEST(Integration, NoMonsterStartsAdjacentToTheArrivalStaircase) {
    // Regression: a level that spawns its garrison on top of the entrance kills
    // the hero before the first keystroke.
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        GameConfig cfg;
        cfg.seed = seed + 20000;
        Game g;
        g.start(cfg);
        leave_crossroads(g);

        for (int depth = 1; depth <= 6; ++depth) {
            for (const auto& m : g.monsters())
                EXPECT_GT(chebyshev(m.a.pos, g.hero().a.pos), 1)
                    << "seed " << seed << ", floor " << depth
                    << ": a monster is already in melee range on arrival";
            g.mutable_hero().a.pos = g.level().exit;
            ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
        }
    }
}

TEST(Integration, TheHeroCanAlwaysReachTheStairsFromWhereTheyStart) {
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        GameConfig cfg;
        cfg.seed = seed + 800;
        Game g;
        g.start(cfg);
        leave_crossroads(g);
        for (int depth = 1; depth <= 6; ++depth) {
            const auto path = find_path(g.map(), g.hero().a.pos, g.level().exit, 20000);
            ASSERT_FALSE(path.empty())
                << "seed " << seed << ", floor " << depth << ": the way down is unreachable";
            g.mutable_hero().a.pos = g.level().exit;
            ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
        }
    }
}

TEST(Integration, TheFieldOfViewAlwaysIncludesTheHeroAndNothingOutOfRange) {
    GameConfig cfg;
    cfg.seed = 4096;
    Game g;
    g.start(cfg);
    leave_crossroads(g);

    Rng policy(4096);
    for (int i = 0; i < 400 && g.state() == RunState::Playing; ++i) {
        g.perform(Action{ActionType::Move,
                         directions8()[static_cast<std::size_t>(policy.below(8))], -1, {}});
        ASSERT_TRUE(g.map().visible(g.hero().a.pos)) << "the hero cannot see their own cell";

        const int radius = g.hero_sight();
        for (int y = 0; y < g.map().height(); ++y)
            for (int x = 0; x < g.map().width(); ++x)
                if (g.map().visible({x, y}))
                    ASSERT_LE(chebyshev({x, y}, g.hero().a.pos), radius)
                        << "cell (" << x << "," << y << ") is lit beyond the sight radius";
    }
}

TEST(Integration, StarvationEventuallyKillsAnIdleHero) {
    // The hunger clock is what stops a player from grinding forever in a corner.
    GameConfig cfg;
    cfg.seed = 12;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    g.mutable_level().monsters.clear();
    g.mutable_hero().inv.items.clear();
    g.mutable_hero().nutrition = 5;

    for (int i = 0; i < 5000 && g.state() == RunState::Playing; ++i)
        g.perform(Action{ActionType::Wait, {}, -1, {}});

    EXPECT_EQ(g.state(), RunState::Dead) << "an unfed hero survived indefinitely";
}

TEST(Integration, TheMessageLogIsBoundedOverALongRun) {
    Game g = play(77, 4000, false);
    EXPECT_LE(g.log().size(), 400u) << "the log grows without limit";
    EXPECT_FALSE(g.log().empty());
    for (const auto& entry : g.log()) {
        EXPECT_FALSE(entry.text.ru.empty()) << "a log entry has no Russian text";
        EXPECT_FALSE(entry.text.en.empty()) << "a log entry has no English text";
    }
}

TEST(Integration, RenderingIsDefinedForEveryCellOfEveryFloor) {
    GameConfig cfg;
    cfg.seed = 5150;
    Game g;
    g.start(cfg);
    leave_crossroads(g);

    for (int depth = 1; depth <= 4; ++depth) {
        g.mutable_level().map.reveal_all();
        for (int y = 0; y < g.map().height(); ++y)
            for (int x = 0; x < g.map().width(); ++x) {
                const RenderCell cell = g.render_at({x, y});
                ASSERT_NE(cell.color, nullptr) << "cell (" << x << "," << y << ") has no colour";
                ASSERT_EQ(cell.color[0], '#') << "colours must be hex literals for both frontends";
            }
        g.mutable_hero().a.pos = g.level().exit;
        ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
}

TEST(Integration, TheScoreRewardsProgress) {
    GameConfig cfg;
    cfg.seed = 606;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    const int start_score = g.score();

    g.mutable_hero().gold += 500;
    EXPECT_GT(g.score(), start_score);

    const int with_gold = g.score();
    g.mutable_hero().a.pos = g.level().exit;
    ASSERT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    EXPECT_GT(g.score(), with_gold) << "going deeper must be worth something";
}

TEST(Integration, EveryHeroClassIsPlayable) {
    for (const auto& cls : class_table()) {
        GameConfig cfg;
        cfg.seed = 2020;
        cfg.hero_class = cls.cls;
        Game g;
        g.start(cfg);
        leave_crossroads(g);

        EXPECT_EQ(g.hero().cls, cls.cls);
        EXPECT_GT(g.hero().a.max_hp, 0);
        EXPECT_GE(g.hero().inv.items.size(), 2u) << cls.name.en << " started with nothing";
        EXPECT_GE(g.hero().inv.weapon, 0) << cls.name.en << " started unarmed";
        EXPECT_GE(g.hero().inv.armor, 0) << cls.name.en << " started unarmoured";
        EXPECT_GT(g.hero_attack(), 0);

        Game played = play(2020, 300, false);
        check_invariants(played, cls.name.en.c_str());
        if (::testing::Test::HasFatalFailure()) return;
    }
}

TEST(Integration, ARunCanBeSavedAndResumedMidFlight) {
    Game original = play(4747, 400, false);
    if (original.state() != RunState::Playing) GTEST_SKIP() << "this seed ended early";

    Game resumed;
    ASSERT_TRUE(resumed.load(original.save()));

    // Continue both by the same script; they must stay in step.
    for (int i = 0; i < 200; ++i) {
        const Action action{ActionType::Move, directions8()[static_cast<std::size_t>(i % 8)], -1, {}};
        original.perform(action);
        resumed.perform(action);
    }
    EXPECT_EQ(original.save(), resumed.save()) << "a resumed run diverged from the original";
}
