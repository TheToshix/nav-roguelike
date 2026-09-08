// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <vector>

#include "nav/geometry.hpp"
#include "nav/text.hpp"

namespace nav {

/// Everything a key can ask the game to do.
///
/// This is not an Action: an Action is what the rules engine executes, while a
/// Command is what the player asked for. Some commands (the menu, the map, the
/// help screen) never reach the rules at all, and the frontends are the ones
/// that answer them.
enum class Command : std::uint8_t {
    None,
    Move,        ///< `dir` holds the step.
    Run,         ///< `dir` holds the step, repeated until something interrupts.
    Wait,
    PickUp,
    Inventory,
    Drop,
    Cast,
    Descend,
    Ascend,
    Pray,
    Map,         ///< The whole explored floor at once.
    Explore,     ///< Walk to the nearest thing not yet seen.
    Save,
    Load,
    Lang,
    Help,
    Menu,        ///< Everything above, for a player who knows no shortcuts.
    Quit,
    Count
};

/// Which set of keys is live.
///
/// The two schemes are not a preference between equals. `Classic` is what a
/// roguelike player expects and reaches for without thinking; `Wasd` is what
/// everyone else expects, and a game whose first floor is unplayable because
/// the arrow keys felt wrong has lost a player before the rules ever spoke.
/// Values are written into the config file, so they are appended, never
/// reordered.
enum class KeyScheme : std::uint8_t { Classic, Wasd, Count };

const char* key_scheme_key(KeyScheme s);
Text key_scheme_name(KeyScheme s);

/// One resolved keypress.
struct KeyPress {
    Command cmd{Command::None};
    Vec2 dir{0, 0};   ///< Meaningful for Move and Run.
};

/// Resolves one character in one scheme.
///
/// Lives in the engine, next to the rules and the score table, for the reason
/// the score table does: both frontends must agree about what a key means, and
/// the only way to guarantee that is to give them one table to read rather than
/// two to maintain. The engine still performs no input: this maps a character
/// that someone else read to a command that someone else executes.
KeyPress command_for(KeyScheme scheme, char key);

/// One line of the help screen.
struct KeyHelpRow {
    Command cmd;
    Text keys;   ///< As shown to the player: "w a s d", "Esc", "через меню".
    Text what;
};

/// The help screen for a scheme, generated from the same table the keys are
/// resolved from — so a rebinding cannot silently leave the help lying.
std::vector<KeyHelpRow> key_help(KeyScheme scheme);

}  // namespace nav
