// SPDX-License-Identifier: MIT
//
// The keyboard, as data.
//
// Two schemes share one table, and the help screen is generated from that same
// table. That is the whole point of the file: a scheme cannot gain a key that
// the help forgets to mention, and the two frontends cannot drift apart about
// what 'd' does — which they would, because they are written in different
// languages by people reading different files.
#include "nav/keys.hpp"

#include <cstring>

namespace nav {
namespace {

/// The eight directions, under both schemes.
///
/// Classic is vi: hjkl for the axes and yubn for the corners, laid out the way
/// they sit on the keyboard. Wasd puts the axes on wasd and the corners on the
/// four keys around them — q e above, z c below — which is the same shape one
/// row up and one row left.
struct MoveKey {
    char classic;
    char wasd;
    char digit;   ///< The numpad key, live in both schemes.
    Vec2 dir;
};

constexpr MoveKey kMoves[] = {
    {'h', 'a', '4', {-1,  0}},
    {'j', 's', '2', { 0,  1}},
    {'k', 'w', '8', { 0, -1}},
    {'l', 'd', '6', { 1,  0}},
    {'y', 'q', '7', {-1, -1}},
    {'u', 'e', '9', { 1, -1}},
    {'b', 'z', '1', {-1,  1}},
    {'n', 'c', '3', { 1,  1}},
};

/// A command and every key that reaches it, per scheme.
///
/// An empty string means the scheme does not bind that command to a letter at
/// all — which is not the same as the command being unavailable, because the
/// menu reaches everything. In the Wasd scheme the movement keys eat the
/// letters that Classic spends on saving and loading, and rather than move
/// those to some third letter nobody would guess, the scheme simply sends the
/// player to the menu. Someone who does not know that `S` saves does not know
/// that `V` saves either.
struct ActionKey {
    Command cmd;
    const char* classic;
    const char* wasd;
    Text shown_classic;   ///< How the help screen writes it.
    Text shown_wasd;
    Text what;
};

const ActionKey* action_table(std::size_t& count) {
    static const ActionKey table[] = {
        {Command::Wait,      ".5",  ".5x",  Text{". или 5", ". or 5"}, Text{". 5 x"},
         Text{"Пропустить ход", "Wait a turn"}},
        {Command::PickUp,    "g,",  "g,",   Text{"g или ,", "g or ,"}, Text{"g или ,", "g or ,"},
         Text{"Поднять", "Pick up"}},
        {Command::Inventory, "i",   "i",    "i",         "i",
         Text{"Котомка", "Pack"}},
        {Command::Drop,      "d",   "r",    "d",         "r",
         Text{"Бросить вещь", "Drop an item"}},
        {Command::Cast,      "z",   "f",    "z",         "f",
         Text{"Колдовать", "Cast a spell"}},
        {Command::Descend,   ">",   ">",    ">",         ">",
         Text{"Вниз по лестнице", "Down the stairs"}},
        {Command::Ascend,    "<",   "<",    "<",         "<",
         Text{"Вверх по лестнице", "Up the stairs"}},
        {Command::Pray,      "p",   "p",    "p",         "p",
         Text{"Жертва на капище", "Offer at a shrine"}},
        {Command::Map,       "m",   "m",    "m",         "m",
         Text{"Карта этажа", "The whole floor"}},
        {Command::Explore,   "o",   "o",    "o",         "o",
         Text{"Дойти до неизведанного", "Walk to what is unseen"}},
        {Command::Save,      "S",   "",     Text{"Shift+S"}, Text{"через меню", "from the menu"},
         Text{"Сохранить", "Save"}},
        {Command::Load,      "R",   "",     Text{"Shift+R"}, Text{"через меню", "from the menu"},
         Text{"Загрузить", "Load"}},
        {Command::Lang,      "T",   "T",    "Shift+T",   "Shift+T",
         Text{"Русский / English", "Русский / English"}},
        {Command::Help,      "?",   "?",    "?",         "?",
         Text{"Эта справка", "This help"}},
        {Command::Menu,      "\x1b", "\x1b", "Esc",      "Esc",
         Text{"Меню", "Menu"}},
        {Command::Quit,      "q",   "",     Text{"q"},   Text{"через меню", "from the menu"},
         Text{"Бросить партию", "Abandon the run"}},
    };
    count = sizeof(table) / sizeof(table[0]);
    return table;
}

bool contains(const char* keys, char key) {
    return keys != nullptr && *keys != '\0' && std::strchr(keys, key) != nullptr;
}

char to_upper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

}  // namespace

const char* key_scheme_key(KeyScheme s) {
    return s == KeyScheme::Wasd ? "wasd" : "classic";
}

Text key_scheme_name(KeyScheme s) {
    return s == KeyScheme::Wasd ? Text{"WASD", "WASD"}
                                : Text{"Классическая (hjkl)", "Classic (hjkl)"};
}

KeyPress command_for(KeyScheme scheme, char key) {
    const bool wasd = scheme == KeyScheme::Wasd;

    // Movement first, and running before walking: running is the same key held
    // with Shift, which arrives as the capital letter, and a capital that also
    // named an action would be ambiguous. The table is built so that never
    // happens, and a test holds it to that.
    for (const MoveKey& m : kMoves) {
        const char letter = wasd ? m.wasd : m.classic;
        if (key == letter || key == m.digit) return KeyPress{Command::Move, m.dir};
        if (key == to_upper(letter)) return KeyPress{Command::Run, m.dir};
    }

    std::size_t count = 0;
    const ActionKey* table = action_table(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (contains(wasd ? table[i].wasd : table[i].classic, key))
            return KeyPress{table[i].cmd, Vec2{0, 0}};
    }
    return KeyPress{};
}

std::vector<KeyHelpRow> key_help(KeyScheme scheme) {
    const bool wasd = scheme == KeyScheme::Wasd;
    std::vector<KeyHelpRow> rows;

    rows.push_back({Command::Move,
                    Text{wasd ? "w a s d  q e z c" : "h j k l  y u b n"},
                    Text{"Идти; можно зажать. Шаг в чудище — удар",
                         "Walk; hold to keep walking. A step into a foe attacks"}});
    rows.push_back({Command::Run,
                    Text{wasd ? "Shift + w a s d" : "Shift + h j k l"},
                    Text{"Зажать — спринт; нажать — бег до развилки",
                         "Hold to sprint; tap to run to the next junction"}});

    std::size_t count = 0;
    const ActionKey* table = action_table(count);
    for (std::size_t i = 0; i < count; ++i)
        rows.push_back({table[i].cmd, wasd ? table[i].shown_wasd : table[i].shown_classic,
                        table[i].what});
    return rows;
}

}  // namespace nav
