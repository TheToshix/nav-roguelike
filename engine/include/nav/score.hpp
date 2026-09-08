// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nav/types.hpp"

namespace nav {

class Game;

/// One finished run, as it appears in the table.
///
/// Kept in the engine rather than in a frontend because both frontends show the
/// same table and neither should get to disagree about how a run is scored,
/// ordered or written down. The engine still performs no I/O: it turns a run
/// into a record and a list of records into text, and the frontends decide
/// where that text lives — a file on disk, or the browser's own storage.
struct ScoreEntry {
    std::string seed_text;   ///< What the player typed, so a run can be replayed.
    HeroClass cls{HeroClass::Vityaz};
    int score{0};
    int deepest{1};
    int turns{0};
    int level{1};
    int kills{0};
    int gold{0};
    bool won{false};
};

/// How many runs the table keeps.
inline constexpr std::size_t kScoreTableSize = 10;

/// Reads a finished (or abandoned) run into a record.
ScoreEntry entry_from(const Game& g);

/// Puts `entry` in its place, keeping the table sorted and capped.
///
/// Returns the position it landed in, or -1 when it did not make the table —
/// which is what the ending screen needs in order to say "a new best" and to
/// highlight the right row.
int insert_score(std::vector<ScoreEntry>& table, const ScoreEntry& entry);

/// The table as one text blob. Round-trips through `parse_scores`.
std::string serialize_scores(const std::vector<ScoreEntry>& table);

/// Reads a blob back. Returns false and leaves `out` empty on anything
/// malformed: a corrupted table is worth losing, never worth half-reading.
bool parse_scores(const std::string& blob, std::vector<ScoreEntry>& out);

}  // namespace nav
