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
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "nav/fov.hpp"
#include "nav/game.hpp"
#include "nav/keys.hpp"
#include "nav/score.hpp"

#include "bot.hpp"

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

/// A byte read while peeking and not yet consumed. A raw terminal offers no way
/// to put a character back, so the one place that peeks keeps it here.
int pushed_back = -1;


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
    if (pushed_back >= 0) {
        const int c = pushed_back;
        pushed_back = -1;
        return c;
    }
    unsigned char c = 0;
    if (::read(STDIN_FILENO, &c, 1) != 1) return -1;
    return c;
}

#endif

/// Special keys, above the ASCII range so they never collide with a letter.
enum : int { kUp = 1000, kDown, kLeft, kRight, kEsc, kNone = -1 };

/// Whether another keypress is already waiting to be read.
///
/// Holding a direction down produces keys faster than the game draws, and the
/// terminal buffers them. Knowing the queue is not empty lets the loop skip a
/// redraw it is only going to throw away, which is what turns held movement
/// from a slideshow into walking.
bool input_pending() {
#if defined(_WIN32)
    return _kbhit() != 0;
#else
    termios saved{};
    if (tcgetattr(STDIN_FILENO, &saved) != 0) return false;
    termios peek = saved;
    peek.c_cc[VMIN] = 0;
    peek.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &peek) != 0) return false;
    unsigned char c = 0;
    const bool have = ::read(STDIN_FILENO, &c, 1) == 1;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    if (have) {
        // Put it back the only way a raw terminal allows: remember it.
        pushed_back = c;
        return true;
    }
    return false;
#endif
}

/// Throws away everything typed but not yet acted on.
///
/// This is the fix for the worst thing a held key can do in a roguelike: the
/// player sees a monster, lets go — and the hero keeps walking, because half a
/// second of keypresses is still sitting in the terminal's buffer waiting to be
/// played into its face.
void flush_input() {
#if defined(_WIN32)
    while (_kbhit()) (void)_getch();
#else
    pushed_back = -1;
    tcflush(STDIN_FILENO, TCIFLUSH);
#endif
}

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
    KeyScheme scheme{KeyScheme::Classic};
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

        // The hint line follows the scheme in force, because a hint that names
        // keys the player does not have is worse than no hint at all.
        const bool wasd = scheme == KeyScheme::Wasd;
        out += "\x1b[38;5;244m";
        out += lang == Lang::Ru
                   ? (wasd ? "wasd/стрелки — идти (можно зажать)  Shift — спринт  o — обойти этаж"
                           : "hjkl/стрелки — идти (можно зажать)  Shift — спринт  o — обойти этаж")
                   : (wasd ? "wasd/arrows walk (hold them)  Shift sprints  o explores"
                           : "hjkl/arrows walk (hold them)  Shift sprints  o explores");
        out += "\x1b[0m\x1b[K\n";
        out += "\x1b[38;5;244m";
        out += lang == Lang::Ru
                   ? (wasd ? "i — котомка  f — колдовать  m — карта  > < — лестницы  Esc — меню"
                           : "i — котомка  z — колдовать  m — карта  > < — лестницы  Esc — меню")
                   : (wasd ? "i pack  f cast  m map  > < stairs  Esc menu"
                           : "i pack  z cast  m map  > < stairs  Esc menu");
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
                                   "спешка", "вязко", "живая вода", "ярость", "оберег", "тень", "сон"};
        static const char* en[] = {"poison", "burn", "frozen", "confused", "blind",
                                   "haste", "slow", "regen", "might", "ward", "unseen", "asleep"};
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

    /// The whole floor at once, as far as it has been walked.
    ///
    /// The scrolling view shows twenty-two rows of a thirty-four-row map, which
    /// is fine while fighting and useless while deciding where to go next. This
    /// draws everything explored, marks the stairs, and marks where the hero is
    /// standing — the three facts the question "where now?" actually needs.
    void map_screen(const Game& g) const {
        clear();
        const Map& map = g.map();
        std::string out = "\x1b[1m";
        out += lang == Lang::Ru ? "Этаж " : "Floor ";
        out += std::to_string(g.depth());
        out += "\x1b[0m  \x1b[38;5;244m" + t(zone_theme_for_depth(g.depth()).name) + "\x1b[0m\n\n";

        for (int y = 0; y < map.height(); ++y) {
            std::string current;
            for (int x = 0; x < map.width(); ++x) {
                const Vec2 p{x, y};
                if (!map.explored(p)) { out += ' '; continue; }
                std::string color = "\x1b[38;5;240m";
                char glyph = '.';
                switch (map.at(p)) {
                    case Tile::Wall:       glyph = '#'; break;
                    case Tile::Door:
                    case Tile::OpenDoor:   glyph = '+'; color = "\x1b[38;5;137m"; break;
                    case Tile::Water:      glyph = '~'; color = "\x1b[38;5;66m";  break;
                    case Tile::Chasm:      glyph = ' '; break;
                    case Tile::Altar:      glyph = '_'; color = "\x1b[38;5;180m"; break;
                    case Tile::StairsDown: glyph = '>'; color = "\x1b[38;5;229m"; break;
                    case Tile::StairsUp:   glyph = '<'; color = "\x1b[38;5;229m"; break;
                    default:               glyph = '.'; break;
                }
                if (p == g.hero().a.pos) { glyph = '@'; color = "\x1b[38;5;231m"; }
                if (color != current) { out += color; current = color; }
                out += glyph;
            }
            out += "\x1b[0m\n";
        }

        const Vec2 down = g.level().exit;
        out += "\n\x1b[38;5;244m";
        if (map.explored(down))
            out += lang == Lang::Ru ? "> — лестница вниз, ты уже её нашёл."
                                    : "> is the way down; you have already found it.";
        else
            out += lang == Lang::Ru ? "Лестница вниз ещё не найдена."
                                    : "The way down has not been found yet.";
        out += "\x1b[0m";
        std::fputs(out.c_str(), stdout);
        std::fputs(lang == Lang::Ru ? "\n\n[любая клавиша]" : "\n\n[any key]", stdout);
        std::fflush(stdout);
        read_key_decoded();
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

/// "+2 удар, -1 защита" — what the piece would change, spelled out.
///
/// A signed number is the whole feature: it turns "Меч-кладенец" from a name
/// into a decision. Green when it is an improvement, red when it is not.
std::string preview_text(const EquipPreview& p, Lang lang, std::string* plain = nullptr) {
    if (!p.valid || p.changes_nothing()) return "";
    struct Row { int value; const char* ru; const char* en; };
    const Row rows[] = {
        {p.attack,  "удар",   "atk"},
        {p.defence, "защита", "def"},
        {p.max_hp,  "жизнь",  "hp"},
        {p.speed,   "прыть",  "spd"},
        {p.sight,   "взор",   "sight"},
    };
    std::string out;
    int good = 0;
    for (const Row& r : rows) {
        if (r.value == 0) continue;
        if (!out.empty()) out += ", ";
        out += (r.value > 0 ? "+" : "") + std::to_string(r.value) + " " +
               (lang == Lang::Ru ? r.ru : r.en);
        good += r.value > 0 ? 1 : -1;
    }
    if (out.empty()) return "";
    if (plain) *plain = out;
    return std::string(good >= 0 ? "\x1b[38;5;114m" : "\x1b[38;5;174m") + out + "\x1b[0m";
}

std::string inventory_line(Game& g, std::size_t index, Lang lang) {
    const Item& it = g.hero().inv.items[index];
    std::string line = item_name(it, g.identification()).get(lang);
    if (it.count > 1) line += " x" + std::to_string(it.count);
    if (g.hero().inv.is_equipped(static_cast<int>(index)))
        line += lang == Lang::Ru ? "  [надето]" : "  [worn]";

    // What it would do, before the note about what it is. Column widths are
    // measured on the uncoloured text: an ANSI escape takes no space on screen
    // but plenty in a std::string, and padding by the latter is how columns end
    // up ragged.
    const EquipPreview p = g.equip_preview(static_cast<int>(index));
    std::string delta_plain;
    const std::string delta = p.taking_off ? std::string() : preview_text(p, lang, &delta_plain);
    std::size_t width = display_width(line);
    if (!delta.empty()) {
        line = pad_to(line, 34) + delta;
        width = std::max<std::size_t>(width, 34) + display_width(delta_plain);
    }

    const std::string note = item_note(it, g.identification()).get(lang);
    if (!note.empty()) {
        const std::size_t column = delta.empty() ? 34 : 58;
        if (width < column) line += std::string(column - width, ' ');
        line += "\x1b[38;5;244m" + note + "\x1b[0m";
    }
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

std::string scores_path() {
    if (const char* home = std::getenv("HOME")) return std::string(home) + "/.nav_scores";
    return "nav_scores.txt";
}

std::string save_path() {
    if (const char* home = std::getenv("HOME")) return std::string(home) + "/.nav_save";
    return "nav_save.txt";
}

std::string config_path() {
    if (const char* home = std::getenv("HOME")) return std::string(home) + "/.nav_config";
    return "nav_config.txt";
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

/// Language and control scheme, remembered between runs.
///
/// Deliberately not part of the save file: a preference belongs to the person,
/// not to the run. Someone who likes WASD likes it in their next game too, and
/// loading someone else's save should not reach over and change their keyboard.
struct Preferences {
    Lang lang{Lang::Ru};
    KeyScheme scheme{KeyScheme::Classic};
};

Preferences read_preferences() {
    Preferences p;
    std::string blob;
    if (!read_file(config_path(), blob)) return p;
    std::istringstream in(blob);
    std::string magic;
    int version = 0;
    if (!(in >> magic >> version) || magic != "NAVCONF" || version != 1) return p;
    std::string lang, scheme;
    if (!(in >> lang >> scheme)) return p;
    p.lang = lang == "en" ? Lang::En : Lang::Ru;
    p.scheme = scheme == "wasd" ? KeyScheme::Wasd : KeyScheme::Classic;
    return p;
}

void write_preferences(const Preferences& p) {
    write_file(config_path(), std::string("NAVCONF 1 ") + (p.lang == Lang::En ? "en" : "ru") + " " +
                                  key_scheme_key(p.scheme) + "\n");
}

/// The last few blows and what was left unused.
///
/// Shown on the ending screen because a death that is not explained reads as
/// unfair, and because the pack usually contains the explanation.
std::string postmortem_text(const Game& g, Lang lang) {
    const Postmortem pm = g.postmortem();
    if (pm.blows.empty() && pm.unspent.empty()) return "";

    std::string out = "\n\x1b[38;5;244m";
    out += lang == Lang::Ru ? "Последние удары" : "The last blows";
    out += "\x1b[0m\n";
    for (const Postmortem::Blow& b : pm.blows) {
        out += "  \x1b[38;5;240m" + pad_to("ход " + std::to_string(b.turn), 10) + "\x1b[0m";
        out += pad_to(b.source.get(lang), 24);
        out += "\x1b[38;5;174m-" + std::to_string(b.amount) + "\x1b[0m";
        out += "\x1b[38;5;240m  \u2192 " + std::to_string(b.hp_left) + "\x1b[0m\n";
    }

    if (!pm.unspent.empty()) {
        out += "\n\x1b[38;5;244m";
        out += lang == Lang::Ru ? "Осталось неиспользованным" : "Left unused in the pack";
        out += "\x1b[0m\n";
        for (const Text& line : pm.unspent) out += "  " + line.get(lang) + "\n";
    }
    return out;
}

/// The help screen, generated from the engine's key table.
///
/// Written out rather than hand-maintained for a reason that cost this project
/// a bug report elsewhere: two copies of the same fact drift. If a scheme gains
/// a key, this screen gains a line, because it is reading the same table the
/// keypress went through.
std::string help_text(Lang lang, KeyScheme scheme) {
    std::string out = lang == Lang::Ru ? "\x1b[1mНАВЬ — как играть\x1b[0m\n\n"
                                       : "\x1b[1mNAV — how to play\x1b[0m\n\n";
    out += lang == Lang::Ru
               ? "  Ты спускаешься в шестнадцать кругов подземного мира.\n"
                 "  Смерть окончательна: сохранение — это пауза, а не запасная жизнь.\n\n"
               : "  You descend through sixteen circles of the underworld.\n"
                 "  Death is final: a save is a pause, not a spare life.\n\n";
    out += "  \x1b[38;5;244m" +
           std::string(lang == Lang::Ru ? "Схема: " : "Scheme: ") +
           key_scheme_name(scheme).get(lang) +
           std::string(lang == Lang::Ru ? "  (сменить — в меню по Esc)" : "  (change it in the Esc menu)") +
           "\x1b[0m\n\n";

    for (const KeyHelpRow& row : key_help(scheme))
        out += "  \x1b[38;5;180m" + pad_to(row.keys.get(lang), 18) + "\x1b[0m" +
               row.what.get(lang) + "\n";

    out += lang == Lang::Ru
               ? "\n  \x1b[38;5;244mЗнаки:\x1b[0m  @ ты   # стена   . пол   + дверь   ~ вода   > лестница\n"
                 "          ! зелье   ? свиток   ) оружие   [ доспех   \" оберег   $ золото   _ капище\n\n"
                 "  \x1b[38;5;244mЗелья и свитки не подписаны, пока их не испробуешь. Это часть игры.\x1b[0m"
               : "\n  \x1b[38;5;244mGlyphs:\x1b[0m  @ you   # wall   . floor   + door   ~ water   > stairs\n"
                 "          ! potion   ? scroll   ) weapon   [ armour   \" charm   $ gold   _ shrine\n\n"
                 "  \x1b[38;5;244mPotions and scrolls are unlabelled until you try them. That is the game.\x1b[0m";
    return out;
}

/// Save, load and drop, factored out so the key dispatch reads as a list of
/// commands rather than as a wall of screen handling.
void do_save(const Game& g, const Ui& ui) {
    ui.notice(write_file(save_path(), g.save())
                  ? (ui.lang == Lang::Ru ? "Сохранено в ~/.nav_save" : "Saved to ~/.nav_save")
                  : (ui.lang == Lang::Ru ? "Не удалось сохранить." : "Could not save."));
}

void do_load(Game& g, const Ui& ui) {
    std::string blob;
    if (read_file(save_path(), blob) && g.load(blob))
        ui.notice(ui.lang == Lang::Ru ? "Загружено." : "Loaded.");
    else
        ui.notice(ui.lang == Lang::Ru ? "Сохранение не найдено или повреждено."
                                      : "No save found, or it is damaged.");
}

void drop_menu(Game& g, const Ui& ui) {
    std::vector<std::string> entries;
    for (std::size_t k = 0; k < g.hero().inv.items.size(); ++k)
        entries.push_back(inventory_line(g, k, ui.lang));
    const int pick = ui.menu(ui.lang == Lang::Ru ? "Что бросить?" : "Drop what?", entries,
                             ui.lang == Lang::Ru ? "Esc — назад." : "Esc to go back.");
    if (pick >= 0) g.perform(Action{ActionType::DropItem, {}, pick, {}});
}

/// The Esc menu. Returns true when the player chose to abandon the run.
///
/// It exists because the WASD scheme cannot spare the letters for saving and
/// loading — but the better reason is that a player who has just opened this
/// game for the first time knows exactly one key that opens menus, and it is
/// this one. Everything reachable by a shortcut is reachable here as well.
bool game_menu(Game& g, Ui& ui) {
    while (true) {
        std::vector<std::string> entries = {
            ui.lang == Lang::Ru ? "Продолжить"        : "Back to the game",
            ui.lang == Lang::Ru ? "Сохранить"         : "Save",
            ui.lang == Lang::Ru ? "Загрузить"         : "Load",
            ui.lang == Lang::Ru ? "Карта этажа"       : "The whole floor",
            std::string(ui.lang == Lang::Ru ? "Управление: " : "Controls: ") +
                key_scheme_name(ui.scheme).get(ui.lang),
            ui.lang == Lang::Ru ? "Язык: русский / English" : "Language: Русский / English",
            ui.lang == Lang::Ru ? "Как играть"        : "How to play",
            ui.lang == Lang::Ru ? "Бросить партию"    : "Abandon the run",
        };
        const int pick = ui.menu(ui.lang == Lang::Ru ? "Меню" : "Menu", entries,
                                 ui.lang == Lang::Ru ? "Esc — назад в игру." : "Esc goes back.");
        switch (pick) {
            case -1: case 0: return false;
            case 1: do_save(g, ui); break;
            case 2: do_load(g, ui); return false;
            case 3: ui.map_screen(g); break;
            case 4:
                ui.scheme = ui.scheme == KeyScheme::Classic ? KeyScheme::Wasd : KeyScheme::Classic;
                write_preferences(Preferences{ui.lang, ui.scheme});
                ui.notice(help_text(ui.lang, ui.scheme));
                break;
            case 5:
                ui.lang = ui.lang == Lang::Ru ? Lang::En : Lang::Ru;
                write_preferences(Preferences{ui.lang, ui.scheme});
                break;
            case 6: ui.notice(help_text(ui.lang, ui.scheme)); break;
            case 7: return true;
            default: break;
        }
    }
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
        // Offered here as well as in the in-game menu: the scheme is the first
        // thing a player wants to change and the last thing they should have to
        // start a run to find.
        out += ui.lang == Lang::Ru ? "  \x1b[38;5;180mk\x1b[0m) управление: "
                                   : "  \x1b[38;5;180mk\x1b[0m) controls: ";
        out += key_scheme_name(ui.scheme).get(ui.lang) + std::string("\n");
        out += ui.lang == Lang::Ru ? "  \x1b[38;5;180m?\x1b[0m) как играть      \x1b[38;5;180mq\x1b[0m) выход\n"
                                   : "  \x1b[38;5;180m?\x1b[0m) how to play     \x1b[38;5;180mq\x1b[0m) quit\n";
        std::fputs(out.c_str(), stdout);
        std::fflush(stdout);

        const int key = read_key_decoded();
        if (key == 'q' || key == kEsc) return false;
        if (key == 'T') {
            ui.lang = ui.lang == Lang::Ru ? Lang::En : Lang::Ru;
            write_preferences(Preferences{ui.lang, ui.scheme});
            continue;
        }
        if (key == 'k') {
            ui.scheme = ui.scheme == KeyScheme::Classic ? KeyScheme::Wasd : KeyScheme::Classic;
            write_preferences(Preferences{ui.lang, ui.scheme});
            continue;
        }
        if (key == '?') { ui.notice(help_text(ui.lang, ui.scheme)); continue; }
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
/// The table, drawn as text. `highlight` marks the row just added, or -1.
std::string score_table_text(const std::vector<ScoreEntry>& table, Lang lang, int highlight) {
    if (table.empty())
        return lang == Lang::Ru ? "\nПока никто не спускался." : "\nNobody has gone down yet.";

    std::ostringstream out;
    out << (lang == Lang::Ru ? "\n\x1b[38;5;179mЛучшие спуски\x1b[0m\n"
                             : "\n\x1b[38;5;179mBest descents\x1b[0m\n");
    for (std::size_t i = 0; i < table.size(); ++i) {
        const ScoreEntry& e = table[i];
        const bool mine = static_cast<int>(i) == highlight;
        out << (mine ? "\x1b[38;5;179m" : "\x1b[38;5;245m");
        out << ' ' << (i + 1 < 10 ? " " : "") << (i + 1) << ". ";
        out << std::setw(6) << e.score << "  ";
        // Depth is the number a player actually compares runs by.
        out << (lang == Lang::Ru ? "гл." : "d.") << std::setw(3) << e.deepest << "  ";
        out << std::setw(10) << std::left << class_info(e.cls).name.get(lang) << std::right;
        if (e.won) out << (lang == Lang::Ru ? "  победа" : "  won");
        if (!e.seed_text.empty()) out << "  [" << e.seed_text << "]";
        out << "\x1b[0m\n";
    }
    return out.str();
}

/// Walks the whole dungeon with an over-levelled hero and checks it can be done.
///
/// A different question from the demo, and deliberately a different tool. The
/// demo asks "where does the curve kill people"; this asks "can the sixteenth
/// floor be reached at all, and does every guardian die when it is supposed
/// to". Before this existed, six of the eight guardians had never been fought
/// outside a test arena, and nothing had ever seen the fourth belt in a real
/// game — which is a thing to find out before a player does.
int run_sweep(std::uint64_t seed, int runs) {
    int failures = 0;
    for (int r = 0; r < std::max(1, runs); ++r) {
        const std::uint64_t s = seed + static_cast<std::uint64_t>(r);
        const HeroClass cls =
            static_cast<HeroClass>(s % static_cast<std::uint64_t>(HeroClass::Count));
        const BotResult res = play_one(s, cls, 60000, BotMode::Sweep);

        const bool bottom = res.deepest >= kMaxDepth;
        std::size_t expected = 0;
        for (int d = 1; d <= kMaxDepth; ++d)
            if (boss_for_depth(d)) ++expected;
        const bool all_slain = res.bosses_slain.size() == expected;

        std::cout << "seed=" << res.seed << " " << class_info(res.cls).name.en
                  << " deepest=" << res.deepest << "/" << kMaxDepth
                  << " guardians=" << res.bosses_slain.size() << "/" << expected
                  << " turns=" << res.turns
                  << " state=" << (res.state == RunState::Playing ? "alive"
                                   : res.state == RunState::Dead ? "dead" : "WON")
                  << " maxhp=" << res.max_hp
                  << " needle=" << (res.needle_broken ? "broken" : "whole")
                  << (res.killed_by.empty() ? "" : " by=\"" + res.killed_by + "\"")
                  << (bottom && all_slain ? "  ok" : "  INCOMPLETE") << "\n";

        if (!bottom || !all_slain) {
            ++failures;
            for (int d = 1; d <= kMaxDepth; ++d) {
                const char* key = boss_for_depth(d);
                if (!key) continue;
                const bool slain = std::find(res.bosses_slain.begin(), res.bosses_slain.end(),
                                             std::string(key)) != res.bosses_slain.end();
                if (!slain) std::cout << "    never put down: " << key << " (depth " << d << ")\n";
            }
        }
    }
    std::cout << (failures ? "SWEEP FAILED\n" : "sweep ok\n");
    return failures;
}

/// Plays `runs` whole games and prints one line each, plus a summary.
///
/// The summary is the point: a single run says nothing about balance, and a
/// hundred of them say where the curve actually breaks.
int run_demo(std::uint64_t seed, int runs, bool verbose) {
    std::vector<BotResult> all;
    all.reserve(static_cast<std::size_t>(std::max(1, runs)));
    int failures = 0;

    for (int r = 0; r < std::max(1, runs); ++r) {
        const std::uint64_t s = seed + static_cast<std::uint64_t>(r);
        const HeroClass cls = static_cast<HeroClass>(s % static_cast<std::uint64_t>(HeroClass::Count));
        const BotResult res = play_one(s, cls);
        all.push_back(res);
        if (!res.save_round_trips) ++failures;

        if (verbose) {
            std::cout << "seed=" << res.seed
                      << " " << class_info(res.cls).name.en
                      << " turns=" << res.turns
                      << " deepest=" << res.deepest
                      << " lvl=" << res.level
                      << " kills=" << res.kills
                      << " score=" << res.score
                      << " state=" << (res.state == RunState::Playing ? "alive"
                                       : res.state == RunState::Dead ? "dead" : "WON");
            if (!res.killed_by.empty()) std::cout << " by=\"" << res.killed_by << "\"";
            std::cout << " save=" << res.save_bytes << "B"
                      << " reload=" << (res.save_round_trips ? "ok" : "FAILED") << "\n";
        }
    }

    if (verbose && all.size() > 1) {
        std::vector<int> depth_counts(static_cast<std::size_t>(kMaxDepth) + 1, 0);
        int wins = 0;
        long long turns = 0, score = 0;
        for (const auto& r : all) {
            depth_counts[static_cast<std::size_t>(std::clamp(r.deepest, 0, kMaxDepth))]++;
            if (r.state == RunState::Ascended) ++wins;
            turns += r.turns;
            score += r.score;
        }
        const int n = static_cast<int>(all.size());
        std::cout << "\n-- " << n << " runs ------------------------------------------\n";
        for (std::size_t d = 1; d < depth_counts.size(); ++d) {
            if (depth_counts[d] == 0) continue;
            std::cout << "  depth " << (d < 10 ? " " : "") << d << "  ";
            for (int i = 0; i < depth_counts[d]; ++i) std::cout << '#';
            std::cout << "  " << depth_counts[d] << "\n";
        }
        int stalled = 0;
        for (const auto& r : all)
            if (r.state == RunState::Playing) ++stalled;
        if (stalled)
            std::cout << "  stalled " << stalled << "/" << n
                      << "  (neither won nor died — a bug in the bot or a floor it cannot leave)\n";
        std::cout << "  won " << wins << "/" << n
                  << "   avg turns " << (turns / n)
                  << "   avg score " << (score / n) << "\n";
    }

    std::cout << (failures ? "DEMO FAILED\n" : "demo ok\n");
    return failures;
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
            return run_demo(0x5EED0000ULL, runs, true) ? 1 : 0;
        }
        if (std::strcmp(argv[i], "--sweep") == 0) {
            const int runs = (i + 1 < argc) ? std::atoi(argv[i + 1]) : 3;
            return run_sweep(0x51EE0000ULL, runs) ? 1 : 0;
        }
    }

    RawMode raw;
    Ui ui;
    // Language and control scheme belong to the player, not to the run, so they
    // are read once here and written back the moment either changes.
    const Preferences prefs = read_preferences();
    ui.lang = prefs.lang;
    ui.scheme = prefs.scheme;
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
            // With a key held down the terminal delivers presses faster than a
            // frame can be drawn. Drawing a screen nobody will look at is the
            // difference between walking and a slideshow, so a redraw is
            // skipped whenever the next key is already waiting.
            if (!input_pending() || game.state() != RunState::Playing) ui.draw(game);

            if (game.state() != RunState::Playing) {
                const bool won = game.state() == RunState::Ascended;

                // The run goes into the table before the ending is drawn, so
                // the screen can say where it landed rather than only what it
                // scored. A table that cannot be read is replaced rather than
                // patched: losing ten old rows beats refusing to record new ones.
                std::vector<ScoreEntry> table;
                std::string blob;
                if (read_file(scores_path(), blob)) parse_scores(blob, table);
                const int place = insert_score(table, entry_from(game));
                write_file(scores_path(), serialize_scores(table));
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

                if (place == 0)
                    body += (ui.lang == Lang::Ru ? "\n\n\x1b[38;5;179mЛучший спуск.\x1b[0m"
                                                 : "\n\n\x1b[38;5;179mYour best descent yet.\x1b[0m");
                else if (place > 0)
                    body += (ui.lang == Lang::Ru ? "\n\nВ таблице: место " : "\n\nOn the board: place ") +
                            std::to_string(place + 1);

                body += "\n" + postmortem_text(game, ui.lang);
                body += "\n" + score_table_text(table, ui.lang, place);
                ui.notice(body);
                break;
            }

            // Remembered before the action so the loop can tell whether the
            // world changed under the player's hand. The judgement itself is
            // the engine's, and it is the same one a run uses to decide it has
            // gone far enough.
            const Game::Situation before = game.situation();

            const int pressed = read_key_decoded();

            // Arrow keys are the terminal's own thing and never reach the
            // engine's table; everything else does, and the table decides what
            // it means under the scheme in force.
            KeyPress k{};
            switch (pressed) {
                case kUp:    k = KeyPress{Command::Move, Vec2{0, -1}}; break;
                case kDown:  k = KeyPress{Command::Move, Vec2{0, 1}};  break;
                case kLeft:  k = KeyPress{Command::Move, Vec2{-1, 0}}; break;
                case kRight: k = KeyPress{Command::Move, Vec2{1, 0}};  break;
                case kEsc:   k = KeyPress{Command::Menu, Vec2{0, 0}};  break;
                default:
                    if (pressed > 0 && pressed < 128)
                        k = command_for(ui.scheme, static_cast<char>(pressed));
                    break;
            }

            switch (k.cmd) {
                case Command::Move:
                    game.perform(Action{ActionType::Move, k.dir, -1, {}});
                    break;
                case Command::Run:
                    game.perform(Action{ActionType::Run, k.dir, -1, {}});
                    break;
                case Command::Explore:
                    game.perform(Action{ActionType::Explore, {}, -1, {}});
                    break;
                case Command::Wait:
                    game.perform(Action{ActionType::Wait, {}, -1, {}});
                    break;
                case Command::PickUp:
                    game.perform(Action{ActionType::PickUp, {}, -1, {}});
                    break;
                case Command::Inventory:
                    show_inventory(game, ui);
                    ui.clear();
                    break;
                case Command::Drop:
                    drop_menu(game, ui);
                    ui.clear();
                    break;
                case Command::Cast:
                    cast_menu(game, ui);
                    ui.clear();
                    break;
                case Command::Descend:
                    game.perform(Action{ActionType::Descend, {}, -1, {}});
                    break;
                case Command::Ascend:
                    game.perform(Action{ActionType::Ascend, {}, -1, {}});
                    break;
                case Command::Pray:
                    game.perform(Action{ActionType::Pray, {}, -1, {}});
                    break;
                case Command::Map:
                    ui.map_screen(game);
                    ui.clear();
                    break;
                case Command::Save:
                    do_save(game, ui);
                    ui.clear();
                    break;
                case Command::Load:
                    do_load(game, ui);
                    ui.clear();
                    break;
                case Command::Lang:
                    ui.lang = ui.lang == Lang::Ru ? Lang::En : Lang::Ru;
                    write_preferences(Preferences{ui.lang, ui.scheme});
                    break;
                case Command::Help:
                    ui.notice(help_text(ui.lang, ui.scheme));
                    ui.clear();
                    break;
                case Command::Menu:
                    if (game_menu(game, ui)) quit_to_title = true;
                    ui.clear();
                    break;
                case Command::Quit:
                    quit_to_title = true;
                    break;
                case Command::None:
                case Command::Count:
                    break;
            }

            // The queue is thrown away the moment the situation changes. A
            // player who sees a monster and lets go must not then watch the
            // hero walk into it on half a second of keypresses that were typed
            // before there was anything to see.
            if (game.situation_changed(before)) flush_input();
        }
    }

    std::fputs("\x1b[0m\x1b[2J\x1b[H", stdout);
    return 0;
}
