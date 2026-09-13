// SPDX-License-Identifier: MIT
//
// The seed is a promise: type the same word on Linux, on Windows, in the
// browser, and you get the same dungeon. Everything else in this suite tests
// that promise by generating a floor twice inside one process and comparing —
// which proves the generator has no hidden state, and proves nothing at all
// about portability. Two runs in the same process share a standard library.
//
// This file is the other half. It carries fingerprints of the dungeon computed
// once and written down, so the four-platform matrix in CI compares GCC against
// Clang against MSVC against Emscripten instead of each against itself. It is
// the test that would have caught the door-candidate sort in `build_arena`,
// which ordered equivalent elements differently in libstdc++ and libc++ and so
// grew a different guardian's hall from the same seed.
//
// -- When this test fails --
//
// If you changed generation or content on purpose, the numbers are simply out
// of date: run
//
//     ./build/tests/nav_tests --gtest_filter='Portability.*' --gtest_also_run_disabled_tests
//
// which prints the current fingerprints (see the DISABLED_ test at the bottom),
// and paste them in. If you changed *nothing* and it fails on one platform
// only, do not touch the numbers — that is the bug this file exists to find.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "nav/game.hpp"
#include "support.hpp"

namespace nav {
namespace {

/// FNV-1a, 64-bit.
///
/// Written out rather than taken from the standard library on purpose:
/// `std::hash` is explicitly allowed to differ between implementations, so a
/// fingerprint built on it would disagree across platforms for reasons that
/// have nothing to do with the dungeon — the test would cry wolf on exactly the
/// axis it is meant to watch.
class Fingerprint {
public:
    void operator()(std::int64_t v) {
        const std::uint64_t bits = static_cast<std::uint64_t>(v);
        for (int byte = 0; byte < 8; ++byte) {
            h_ ^= (bits >> (byte * 8)) & 0xFF;
            h_ *= 1099511628211ull;
        }
    }
    std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_{14695981039346656037ull};
};

/// The shape of a floor: what was carved, and where the ways in and out are.
///
/// Kept apart from the contents below so that the two fail for different
/// reasons. Rebalancing the bestiary moves every monster on every floor and
/// must not be allowed to also invalidate the terrain fingerprint — a canary
/// that goes off every time anyone edits data.cpp is a canary nobody reads.
std::uint64_t terrain_fingerprint(const Game& g) {
    Fingerprint f;
    for (int depth = kLobbyDepth; depth <= kMaxDepth; ++depth) {
        const Level& lvl = g.level_at(depth);
        f(lvl.generated ? 1 : 0);
        if (!lvl.generated) continue;
        f(lvl.map.width());
        f(lvl.map.height());
        for (Tile t : lvl.map.raw_tiles()) f(static_cast<int>(t));
        f(lvl.entrance.x);
        f(lvl.entrance.y);
        f(lvl.exit.x);
        f(lvl.exit.y);
        const Arena& a = lvl.arena;
        f(a.exists ? 1 : 0);
        if (a.exists) {
            f(a.min.x); f(a.min.y);
            f(a.max.x); f(a.max.y);
            f(a.door.x); f(a.door.y);
            f(a.seals ? 1 : 0);
        }
    }
    return f.value();
}

/// What was put on the floors: every creature and every item, in the order the
/// generator produced them. Ordering is part of the fingerprint deliberately —
/// the turn queue walks this vector, so two runs that hold the same monsters in
/// a different order are already two different games.
std::uint64_t contents_fingerprint(const Game& g) {
    Fingerprint f;
    for (int depth = kLobbyDepth; depth <= kMaxDepth; ++depth) {
        const Level& lvl = g.level_at(depth);
        if (!lvl.generated) continue;
        f(static_cast<int>(lvl.event));
        for (const Monster& m : lvl.monsters) {
            f(m.species);
            f(m.a.pos.x); f(m.a.pos.y);
            f(m.a.hp); f(m.a.max_hp);
            f(m.a.attack); f(m.a.defence); f(m.a.speed);
            f(m.phase);
        }
        for (const Item& it : lvl.items) {
            f(static_cast<int>(it.kind));
            f(it.subtype); f(it.power); f(it.enchant); f(it.count);
            f(it.cursed ? 1 : 0);
            f(it.pos.x); f(it.pos.y);
        }
    }
    return f.value();
}

/// A run walked to the bottom, so every floor of every belt is generated.
Game whole_dungeon(std::uint64_t seed) { return descend_to(kMaxDepth, seed); }

struct Golden {
    std::uint64_t seed;
    std::uint64_t terrain;
    std::uint64_t contents;
};

/// Three seeds, because one would not exercise both generators: a run is four
/// belts of four floors, so any seed covers rooms, caves, every guardian's hall
/// and every belt event table. Three of them make an accidental agreement
/// vanishingly unlikely.
constexpr Golden kGolden[] = {
    {1u,          0x754eb9e6546b68cbull, 0xa1657dc5a4e8bd9full},
    {20260909u,   0x611fc3626f2a848eull, 0x98d6ca909ea95450ull},
    {0xDEADBEEFu, 0x38dc4acae7d3d3acull, 0x59ae3edbafd56496ull},
};

// ---------------------------------------------------------------------------

TEST(Portability, TheSameSeedCarvesTheSameDungeonOnEveryPlatform) {
    for (const Golden& g : kGolden) {
        const Game run = whole_dungeon(g.seed);
        EXPECT_EQ(terrain_fingerprint(run), g.terrain)
            << "seed " << g.seed << ": the floors themselves came out different.\n"
            << "If generation was changed on purpose, refresh the table in "
               "tests/test_portability.cpp.\n"
            << "If it was not, and this passes elsewhere, the generator has "
               "picked up something that is not the same on every standard "
               "library — an unstable sort, an unordered container, a float.";
    }
}

TEST(Portability, TheSameSeedFillsTheDungeonTheSameWayOnEveryPlatform) {
    for (const Golden& g : kGolden) {
        const Game run = whole_dungeon(g.seed);
        EXPECT_EQ(contents_fingerprint(run), g.contents)
            << "seed " << g.seed << ": the floors match but what stands on them "
               "does not. Placement draws from the same generator, so this is "
               "either a deliberate content change or a divergence downstream "
               "of the map.";
    }
}

/// Belt and braces: the fingerprints must also be a property of the seed and
/// not of the machine's mood. If this fails, the two above are meaningless.
TEST(Portability, FingerprintsDependOnNothingButTheSeed) {
    for (const Golden& g : kGolden) {
        EXPECT_EQ(terrain_fingerprint(whole_dungeon(g.seed)),
                  terrain_fingerprint(whole_dungeon(g.seed)));
        EXPECT_EQ(contents_fingerprint(whole_dungeon(g.seed)),
                  contents_fingerprint(whole_dungeon(g.seed)));
    }
    EXPECT_NE(terrain_fingerprint(whole_dungeon(kGolden[0].seed)),
              terrain_fingerprint(whole_dungeon(kGolden[1].seed)));
}

/// Not a test: the way to refresh the table above after a deliberate change.
/// Run with --gtest_also_run_disabled_tests and copy what it prints.
TEST(Portability, DISABLED_PrintTheFingerprints) {
    std::printf("\nconstexpr Golden kGolden[] = {\n");
    for (const Golden& g : kGolden) {
        const Game run = whole_dungeon(g.seed);
        std::printf("    {%lluu, 0x%016llxull, 0x%016llxull},\n",
                    static_cast<unsigned long long>(g.seed),
                    static_cast<unsigned long long>(terrain_fingerprint(run)),
                    static_cast<unsigned long long>(contents_fingerprint(run)));
    }
    std::printf("};\n\n");
}

}  // namespace
}  // namespace nav
