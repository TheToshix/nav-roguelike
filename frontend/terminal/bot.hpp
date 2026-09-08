// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nav/game.hpp"

namespace nav {

/// What one bot game produced.
struct BotResult {
    std::uint64_t seed{0};
    HeroClass cls{HeroClass::Vityaz};
    int turns{0};
    int depth{0};
    int deepest{0};
    int level{1};
    int max_hp{0};
    int kills{0};
    int gold{0};
    int score{0};
    RunState state{RunState::Playing};
    std::size_t save_bytes{0};
    bool save_round_trips{false};
    /// Species key of whatever killed the hero, or "" — this is what turns a
    /// pile of runs into a balance report rather than a pile of runs.
    std::string killed_by;
    /// Every guardian the run actually put down, in the order they fell.
    std::vector<std::string> bosses_slain;
    /// Whether the run ever found and broke Кощей's needle. Without it he
    /// cannot be killed at all, so a sweep that never breaks it has not really
    /// been to the twelfth floor.
    bool needle_broken{false};
};

/// How the bot is set up for a run.
///
/// Two questions need two instruments. `Fair` measures the curve: a hero with
/// the stats the game gives, dying where the game kills. `Sweep` measures
/// reachability: a deliberately over-levelled hero whose job is to touch every
/// floor and put down every guardian, so that the deep content is exercised by
/// a whole game and not only by arena tests. A sweep proves nothing about
/// balance and is never quoted as if it did.
enum class BotMode { Fair, Sweep };

/// Plays one whole game and returns what happened.
///
/// Deliberately not a good player: it is a harness that has to be able to reach
/// the bottom of the dungeon so that the second half of the content is exercised
/// by something other than an arena test. Every decision it makes is a
/// heuristic, and the file says which and why.
BotResult play_one(std::uint64_t seed, HeroClass cls, int max_turns = 20000,
                   BotMode mode = BotMode::Fair);

}  // namespace nav
