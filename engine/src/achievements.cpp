// SPDX-License-Identifier: MIT
//
// Achievements: earned once, kept between runs. Stored separately from the score
// table, in the same plain-text, strictly-parsed style.
#include "nav/achievements.hpp"

#include <algorithm>
#include <sstream>

#include "nav/data.hpp"
#include "nav/game.hpp"

namespace nav {
namespace {

constexpr const char* kMagic = "NAVFEATS";
constexpr int kVersion = 1;

}  // namespace

const std::vector<AchievementInfo>& achievement_table() {
    static const std::vector<AchievementInfo> table = {
        {"ascended", Text{"Исход из Нави", "Out of Nav"},
         Text{"Повергнуть Змея Горыныча и подняться на свет.",
              "Put down Zmey Gorynych and climb back to the light."}},
        {"flawless_win", Text{"Неприкосновенный", "Untouched"},
         Text{"Победить, не потеряв ни одного очка здоровья за всю партию.",
              "Win without losing a single point of health the whole run."}},
        {"onfoot", Text{"Своим ходом", "On foot"},
         Text{"Победить, ни разу не воспользовавшись бегом и обходом.",
              "Win without ever using the run or the auto-explore command."}},
        {"speedrun", Text{"Скорый спуск", "Quick descent"},
         Text{"Победить за 15000 ходов или меньше.",
              "Win in 15000 turns or fewer."}},
        {"deathless8", Text{"Ни царапины", "Not a scratch"},
         Text{"Дойти до восьмого этажа, не получив ни одного удара.",
              "Reach the eighth floor having taken no damage at all."}},
        {"codex_full", Text{"Всех переписал", "All catalogued"},
         Text{"Открыть в кодексе каждую строку бестиария.",
              "Unlock every row of the bestiary in the codex."}},
        {"won_vityaz",  Text{"Витязем",   "As the Vityaz"},  Text{"Победить Витязем.",  "Win as the Vityaz."}},
        {"won_vedun",   Text{"Ведуном",   "As the Vedun"},   Text{"Победить Ведуном.",  "Win as the Vedun."}},
        {"won_tat",     Text{"Татем",     "As the Tat"},     Text{"Победить Татем.",    "Win as the Tat."}},
        {"won_znahar",  Text{"Знахарем",  "As the Znahar"},  Text{"Победить Знахарем.", "Win as the Znahar."}},
        {"won_kuznets", Text{"Кузнецом",  "As the Kuznets"}, Text{"Победить Кузнецом.", "Win as the Kuznets."}},
        {"won_bogatyr", Text{"Богатырём", "As the Bogatyr"}, Text{"Победить Богатырём.","Win as the Bogatyr."}},
    };
    return table;
}

std::vector<std::string> achievements_earned(const Game& g) {
    std::vector<std::string> out;
    const bool won = g.state() == RunState::Ascended;

    if (won) out.push_back("ascended");
    if (won && g.flawless()) out.push_back("flawless_win");
    if (won && !g.used_run_or_explore()) out.push_back("onfoot");
    if (won && g.turn() <= 15000) out.push_back("speedrun");
    if (g.deepest_unhurt() >= 8) out.push_back("deathless8");

    bool codex_full = !g.codex_seen().empty();
    for (std::uint8_t v : g.codex_seen())
        if (!v) { codex_full = false; break; }
    if (codex_full) out.push_back("codex_full");

    if (won) {
        static const char* per_class[] = {"won_vityaz", "won_vedun",   "won_tat",
                                          "won_znahar", "won_kuznets", "won_bogatyr"};
        const int c = static_cast<int>(g.hero().cls);
        if (c >= 0 && c < 6) out.push_back(per_class[c]);
    }
    return out;
}

std::string serialize_achievements(const std::vector<std::string>& unlocked) {
    // Written in table order so the file reads like the screen.
    std::ostringstream out;
    std::vector<std::string> ordered;
    for (const AchievementInfo& a : achievement_table())
        if (std::find(unlocked.begin(), unlocked.end(), a.key) != unlocked.end())
            ordered.emplace_back(a.key);

    out << kMagic << ' ' << kVersion << ' ' << ordered.size() << '\n';
    for (const std::string& k : ordered) out << k << '\n';
    return out.str();
}

bool parse_achievements(const std::string& blob, std::vector<std::string>& out) {
    out.clear();
    std::istringstream in(blob);
    std::string magic;
    int version = 0;
    std::size_t count = 0;
    if (!(in >> magic >> version >> count)) return false;
    if (magic != kMagic || version != kVersion) return false;
    if (count > achievement_table().size() * 4) return false;  // clearly corrupt

    std::vector<std::string> raw;
    std::string key;
    while (in >> key) raw.push_back(key);

    // Keep only keys the current table knows, in table order — this is how a
    // renamed or removed achievement is dropped without breaking the file.
    for (const AchievementInfo& a : achievement_table())
        if (std::find(raw.begin(), raw.end(), a.key) != raw.end())
            out.emplace_back(a.key);
    return true;
}

std::vector<std::string> merge_achievements(std::vector<std::string>& unlocked,
                                            const std::vector<std::string>& earned) {
    std::vector<std::string> fresh;
    for (const AchievementInfo& a : achievement_table()) {
        const bool now = std::find(earned.begin(), earned.end(), a.key) != earned.end();
        const bool had = std::find(unlocked.begin(), unlocked.end(), a.key) != unlocked.end();
        if (now && !had) {
            unlocked.emplace_back(a.key);
            fresh.emplace_back(a.key);
        }
    }
    // Normalise the stored order to the table's.
    std::vector<std::string> ordered;
    for (const AchievementInfo& a : achievement_table())
        if (std::find(unlocked.begin(), unlocked.end(), a.key) != unlocked.end())
            ordered.emplace_back(a.key);
    unlocked = std::move(ordered);
    return fresh;
}

}  // namespace nav
