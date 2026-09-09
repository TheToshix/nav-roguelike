// SPDX-License-Identifier: MIT
//
// The art and the engine are written in different languages and by different
// hands, so nothing but a test keeps them in step. Every tile, every species
// and every item kind the engine can put on screen must have a sprite drawn
// for it; a creature added to the bestiary without art would otherwise ship as
// an invisible monster, which is the worst kind of bug — the player sees
// nothing at all and has no way to report it.
//
// The check reads frontend/web/sprites.js, the generated artwork itself,
// rather than a second list kept next to the engine. A list would agree with
// the engine and disagree with the pictures.
#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "nav/data.hpp"
#include "nav/game.hpp"
#include "nav/types.hpp"

using namespace nav;

namespace {

/// Every top-level sprite name in the generated file.
///
/// The file is JSON inside one assignment. The only objects in it are the
/// sprite entries and the one that holds them, so every quoted key immediately
/// followed by `:{` is a sprite name — bar the container itself. Parsing that
/// much and no more keeps the test from needing a JSON library.
const std::set<std::string>& drawn_sprites() {
    static const std::set<std::string> names = [] {
        std::set<std::string> out;
        std::ifstream in(NAV_SPRITES_JS);
        EXPECT_TRUE(in.good()) << "cannot open " << NAV_SPRITES_JS
                               << " — run tools/sprites/build.py";
        std::stringstream buf;
        buf << in.rdbuf();
        const std::string text = buf.str();

        const std::string marker = "\":{";
        std::size_t at = 0;
        while ((at = text.find(marker, at)) != std::string::npos) {
            const std::size_t close = at;                       // the key's closing quote
            const std::size_t open = text.rfind('"', close - 1);
            at += marker.size();
            if (open == std::string::npos) continue;
            std::string name = text.substr(open + 1, close - open - 1);
            if (name == "sprites") continue;                    // the container, not a sprite
            out.insert(std::move(name));
        }
        return out;
    }();
    return names;
}

void expect_drawn(const std::string& key, const std::string& what) {
    EXPECT_EQ(drawn_sprites().count(key), 1u)
        << what << " has no sprite: add '" << key << "' to tools/sprites/pixels.py"
        << " and re-run tools/sprites/build.py";
}

}  // namespace

TEST(Sprites, TheGeneratedFileWasFoundAndParsed) {
    // A silent failure to read the file would make every other test in here
    // pass vacuously, so assert the file has real content of its own first.
    ASSERT_GE(drawn_sprites().size(), 20u)
        << "frontend/web/sprites.js looks empty or unparsable";
    EXPECT_EQ(drawn_sprites().count("floor"), 1u);
}

TEST(Sprites, EveryTileHasOne) {
    const Tile tiles[] = {Tile::Wall,  Tile::Floor, Tile::StairsDown, Tile::StairsUp, Tile::Door,
                          Tile::OpenDoor, Tile::Water, Tile::Chasm,   Tile::Altar};
    for (Tile t : tiles) expect_drawn(tile_sprite_key(t), "a tile");
}

TEST(Sprites, EveryItemKindHasOne) {
    for (int k = 0; k <= static_cast<int>(ItemKind::Feather); ++k)
        expect_drawn(item_sprite_key(static_cast<ItemKind>(k)), "an item kind");
}

TEST(Sprites, EveryHeroClassHasOne) {
    for (int c = 0; c < static_cast<int>(HeroClass::Count); ++c)
        expect_drawn(hero_sprite_key(static_cast<HeroClass>(c)), "a hero class");
}

TEST(Sprites, EverySpeciesHasOne) {
    for (const Species& s : bestiary()) expect_drawn(s.key, std::string("species ") + s.key);
}

TEST(Sprites, TheFireOverlayHasOne) {
    // Fire is not a tile — it is a timed overlay the engine draws over the floor
    // — so it is not covered by EveryTileHasOne, but the renderer still asks for
    // it by name and it still has to exist.
    expect_drawn(ember_sprite_key(), "the burning-floor overlay");
}

TEST(Sprites, NoSpriteIsDrawnThatNothingCanShow) {
    // The other direction: art nobody asks for is dead weight in the payload,
    // and usually means a key was renamed on one side only.
    std::set<std::string> reachable;
    const Tile tiles[] = {Tile::Wall,  Tile::Floor, Tile::StairsDown, Tile::StairsUp, Tile::Door,
                          Tile::OpenDoor, Tile::Water, Tile::Chasm,   Tile::Altar};
    for (Tile t : tiles) reachable.insert(tile_sprite_key(t));
    for (int k = 0; k <= static_cast<int>(ItemKind::Feather); ++k)
        reachable.insert(item_sprite_key(static_cast<ItemKind>(k)));
    for (int c = 0; c < static_cast<int>(HeroClass::Count); ++c)
        reachable.insert(hero_sprite_key(static_cast<HeroClass>(c)));
    for (const Species& s : bestiary()) reachable.insert(s.key);
    reachable.insert(ember_sprite_key());   // the burning-floor overlay

    for (const std::string& drawn : drawn_sprites())
        EXPECT_EQ(reachable.count(drawn), 1u)
            << "'" << drawn << "' is drawn but nothing in the engine ever asks for it";
}
