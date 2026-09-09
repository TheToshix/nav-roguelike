// SPDX-License-Identifier: MIT
//
// Shared helpers for the test suite.
#pragma once

#include <gtest/gtest.h>

#include "nav/game.hpp"

namespace nav {

/// Walks the hero off the crossroads and down onto the first dungeon floor.
///
/// A run now begins in a room rather than on a floor, and almost every test
/// here is about the dungeon. Rather than teach the engine a back door, this
/// does exactly what a player does: step to the stairs and take them. That
/// keeps the tests honest about the only route into the game there is.
inline void leave_crossroads(Game& g) {
    for (int guard = 0; guard < 400 && g.in_lobby(); ++guard) {
        const Vec2 me = g.hero().a.pos;
        const Vec2 stairs = g.level().exit;
        Action a{};
        if (me == stairs) {
            a.type = ActionType::Descend;
        } else {
            a.type = ActionType::Move;
            a.dir = step_towards(me, stairs);
        }
        if (!g.perform(a)) break;
    }
}

/// Walks a run down to `depth`, generating every floor on the way and leaving
/// each one as it was generated.
///
/// The hero is put on the stairs rather than made to walk to them: the walk
/// would fight monsters, spend potions and take damage, and a test about how a
/// floor is *built* has no business depending on how a fight on the floor above
/// went. Taking the stairs is still a real Descend, so the floor is generated
/// through the only path there is.
inline Game descend_to(int depth, std::uint64_t seed) {
    GameConfig cfg;
    cfg.seed = seed;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    for (int d = 1; d < depth; ++d) {
        g.mutable_hero().a.pos = g.level().exit;
        g.refresh_view();
        EXPECT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
    return g;
}

}  // namespace nav
