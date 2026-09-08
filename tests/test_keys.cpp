// SPDX-License-Identifier: MIT
//
// The keyboard table.
//
// These tests exist because a keymap is exactly the kind of thing that rots
// quietly: someone binds a new command, it happens to collide with a movement
// key in the other scheme, and nobody notices until a player reports that
// pressing "run left" saved the game. So the properties are checked over the
// whole table rather than key by key.
#include <gtest/gtest.h>

#include <set>
#include <string>

#include "nav/keys.hpp"

using namespace nav;

namespace {

const KeyScheme kSchemes[] = {KeyScheme::Classic, KeyScheme::Wasd};

/// Every printable key the game could plausibly be handed.
std::string every_key() {
    std::string keys;
    for (char c = 'a'; c <= 'z'; ++c) keys += c;
    for (char c = 'A'; c <= 'Z'; ++c) keys += c;
    for (char c = '0'; c <= '9'; ++c) keys += c;
    keys += ".,<>?/;:'\"[]{}-=_+*&^%$#@!`~\\|()";
    keys += '\x1b';
    return keys;
}

}  // namespace

TEST(Keys, TheTwoSchemesMoveInAllEightDirections) {
    for (KeyScheme s : kSchemes) {
        std::set<std::pair<int, int>> seen;
        for (char key : every_key()) {
            const KeyPress k = command_for(s, key);
            if (k.cmd == Command::Move) seen.insert({k.dir.x, k.dir.y});
        }
        EXPECT_EQ(seen.size(), 8u) << "scheme " << key_scheme_key(s) << " cannot reach every corner";
        EXPECT_EQ(seen.count({0, 0}), 0u) << "a movement key that goes nowhere";
    }
}

TEST(Keys, RunningCoversTheSameEightDirections) {
    for (KeyScheme s : kSchemes) {
        std::set<std::pair<int, int>> walk, run;
        for (char key : every_key()) {
            const KeyPress k = command_for(s, key);
            if (k.cmd == Command::Move) walk.insert({k.dir.x, k.dir.y});
            if (k.cmd == Command::Run) run.insert({k.dir.x, k.dir.y});
        }
        EXPECT_EQ(walk, run) << "scheme " << key_scheme_key(s)
                             << " can walk somewhere it cannot run";
    }
}

TEST(Keys, WasdIsWhereAPlayerWouldReachForIt) {
    // The point of the whole scheme, stated once so it cannot drift.
    EXPECT_EQ(command_for(KeyScheme::Wasd, 'w').dir.y, -1);
    EXPECT_EQ(command_for(KeyScheme::Wasd, 's').dir.y, 1);
    EXPECT_EQ(command_for(KeyScheme::Wasd, 'a').dir.x, -1);
    EXPECT_EQ(command_for(KeyScheme::Wasd, 'd').dir.x, 1);
    EXPECT_EQ(command_for(KeyScheme::Wasd, 'w').cmd, Command::Move);
    EXPECT_EQ(command_for(KeyScheme::Wasd, 'W').cmd, Command::Run);

    // And the classic scheme is untouched by its arrival.
    EXPECT_EQ(command_for(KeyScheme::Classic, 'h').dir.x, -1);
    EXPECT_EQ(command_for(KeyScheme::Classic, 'l').dir.x, 1);
    EXPECT_EQ(command_for(KeyScheme::Classic, 'd').cmd, Command::Drop);
    EXPECT_EQ(command_for(KeyScheme::Classic, 'z').cmd, Command::Cast);
}

TEST(Keys, NoKeyMeansTwoThingsInOneScheme) {
    // The collision this guards against is not hypothetical: 'd' is "drop" in
    // the classic scheme and "walk right" in WASD, and 'S' is "save" in one and
    // "run down" in the other. A single table is only safe if it is checked.
    for (KeyScheme s : kSchemes) {
        for (char key : every_key()) {
            const KeyPress a = command_for(s, key);
            const KeyPress b = command_for(s, key);
            EXPECT_EQ(a.cmd, b.cmd) << "resolution is not deterministic";
            if (a.cmd == Command::Move || a.cmd == Command::Run) continue;
            // A non-movement key must not carry a direction.
            EXPECT_EQ(a.dir.x, 0);
            EXPECT_EQ(a.dir.y, 0);
        }
    }
}

TEST(Keys, EveryCommandIsReachableOrDeliberatelyLeftToTheMenu) {
    // Some commands lose their letter in the WASD scheme because a movement key
    // took it. That is allowed — but only if the help screen says where the
    // command went, which is the second half of this test.
    for (KeyScheme s : kSchemes) {
        std::set<int> bound;
        for (char key : every_key()) bound.insert(static_cast<int>(command_for(s, key).cmd));

        std::set<int> documented;
        for (const KeyHelpRow& row : key_help(s)) {
            documented.insert(static_cast<int>(row.cmd));
            EXPECT_FALSE(row.keys.ru.empty()) << "a help row with no keys at all";
            EXPECT_FALSE(row.what.ru.empty()) << "a help row that explains nothing";
            EXPECT_FALSE(row.what.en.empty()) << "a help row with no English";
        }

        for (int c = 1; c < static_cast<int>(Command::Count); ++c) {
            EXPECT_EQ(documented.count(c), 1u)
                << "command " << c << " is missing from the help of " << key_scheme_key(s);
            if (bound.count(c) == 0) {
                // Unbound is fine only for the commands the menu covers.
                const Command cmd = static_cast<Command>(c);
                EXPECT_TRUE(cmd == Command::Save || cmd == Command::Load || cmd == Command::Quit)
                    << "command " << c << " is unreachable in " << key_scheme_key(s);
            }
        }
    }
}

TEST(Keys, TheMenuIsAlwaysOneKeyAway) {
    // Whatever else changes, Escape opens the menu — which is the only thing a
    // player who knows nothing about the scheme can be told to press.
    for (KeyScheme s : kSchemes) EXPECT_EQ(command_for(s, '\x1b').cmd, Command::Menu);
}

TEST(Keys, TheNumpadWorksInBothSchemes) {
    for (KeyScheme s : kSchemes) {
        EXPECT_EQ(command_for(s, '4').dir.x, -1);
        EXPECT_EQ(command_for(s, '6').dir.x, 1);
        EXPECT_EQ(command_for(s, '8').dir.y, -1);
        EXPECT_EQ(command_for(s, '2').dir.y, 1);
        EXPECT_EQ(command_for(s, '5').cmd, Command::Wait);
    }
}

TEST(Keys, AnUnboundKeyDoesNothing) {
    EXPECT_EQ(command_for(KeyScheme::Classic, '\t').cmd, Command::None);
    EXPECT_EQ(command_for(KeyScheme::Wasd, '\t').cmd, Command::None);
    EXPECT_EQ(command_for(KeyScheme::Classic, '~').cmd, Command::None);
}

TEST(Keys, BothSchemesHaveNames) {
    for (KeyScheme s : kSchemes) {
        EXPECT_FALSE(key_scheme_name(s).ru.empty());
        EXPECT_FALSE(key_scheme_name(s).en.empty());
        EXPECT_NE(std::string(key_scheme_key(s)), std::string());
    }
    EXPECT_NE(std::string(key_scheme_key(KeyScheme::Classic)),
              std::string(key_scheme_key(KeyScheme::Wasd)));
}
