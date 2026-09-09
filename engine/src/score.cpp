// SPDX-License-Identifier: MIT
//
// The high-score table.
//
// A roguelike without one throws away the only record a run leaves behind. The
// format is deliberately plain text and length-prefixed where it has to hold
// arbitrary characters, for the same reason the save format is: a table that
// cannot be read by eye cannot be debugged from a bug report.
#include "nav/score.hpp"

#include <algorithm>
#include <sstream>

#include "nav/game.hpp"

namespace nav {
namespace {

constexpr const char* kMagic = "NAVSCORES";
constexpr int kVersion = 2;   // v2 adds the daily flag per row

}  // namespace

bool is_daily_seed(const std::string& seed_text) {
    const std::string prefix = kDailySeedPrefix;
    return seed_text.size() > prefix.size() && seed_text.compare(0, prefix.size(), prefix) == 0;
}

std::string daily_seed_text(long long day_index) {
    return std::string(kDailySeedPrefix) + std::to_string(day_index);
}

ScoreEntry entry_from(const Game& g) {
    ScoreEntry e;
    e.seed_text = g.config().seed_text;
    e.cls = g.hero().cls;
    e.score = g.score();
    e.deepest = g.hero().deepest;
    e.turns = g.turn();
    e.level = g.hero().level;
    e.kills = g.hero().kills;
    e.gold = g.hero().gold;
    e.won = g.state() == RunState::Ascended;
    e.daily = is_daily_seed(e.seed_text);
    return e;
}

int insert_score(std::vector<ScoreEntry>& table, const ScoreEntry& entry) {
    // Ordering is by score, and ties are broken by depth and then by speed: two
    // runs worth the same are separated by how far they got, and two that got
    // as far by which one wasted less of the dungeon's time.
    const auto better = [](const ScoreEntry& a, const ScoreEntry& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.deepest != b.deepest) return a.deepest > b.deepest;
        return a.turns < b.turns;
    };

    const auto at = std::upper_bound(table.begin(), table.end(), entry, better);
    const int index = static_cast<int>(at - table.begin());
    if (index >= static_cast<int>(kScoreTableSize)) return -1;

    table.insert(at, entry);
    if (table.size() > kScoreTableSize) table.resize(kScoreTableSize);
    return index;
}

std::string serialize_scores(const std::vector<ScoreEntry>& table) {
    std::ostringstream out;
    out << kMagic << ' ' << kVersion << ' ' << table.size() << '\n';
    for (const ScoreEntry& e : table) {
        out << e.score << ' ' << e.deepest << ' ' << e.turns << ' ' << e.level << ' '
            << e.kills << ' ' << e.gold << ' ' << static_cast<int>(e.cls) << ' '
            << (e.won ? 1 : 0) << ' ' << (e.daily ? 1 : 0) << ' '
            << e.seed_text.size() << ' ' << e.seed_text << '\n';
    }
    return out.str();
}

bool parse_scores(const std::string& blob, std::vector<ScoreEntry>& out) {
    out.clear();
    std::istringstream in(blob);

    std::string magic;
    int version = 0;
    std::size_t count = 0;
    if (!(in >> magic >> version >> count)) return false;
    if (magic != kMagic || version != kVersion) return false;
    // A table claiming more rows than it can hold is either corrupt or hostile.
    if (count > kScoreTableSize) return false;

    for (std::size_t i = 0; i < count; ++i) {
        ScoreEntry e;
        int cls = 0, won = 0, daily = 0;
        std::size_t length = 0;
        if (!(in >> e.score >> e.deepest >> e.turns >> e.level >> e.kills >> e.gold >> cls >>
              won >> daily >> length))
            return false;
        if (cls < 0 || cls >= static_cast<int>(HeroClass::Count)) return false;
        if (length > 64) return false;

        // The seed follows its own length, so it may contain spaces without
        // needing an escape that a human editing the file would have to know.
        in.get();  // the single space after the length
        std::string seed(length, '\0');
        if (length > 0 && !in.read(&seed[0], static_cast<std::streamsize>(length))) return false;

        e.cls = static_cast<HeroClass>(cls);
        e.won = won != 0;
        e.daily = daily != 0;
        e.seed_text = seed;
        if (e.deepest < 0 || e.deepest > kMaxDepth) return false;
        if (e.level < 1 || e.turns < 0 || e.kills < 0 || e.gold < 0) return false;
        out.push_back(e);
    }

    // Trailing junk means the file is not what it claims to be.
    std::string leftover;
    if (in >> leftover) { out.clear(); return false; }
    return true;
}

}  // namespace nav
