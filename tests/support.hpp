// SPDX-License-Identifier: MIT
//
// Shared helpers for the test suite.
#pragma once

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

}  // namespace nav
