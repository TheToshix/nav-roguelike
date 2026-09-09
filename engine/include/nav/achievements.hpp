// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

#include "nav/text.hpp"

namespace nav {

class Game;

/// One thing a player can do once and keep for good.
///
/// Lives in the engine, next to the score table and for the same reason: both
/// frontends show the same list and check it the same way, and the engine turns
/// a finished run into a set of keys while the frontends only decide where that
/// set is stored — a file, or the browser.
struct AchievementInfo {
    const char* key;   ///< Stable identifier written to the store.
    Text name;
    Text how;          ///< What earns it, shown as the goal when it is still locked.
};

/// The full list, in display order.
const std::vector<AchievementInfo>& achievement_table();

/// The keys the run in `g` has earned as it stands right now. Called at the end
/// of a run, but every check is a plain read of run state, so it is safe any time.
std::vector<std::string> achievements_earned(const Game& g);

/// Serialisation of the "unlocked" set. Round-trips through `parse_achievements`.
std::string serialize_achievements(const std::vector<std::string>& unlocked);

/// Reads a blob back into `out` (unknown keys dropped, order normalised to the
/// table). Returns false and leaves `out` empty on a malformed header.
bool parse_achievements(const std::string& blob, std::vector<std::string>& out);

/// Adds `earned` to `unlocked` (deduplicated, table order). Returns the keys
/// that were not already there — what the ending screen announces.
std::vector<std::string> merge_achievements(std::vector<std::string>& unlocked,
                                            const std::vector<std::string>& earned);

}  // namespace nav
