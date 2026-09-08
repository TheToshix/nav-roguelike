// SPDX-License-Identifier: MIT
//
// Terminal frontend: ANSI rendering plus raw keyboard input.
//
// It contains no game rules whatsoever — every keypress becomes a nav::Action,
// and everything drawn comes from querying nav::Game. The WebAssembly frontend
// talks to exactly the same interface.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "nav/fov.hpp"
#include "nav/game.hpp"

#if defined(_WIN32)
#  include <conio.h>
#  include <windows.h>
#else
#  include <termios.h>
#  include <unistd.h>
#endif

namespace {

using namespace nav;

// ---------------------------------------------------------------------------
// Terminal plumbing
// ---------------------------------------------------------------------------

#if defined(_WIN32)

struct RawMode {
    UINT previous_cp{0};
    RawMode() {
        previous_cp = GetConsoleOutputCP();
        SetConsoleOutputCP(CP_UTF8);
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(out, &mode))
            SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    ~RawMode() { SetConsoleOutputCP(previous_cp); }
};

int read_key() { return _getch(); }

#else

/// Puts the terminal into raw mode for the lifetime of the object and restores
/// the previous settings on the way out, including on an exception.
struct RawMode {
    termios previous{};
    bool active{false};

    RawMode() {
        if (!isatty(STDIN_FILENO)) return;
        if (tcgetattr(STDIN_FILENO, &previous) != 0) return;
        termios raw = previous;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) active = true;
    }
    ~RawMode() {
        if (active) tcsetattr(STDIN_FILENO, TCSAFLUSH, &previous);
    }
};

int read_key() {
    unsigned char c = 0;
    if (::read(STDIN_FILENO, &c, 1) != 1) return -1;
    return c;
}

#endif

/// Special keys, above the ASCII range so they never collide with a letter.
enum : int { kUp = 1000, kDown, kLeft, kRight, kEsc, kNone = -1 };

/// Reads one key, decoding the CSI escape sequences the arrow keys produce.
int read_key_decoded() {
    const int c = read_key();
    if (c != 27) return c;

#if defined(_WIN32)
    return kEsc;
#else
    // Peek: a lone Esc has nothing queued behind it.
    termios saved{};
    tcgetattr(STDIN_FILENO, &saved);
    termios peek = saved;
    peek.c_cc[VMIN] = 0;
    peek.c_cc[VTIME] = 1;  // 100 ms
    tcsetattr(STDIN_FILENO, TCSANOW, &peek);

    unsigned char b1 = 0, b2 = 0;
    const bool have1 = ::read(STDIN_FILENO, &b1, 1) == 1;
    const bool have2 = have1 && ::read(STDIN_FILENO, &b2, 1) == 1;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);

    if (!have1) return kEsc;
    if (b1 == '[' && have2) {
        switch (b2) {
            case 'A': return kUp;
            case 'B': return kDown;
            case 'C': return kRight;
            case 'D': return kLeft;
            default: return kEsc;
        }
    }
    return kEsc;
#endif
}

const char* severity_color(Severity s) {
    switch (s) {
        case Severity::Good:     return "\x1b[38;5;114m";
        case Severity::Bad:      return "\x1b[38;5;174m";
        case Severity::Critical: return "\x1b[38;5;203m";
        case Severity::System:   return "\x1b[38;5;180m";
        default:                 return "\x1b[38;5;250m";
    }
}

/// Converts one of the engine's "#rrggbb" colours into an ANSI 24-bit escape.
std::string ansi_from_hex(const char* hex) {
    if (!hex || hex[0] != '#' || std::strlen(hex) < 7) return "\x1b[0m";
    auto part = [hex](int offset) {
        return std::stoi(std::string(hex + offset, 2), nullptr, 16);
    };
    return "\x1b[38;2;" + std::to_string(part(1)) + ";" + std::to_string(part(3)) + ";" +
           std::to_string(part(5)) + "m";
}

/// Display width of a UTF-8 string in terminal cells (counts codepoints, which
/// is correct for Cyrillic and Latin alike).
std::size_t display_width(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string pad_to(const std::string& s, std::size_t width) {
    const std::size_t have = display_width(s);
    return have >= width ? s : s + std::string(width - have, ' ');
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

struct Ui {
    Lang lang{Lang::Ru};
    int view_w{58};
    int view_h{22};

    const std::string& t(const Text& text) const { return text.get(lang); }

    void clear() const { std::fputs("\x1b[2J\x1b[H", stdout); }

    void draw(const Game& g) const {
        std::string out;
        out.reserve(16384);
        out += "\x1b[H";  // home, without clearing: avoids the flicker

        const Map& map = g.map();
        const Vec2 hero = g.hero().a.pos;
        const int half_w = view_w / 2, half_h = view_h / 2;
        const int ox = std::clamp(hero.x - half_w, 0, std::max(0, map.width() - view_w));
        const int oy = std::clamp(hero.y - half_h, 0, std::max(0, map.height() - view_h));

        // --- Header ---------------------------------------------------------
        out += "\x1b[38;5;180m";
        out += lang == Lang::Ru ? "  НАВЬ  " : "   NAV  ";
        out += "\x1b[0m\x1b[38;5;244m";
        out += lang == Lang::Ru ? "— спуск в двенадцать кругов" : "— a descent through twelve circles";
        out += "\x1b[0m\x1b[K\n";

        // --- Map + sidebar --------------------------------------------------
        const std::vector<std::string> side = sidebar(g);
        for (int row = 0; row < view_h; ++row) {
            const int y = oy + row;
            std::string current;
            for (int col = 0; col < view_w; ++col) {
                const Vec2 p{ox + col, y};
                if (!map.in_bounds(p)) { out += ' '; continue; }
                const RenderCell cell = g.render_at(p);
                if (!cell.explored) { out += ' '; continue; }

                const std::string color =
                    cell.visible ? ansi_from_hex(cell.color) : std::string("\x1b[38;5;238m");
                if (color != current) { out += color; current = color; }
                out += cell.glyph;
            }
            out += "\x1b[0m  ";
            if (static_cast<std::size_t>(row) < side.size()) out += side[static_cast<std::size_t>(row)];
            out += "\x1b[K\n";
        }

        // --- Message log ----------------------------------------------------
        out += "\x1b[38;5;240m";
        out += std::string(static_cast<std::size_t>(view_w), '-');
        out += "\x1b[0m\x1b[K\n";

        const auto& log = g.log();
        const std::size_t show = 6;
        const std::size_t first = log.size() > show ? log.size() - show : 0;
        for (std::size_t i = 0; i < show; ++i) {
            if (first + i < log.size()) {
                const LogEntry& e = log[first + i];
                out += severity_color(e.severity);
                out += t(e.text);
                out += "\x1b[0m";
            }
            out += "\x1b[K\n";
        }

        out += "\x1b[38;5;244m";
        out += lang == Lang::Ru
                   ? "hjkl/стрелки — идти  g — взять  i — котомка  z — колдовать  > < — лестницы"
                   : "hjkl/arrows move  g pick up  i pack  z cast  > < stairs";
        out += "\x1b[0m\x1b[K\n";
        out += "\x1b[38;5;244m";
        out += lang == Lang::Ru ? "p — жертва  S — сохранить  T — язык  ? — помощь  q — выход"
                                : "p pray  S save  T language  ? help  q quit";
        out += "\x1b[0m\x1b[K\x1b[J";

        std::fputs(out.c_str(), stdout);
        std::fflush(stdout);
    }

    std::vector<std::string> sidebar(const Game& g) const {
        const Hero& h = g.hero();
        std::vector<std::string> rows;
        auto add = [&](const std::string& s) { rows.push_back(s); };

        add("\x1b[1m" + t(class_info(h.cls).name) + "\x1b[0m  " +
            (lang == Lang::Ru ? "ур. " : "lv ") + std::to_string(h.level));
        add(bar(lang == Lang::Ru ? "Жизнь" : "Life", h.a.hp, h.a.max_hp, "\x1b[38;5;174m"));
        if (h.max_mana > 0)
            add(bar(lang == Lang::Ru ? "Силы" : "Power", h.mana, h.max_mana, "\x1b[38;5;110m"));
        add("");
        add((lang == Lang::Ru ? "Глубина: " : "Depth:  ") + std::to_string(g.depth()) + " / " +
            std::to_string(kMaxDepth));
        add("\x1b[38;5;180m" + t(zone_theme_for_depth(g.depth()).name) + "\x1b[0m");
        add((lang == Lang::Ru ? "Удар:    " : "Attack: ") + std::to_string(g.hero_attack()));
        add((lang == Lang::Ru ? "Защита:  " : "Armour: ") + std::to_string(g.hero_defence()));
        add((lang == Lang::Ru ? "Золото:  " : "Gold:   ") + std::to_string(h.gold));
        add((lang == Lang::Ru ? "Опыт:    " : "XP:     ") + std::to_string(h.xp) + " / " +
            std::to_string(xp_for_level(h.level + 1)));
        add((lang == Lang::Ru ? "Ход:     " : "Turn:   ") + std::to_string(g.turn()));

        if (h.nutrition <= 0)
            add("\x1b[38;5;203m" + std::string(lang == Lang::Ru ? "ГОЛОД" : "STARVING") + "\x1b[0m");
        else if (h.nutrition < 200)
            add("\x1b[38;5;179m" + std::string(lang == Lang::Ru ? "голоден" : "hungry") + "\x1b[0m");
        else
            add("");

        // Active status effects.
        std::string effects;
        for (const auto& e : h.a.effects) {
            if (e.turns <= 0) continue;
            if (!effects.empty()) effects += " ";
            effects += effect_label(e.kind) + "(" + std::to_string(e.turns) + ")";
        }
        add(effects.empty() ? "" : "\x1b[38;5;180m" + effects + "\x1b[0m");
        add("");

        // Nearby monsters, so the player is never surprised by something just
        // off the edge of their attention.
        add("\x1b[38;5;244m" + std::string(lang == Lang::Ru ? "Рядом:" : "Nearby:") + "\x1b[0m");
        int listed = 0;
        for (const auto& m : g.monsters()) {
            if (listed >= 5 || !g.map().visible(m.a.pos)) continue;
            const auto& sp = bestiary()[static_cast<std::size_t>(m.species)];
            add(ansi_from_hex(sp.color) + std::string(1, sp.glyph) + "\x1b[0m " +
                pad_to(t(sp.name), 20) + std::to_string(m.a.hp) + "/" + std::to_string(m.a.max_hp));
            ++listed;
        }
        while (rows.size() < static_cast<std::size_t>(view_h)) add("");
        return rows;
    }

    std::string bar(const std::string& label, int value, int max_value, const char* color) const {
        const int width = 14;
        const int filled = max_value > 0 ? std::clamp(value * width / max_value, 0, width) : 0;
        std::string s = pad_to(label, 6) + color;
        for (int i = 0; i < width; ++i) s += i < filled ? "#" : ".";
        s += "\x1b[0m " + std::to_string(value) + "/" + std::to_string(max_value);
        return s;
    }

    std::string effect_label(Effect e) const {
        static const char* ru[] = {"яд", "огонь", "оковы", "морок", "слепота",
                                   "спешка", "вязко", "живая вода", "ярость", "оберег", "тень"};
        static const char* en[] = {"poison", "burn", "frozen", "confused", "blind",
                                   "haste", "slow", "regen", "might", "ward", "unseen"};
        const std::size_t i = static_cast<std::size_t>(e);
        if (i >= sizeof(ru) / sizeof(ru[0])) return "?";
        return lang == Lang::Ru ? ru[i] : en[i];
    }

    /// Blocking full-screen list; returns the chosen index or -1.
    int menu(const std::string& title, const std::vector<std::string>& entries,
             const std::string& footer) const {
        clear();
        std::string out = "\x1b[1m" + title + "\x1b[0m\n\n";
        for (std::size_t i = 0; i < entries.size(); ++i) {
            out += "  \x1b[38;5;180m";
            out += static_cast<char>('a' + static_cast<int>(i));
            out += "\x1b[0m) " + entries[i] + "\n";
        }
        if (entries.empty()) out += (lang == Lang::Ru ? "  (пусто)\n" : "  (empty)\n");
        out += "\n\x1b[38;5;244m" + footer + "\x1b[0m\n";
        std::fputs(out.c_str(), stdout);
        std::fflush(stdout);

        const int key = read_key_decoded();
        if (key == kEsc || key == ' ' || key == 'q') return -1;
        const int index = key - 'a';
        return (index >= 0 && index < static_cast<int>(entries.size())) ? index : -1;
    }

    void notice(const std::string& body) const {
        clear();
        std::fputs(body.c_str(), stdout);
        std::fputs(lang == Lang::Ru ? "\n\n[любая клавиша]" : "\n\n[any key]", stdout);
        std::fflush(stdout);
        read_key_decoded();
    }
};

// ---------------------------------------------------------------------------
// Input mapping
// ---------------------------------------------------------------------------

/// Maps a key to a movement vector. Returns false when it is not a move.
bool key_to_direction(int key, Vec2& dir) {
    switch (key) {
        case 'h': case '4': case kLeft:  dir = {-1,  0}; return true;
        case 'j': case '2': case kDown:  dir = { 0,  1}; return true;
        case 'k': case '8': case kUp:    dir = { 0, -1}; return true;
        case 'l': case '6': case kRight: dir = { 1,  0}; return true;
        case 'y': case '7':              dir = {-1, -1}; return true;
        case 'u': case '9':              dir = { 1, -1}; return true;
        case 'b': case '1':              dir = {-1,  1}; return true;
        case 'n': case '3':              dir = { 1,  1}; return true;
        default: return false;
    }
}

std::string inventory_line(const Game& g, std::size_t index, Lang lang) {
    const Item& it = g.hero().inv.items[index];
    std::string line = item_name(it, g.identification()).get(lang);
    if (it.count > 1) line += " x" + std::to_string(it.count);
    if (g.hero().inv.is_equipped(static_cast<int>(index)))
        line += lang == Lang::Ru ? "  [надето]" : "  [worn]";
    const std::string note = item_note(it, g.identification()).get(lang);
    if (!note.empty()) line = pad_to(line, 34) + "\x1b[38;5;244m" + note + "\x1b[0m";
    return line;
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

void show_inventory(Game& g, const Ui& ui) {
    while (true) {
        std::vector<std::string> entries;
        for (std::size_t i = 0; i < g.hero().inv.items.size(); ++i)
            entries.push_back(inventory_line(g, i, ui.lang));

        const std::string title = ui.lang == Lang::Ru ? "Котомка" : "Pack";
        const std::string footer =
            ui.lang == Lang::Ru
                ? "Выбери букву, чтобы применить или надеть. Esc — назад."
                : "Choose a letter to use or equip. Esc to go back.";
        const int pick = ui.menu(title, entries, footer);
        if (pick < 0) return;
        g.perform(Action{ActionType::UseItem, {}, pick, {}});
        return;
    }
}

std::string target_label(const Monster& m, Lang lang) {
    const auto& sp = bestiary()[static_cast<std::size_t>(m.species)];
    return sp.name.get(lang) + " (" + std::to_string(m.a.hp) + "/" +
           std::to_string(m.a.max_hp) + ")";
}

void cast_menu(Game& g, const Ui& ui) {
    const auto spells = g.castable_spells();
    if (spells.empty()) {
        ui.notice(ui.lang == Lang::Ru ? "Нечего колдовать — нет сил или заклятий."
                                      : "Nothing to cast — no power, or no spells.");
        return;
    }

    std::vector<std::string> entries;
    for (Spell s : spells) {
        const SpellTemplate& t = spell_info(s);
        entries.push_back(pad_to(t.name.get(ui.lang), 22) + "\x1b[38;5;244m" +
                          std::to_string(t.cost) + (ui.lang == Lang::Ru ? " сил  " : " power  ") +
                          t.note.get(ui.lang) + "\x1b[0m");
    }

    const int pick = ui.menu(ui.lang == Lang::Ru ? "Заклятья" : "Spells", entries,
                             ui.lang == Lang::Ru ? "Esc — назад." : "Esc to go back.");
    if (pick < 0) return;

    const Spell chosen = spells[static_cast<std::size_t>(pick)];
    Vec2 target{-1, -1};
    if (spell_info(chosen).needs_target) {
        const auto targets = g.spell_targets(chosen);
        if (targets.empty()) {
            ui.notice(ui.lang == Lang::Ru ? "Некого бить — цели вне досягаемости."
                                          : "No target within reach.");
            return;
        }
        // Cycle through candidates with any key, confirm with Enter or space.
        std::size_t index = 0;
        while (true) {
            ui.clear();
            const Monster* m = g.monster_at(targets[index]);
            std::string body = (ui.lang == Lang::Ru ? "Цель: " : "Target: ");
            body += m ? target_label(*m, ui.lang) : "?";
            body += ui.lang == Lang::Ru
                        ? "\n\nTab — следующая цель, Enter — бить, Esc — отмена."
                        : "\n\nTab for next target, Enter to cast, Esc to cancel.";
            std::fputs(body.c_str(), stdout);
            std::fflush(stdout);

            const int key = read_key_decoded();
            if (key == kEsc || key == 'q') return;
            if (key == '\r' || key == '\n' || key == ' ') break;
            index = (index + 1) % targets.size();
        }
        target = targets[index];
    }
    g.perform(Action{ActionType::CastSpell, {}, static_cast<int>(chosen), target});
}

std::string save_path() {
    if (const char* home = std::getenv("HOME")) return std::string(home) + "/.nav_save";
    return "nav_save.txt";
}

bool write_file(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << data;
    return static_cast<bool>(out);
}

bool read_file(const std::string& path, std::string& data) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    data = ss.str();
    return true;
}

const char* help_text(Lang lang) {
    if (lang == Lang::Ru) {
        return
            "\x1b[1mНАВЬ — как играть\x1b[0m\n\n"
            "  Ты спускаешься в двенадцать кругов подземного мира. Внизу ждёт Кощей.\n"
            "  Смерть окончательна: сохранение — это пауза, а не запасная жизнь.\n\n"
            "  \x1b[38;5;180mДвижение\x1b[0m   hjkl yubn, стрелки или цифровой блок. Шаг в тварь — удар.\n"
            "  \x1b[38;5;180m.\x1b[0m или 5     переждать ход\n"
            "  \x1b[38;5;180mg\x1b[0m           подобрать\n"
            "  \x1b[38;5;180mi\x1b[0m           котомка: применить, надеть, снять\n"
            "  \x1b[38;5;180md\x1b[0m           бросить вещь\n"
            "  \x1b[38;5;180mz\x1b[0m           колдовать\n"
            "  \x1b[38;5;180m>  <\x1b[0m        спуститься / подняться по лестнице\n"
            "  \x1b[38;5;180mp\x1b[0m           принести жертву на капище (_)\n"
            "  \x1b[38;5;180mS  L\x1b[0m        сохранить / загрузить\n"
            "  \x1b[38;5;180mT\x1b[0m           переключить язык\n"
            "  \x1b[38;5;180mq\x1b[0m           выйти\n\n"
            "  \x1b[38;5;244mЗнаки:\x1b[0m  @ ты   # стена   . пол   + дверь   ~ вода   > лестница\n"
            "          ! зелье   ? свиток   ) оружие   [ доспех   \" оберег   $ золото   _ капище\n\n"
            "  \x1b[38;5;244mЗелья и свитки не подписаны, пока их не испробуешь. Это часть игры.\x1b[0m";
    }
    return
        "\x1b[1mNAV — how to play\x1b[0m\n\n"
        "  You descend through twelve circles of the underworld. Koschei waits at the bottom.\n"
        "  Death is final: a save is a pause, not a spare life.\n\n"
        "  \x1b[38;5;180mMovement\x1b[0m   hjkl yubn, arrows or the numeric keypad. Step into a creature to attack.\n"
        "  \x1b[38;5;180m.\x1b[0m or 5     wait a turn\n"
        "  \x1b[38;5;180mg\x1b[0m           pick up\n"
        "  \x1b[38;5;180mi\x1b[0m           pack: use, equip, remove\n"
        "  \x1b[38;5;180md\x1b[0m           drop an item\n"
        "  \x1b[38;5;180mz\x1b[0m           cast a spell\n"
        "  \x1b[38;5;180m>  <\x1b[0m        descend / climb stairs\n"
        "  \x1b[38;5;180mp\x1b[0m           make an offering at a shrine (_)\n"
        "  \x1b[38;5;180mS  L\x1b[0m        save / load\n"
        "  \x1b[38;5;180mT\x1b[0m           switch language\n"
        "  \x1b[38;5;180mq\x1b[0m           quit\n\n"
        "  \x1b[38;5;244mGlyphs:\x1b[0m  @ you   # wall   . floor   + door   ~ water   > stairs\n"
        "          ! potion   ? scroll   ) weapon   [ armour   \" charm   $ gold   _ shrine\n\n"
        "  \x1b[38;5;244mPotions and scrolls are unlabelled until you try them. That is the game.\x1b[0m";
}

/// Title screen: language, class and seed.
bool title_screen(Ui& ui, GameConfig& cfg) {
    while (true) {
        ui.clear();
        std::string out;
        out += "\x1b[38;5;180m";
        out += R"(
        ███    ██  █████  ██    ██
        ████   ██ ██   ██ ██    ██
        ██ ██  ██ ███████ ██    ██
        ██  ██ ██ ██   ██  ██  ██
        ██   ████ ██   ██   ████
)";
        out += "\x1b[0m\n";
        out += ui.lang == Lang::Ru
                   ? "        \x1b[38;5;244mСпуск в двенадцать кругов подземного мира.\x1b[0m\n\n"
                   : "        \x1b[38;5;244mA descent through twelve circles of the underworld.\x1b[0m\n\n";

        const auto& classes = class_table();
        for (std::size_t i = 0; i < classes.size(); ++i) {
            out += "  \x1b[38;5;180m";
            out += static_cast<char>('1' + static_cast<int>(i));
            out += "\x1b[0m) \x1b[1m" + pad_to(classes[i].name.get(ui.lang), 10) + "\x1b[0m";
            out += "\x1b[38;5;244m" + classes[i].blurb.get(ui.lang) + "\x1b[0m\n";
        }
        out += ui.lang == Lang::Ru
                   ? "\n  \x1b[38;5;180ms\x1b[0m) задать зерно (сейчас: "
                   : "\n  \x1b[38;5;180ms\x1b[0m) set the seed (now: ";
        out += (cfg.seed_text.empty() ? (ui.lang == Lang::Ru ? "случайное" : "random")
                                      : cfg.seed_text);
        out += ")\n";
        out += ui.lang == Lang::Ru ? "  \x1b[38;5;180mT\x1b[0m) язык: русский\n"
                                   : "  \x1b[38;5;180mT\x1b[0m) language: English\n";
        out += ui.lang == Lang::Ru ? "  \x1b[38;5;180mL\x1b[0m) загрузить сохранение\n"
                                   : "  \x1b[38;5;180mL\x1b[0m) load a save\n";
        out += ui.lang == Lang::Ru ? "  \x1b[38;5;180m?\x1b[0m) как играть      \x1b[38;5;180mq\x1b[0m) выход\n"
                                   : "  \x1b[38;5;180m?\x1b[0m) how to play     \x1b[38;5;180mq\x1b[0m) quit\n";
        std::fputs(out.c_str(), stdout);
        std::fflush(stdout);

        const int key = read_key_decoded();
        if (key == 'q' || key == kEsc) return false;
        if (key == 'T') { ui.lang = ui.lang == Lang::Ru ? Lang::En : Lang::Ru; continue; }
        if (key == '?') { ui.notice(help_text(ui.lang)); continue; }
        if (key == 'L') { cfg.seed_text = "\x01load"; return true; }
        if (key == 's') {
            ui.clear();
            std::fputs(ui.lang == Lang::Ru ? "Зерно (Enter — случайное): " : "Seed (Enter for random): ",
                       stdout);
            std::fflush(stdout);
            std::string typed;
            while (true) {
                const int c = read_key_decoded();
                if (c == '\r' || c == '\n') break;
                if (c == kEsc) { typed.clear(); break; }
                if ((c == 127 || c == 8) && !typed.empty()) {
                    typed.pop_back();
                    std::fputs("\b \b", stdout);
                    std::fflush(stdout);
                    continue;
                }
                if (c >= 32 && c < 127 && typed.size() < 32) {
                    typed += static_cast<char>(c);
                    std::fputc(c, stdout);
                    std::fflush(stdout);
                }
            }
            cfg.seed_text = typed;
            continue;
        }
        const int index = key - '1';
        if (index >= 0 && index < static_cast<int>(classes.size())) {
            cfg.hero_class = classes[static_cast<std::size_t>(index)].cls;
            return true;
        }
    }
}

/// Plays a whole run with a simple scripted policy.
///
/// CI runs this instead of a human: it drives generation, combat, items,
/// spells, stairs, the turn scheduler and the serialiser over thousands of
/// turns and many seeds, and reports a failure if the engine ever wedges or a
/// save fails to round-trip. The policy is deliberately crude — its job is
/// coverage, not skill.
int run_demo(std::uint64_t seed, int turns, bool verbose) {
    GameConfig cfg;
    cfg.seed = seed;
    cfg.hero_class = static_cast<HeroClass>(seed % 3);
    Game g;
    g.start(cfg);

    Rng policy(seed ^ 0xABCDEF01ULL);
    int stuck = 0;
    int floor_turns = 0;
    int last_depth = g.depth();
    Vec2 previous = g.hero().a.pos;
    bool moved_aside = false;

    for (int i = 0; i < turns && g.state() == RunState::Playing; ++i) {
        const Vec2 me = g.hero().a.pos;
        if (g.depth() != last_depth) { last_depth = g.depth(); floor_turns = 0; }
        ++floor_turns;

        // Diving straight down means meeting Вий at hero level 2, which is not
        // how the game is meant to be played. The bot clears a floor first and
        // only then takes the stairs, which is also what gives the deeper
        // content any test coverage at all.
        const bool ready_to_descend =
            g.hero().level > g.depth() || floor_turns > 500 || g.hero().nutrition < 250;

        // 1. Heal when badly hurt.
        if (g.hero().a.hp * 3 < g.hero().a.max_hp) {
            bool acted = false;
            for (std::size_t k = 0; k < g.hero().inv.items.size() && !acted; ++k) {
                const Item& it = g.hero().inv.items[k];
                if (it.kind != ItemKind::Potion) continue;
                if (it.subtype != static_cast<int>(PotionKind::Heal) &&
                    it.subtype != static_cast<int>(PotionKind::GreaterHeal)) continue;
                acted = g.perform(Action{ActionType::UseItem, {}, static_cast<int>(k), {}});
            }
            if (acted) continue;
        }

        // 2. Eat before starving.
        if (g.hero().nutrition < 120) {
            bool ate = false;
            for (std::size_t k = 0; k < g.hero().inv.items.size() && !ate; ++k)
                if (g.hero().inv.items[k].kind == ItemKind::Food)
                    ate = g.perform(Action{ActionType::UseItem, {}, static_cast<int>(k), {}});
            if (ate) continue;
        }

        // 3. Duck out of Вий's line of sight when his eyelids are about to
        //    rise. This is the fight's actual counter-play, and having the bot
        //    perform it is what proves the mechanic is beatable rather than
        //    merely punishing.
        for (const auto& m : g.monsters()) {
            const auto& sp = bestiary()[static_cast<std::size_t>(m.species)];
            if (std::strcmp(sp.key, "viy") != 0 || m.charge < 3) continue;
            if (!has_line_of_sight(g.map(), me, m.a.pos, sp.sight)) break;
            for (Vec2 d : directions8()) {
                const Vec2 step = me + d;
                if (!g.map().walkable(step) || g.monster_at(step)) continue;
                if (has_line_of_sight(g.map(), step, m.a.pos, sp.sight)) continue;
                if (g.perform(Action{ActionType::Move, d, -1, {}})) { moved_aside = true; break; }
            }
            break;
        }
        if (moved_aside) { moved_aside = false; continue; }

        // 4. Attack anything adjacent.
        bool fought = false;
        for (Vec2 d : directions8()) {
            if (!g.monster_at(me + d)) continue;
            fought = g.perform(Action{ActionType::Move, d, -1, {}});
            break;
        }
        if (fought) continue;

        // 5. Otherwise throw a spell at whatever is in range.
        const auto spells = g.castable_spells();
        if (!spells.empty() && policy.chance(40)) {
            const Spell s = spells[static_cast<std::size_t>(policy.below(static_cast<int>(spells.size())))];
            const auto targets = g.spell_targets(s);
            const Vec2 aim = targets.empty() ? Vec2{-1, -1} : targets.front();
            if (!spell_info(s).needs_target || !targets.empty())
                if (g.perform(Action{ActionType::CastSpell, {}, static_cast<int>(s), aim})) continue;
        }

        // 6. Collect loot underfoot, and take the stairs when standing on them.
        if (g.item_index_at(me) >= 0 && g.perform(Action{ActionType::PickUp, {}, -1, {}})) continue;
        if (ready_to_descend && g.map().at(me) == Tile::StairsDown &&
            g.perform(Action{ActionType::Descend, {}, -1, {}})) {
            stuck = 0;
            continue;
        }

        // 7. Head for the nearest monster, or for the stairs once the floor is
        //    cleared. The bot reads the true map rather than only what it has
        //    explored — it is a test harness, not a player.
        Vec2 goal = g.level().exit;
        if (!ready_to_descend) {
            int best = 1 << 30;
            for (const auto& m : g.monsters()) {
                const int d = dist_sq(me, m.a.pos);
                if (d < best) { best = d; goal = m.a.pos; }
            }
        }

        bool moved = false;
        if (stuck < 12) {
            const auto path = find_path(g.map(), me, goal, 3000);
            if (!path.empty())
                moved = g.perform(Action{ActionType::Move, path.front() - me, -1, {}});
        }
        if (!moved) {
            const Vec2 dir = directions8()[static_cast<std::size_t>(policy.below(8))];
            if (!g.perform(Action{ActionType::Move, dir, -1, {}}))
                g.perform(Action{ActionType::Wait, {}, -1, {}});
        }

        stuck = (g.hero().a.pos == previous) ? stuck + 1 : 0;
        previous = g.hero().a.pos;
    }

    // A save/load round trip at the end catches serialisation regressions.
    const std::string blob = g.save();
    Game restored;
    const bool reloaded = restored.load(blob);
    const bool consistent = reloaded && restored.depth() == g.depth() &&
                            restored.turn() == g.turn() &&
                            restored.hero().a.hp == g.hero().a.hp;

    if (verbose) {
        std::cout << "seed=" << seed << " turns=" << g.turn() << " depth=" << g.depth()
                  << " deepest=" << g.hero().deepest
                  << " hp=" << g.hero().a.hp << "/" << g.hero().a.max_hp
                  << " lvl=" << g.hero().level << " kills=" << g.hero().kills
                  << " score=" << g.score()
                  << " state=" << (g.state() == RunState::Playing ? "alive"
                                   : g.state() == RunState::Dead ? "dead" : "WON")
                  << " save=" << blob.size() << "B"
                  << " reload=" << (consistent ? "ok" : "FAILED") << "\n";
    }
    return consistent ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    // --- Headless modes ----------------------------------------------------
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::cout << "nav " << "1.0.0" << "\n";
            return 0;
        }
        if (std::strcmp(argv[i], "--demo") == 0) {
            const int runs = (i + 1 < argc) ? std::atoi(argv[i + 1]) : 5;
            int failures = 0;
            for (int r = 0; r < std::max(1, runs); ++r)
                failures += run_demo(0x5EED0000ULL + static_cast<std::uint64_t>(r), 8000, true);
            std::cout << (failures ? "DEMO FAILED\n" : "demo ok\n");
            return failures ? 1 : 0;
        }
    }

    RawMode raw;
    Ui ui;
    GameConfig cfg;

    while (true) {
        cfg.seed_text.clear();
        if (!title_screen(ui, cfg)) break;

        Game game;
        if (cfg.seed_text == "\x01load") {
            std::string blob;
            if (!read_file(save_path(), blob) || !game.load(blob)) {
                ui.notice(ui.lang == Lang::Ru ? "Сохранение не найдено или повреждено."
                                              : "No save found, or it is damaged.");
                continue;
            }
        } else {
            cfg.seed = cfg.seed_text.empty()
                           ? static_cast<std::uint64_t>(
                                 std::chrono::steady_clock::now().time_since_epoch().count())
                           : Rng::hash_seed(cfg.seed_text);
            game.start(cfg);
        }

        ui.clear();
        bool quit_to_title = false;
        while (!quit_to_title) {
            ui.draw(game);

            if (game.state() != RunState::Playing) {
                const bool won = game.state() == RunState::Ascended;
                std::string body =
                    won ? (ui.lang == Lang::Ru ? "\n\x1b[38;5;114mКощей повержен. Навь отпускает тебя.\x1b[0m"
                                               : "\n\x1b[38;5;114mKoschei is slain. Nav lets you go.\x1b[0m")
                        : (ui.lang == Lang::Ru ? "\n\x1b[38;5;203mТы остался в Нави навсегда.\x1b[0m"
                                               : "\n\x1b[38;5;203mYou remain in Nav forever.\x1b[0m");
                body += (ui.lang == Lang::Ru ? "\n\nОчков: " : "\n\nScore: ") +
                        std::to_string(game.score()) +
                        (ui.lang == Lang::Ru ? "\nГлубина: " : "\nDepth: ") +
                        std::to_string(game.hero().deepest) +
                        (ui.lang == Lang::Ru ? "\nУбито: " : "\nKills: ") +
                        std::to_string(game.hero().kills) +
                        (ui.lang == Lang::Ru ? "\nХодов: " : "\nTurns: ") +
                        std::to_string(game.turn());
                ui.notice(body);
                break;
            }

            const int key = read_key_decoded();
            Vec2 dir{};

            if (key_to_direction(key, dir)) {
                game.perform(Action{ActionType::Move, dir, -1, {}});
            } else switch (key) {
                case '.': case '5':
                    game.perform(Action{ActionType::Wait, {}, -1, {}});
                    break;
                case 'g': case ',':
                    game.perform(Action{ActionType::PickUp, {}, -1, {}});
                    break;
                case 'i':
                    show_inventory(game, ui);
                    ui.clear();
                    break;
                case 'd': {
                    std::vector<std::string> entries;
                    for (std::size_t k = 0; k < game.hero().inv.items.size(); ++k)
                        entries.push_back(inventory_line(game, k, ui.lang));
                    const int pick = ui.menu(ui.lang == Lang::Ru ? "Что бросить?" : "Drop what?",
                                             entries,
                                             ui.lang == Lang::Ru ? "Esc — назад." : "Esc to go back.");
                    if (pick >= 0) game.perform(Action{ActionType::DropItem, {}, pick, {}});
                    ui.clear();
                    break;
                }
                case 'z':
                    cast_menu(game, ui);
                    ui.clear();
                    break;
                case '>':
                    game.perform(Action{ActionType::Descend, {}, -1, {}});
                    break;
                case '<':
                    game.perform(Action{ActionType::Ascend, {}, -1, {}});
                    break;
                case 'p':
                    game.perform(Action{ActionType::Pray, {}, -1, {}});
                    break;
                case 'S':
                    ui.notice(write_file(save_path(), game.save())
                                  ? (ui.lang == Lang::Ru ? "Сохранено в ~/.nav_save"
                                                         : "Saved to ~/.nav_save")
                                  : (ui.lang == Lang::Ru ? "Не удалось сохранить."
                                                         : "Could not save."));
                    ui.clear();
                    break;
                case 'L': {
                    std::string blob;
                    if (read_file(save_path(), blob) && game.load(blob))
                        ui.notice(ui.lang == Lang::Ru ? "Загружено." : "Loaded.");
                    else
                        ui.notice(ui.lang == Lang::Ru ? "Сохранение не найдено или повреждено."
                                                      : "No save found, or it is damaged.");
                    ui.clear();
                    break;
                }
                case 'T':
                    ui.lang = ui.lang == Lang::Ru ? Lang::En : Lang::Ru;
                    break;
                case '?':
                    ui.notice(help_text(ui.lang));
                    ui.clear();
                    break;
                case 'q': case kEsc:
                    quit_to_title = true;
                    break;
                default:
                    break;
            }
        }
    }

    std::fputs("\x1b[0m\x1b[2J\x1b[H", stdout);
    return 0;
}
