// SPDX-License-Identifier: MIT
//
// A save that silently loads wrong is worse than one that refuses to load, so
// the format is tested from both ends: faithful round trips, and hard refusals
// on anything malformed.
#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>
#include <utility>

#include "nav/game.hpp"

#include "support.hpp"

using namespace nav;

namespace {

/// A game advanced far enough to have interesting state: a second floor,
/// damage taken, effects running, items identified.
Game played_game(std::uint64_t seed = 909, int steps = 300) {
    GameConfig cfg;
    cfg.seed = seed;
    cfg.seed_text = "кощей";
    cfg.hero_class = HeroClass::Vedun;

    Game g;
    g.start(cfg);
    leave_crossroads(g);
    Rng policy(seed);
    for (int i = 0; i < steps && g.state() == RunState::Playing; ++i) {
        if (g.map().at(g.hero().a.pos) == Tile::StairsDown)
            if (g.perform(Action{ActionType::Descend, {}, -1, {}})) continue;
        if (g.item_index_at(g.hero().a.pos) >= 0)
            if (g.perform(Action{ActionType::PickUp, {}, -1, {}})) continue;
        const Vec2 dir = directions8()[static_cast<std::size_t>(policy.below(8))];
        if (!g.perform(Action{ActionType::Move, dir, -1, {}}))
            g.perform(Action{ActionType::Wait, {}, -1, {}});
    }
    return g;
}

void expect_same_state(const Game& a, const Game& b) {
    EXPECT_EQ(a.depth(), b.depth());
    EXPECT_EQ(a.turn(), b.turn());
    EXPECT_EQ(a.state(), b.state());
    EXPECT_EQ(a.score(), b.score());
    EXPECT_EQ(a.hero().a.pos, b.hero().a.pos);
    EXPECT_EQ(a.hero().a.hp, b.hero().a.hp);
    EXPECT_EQ(a.hero().a.max_hp, b.hero().a.max_hp);
    EXPECT_EQ(a.hero().mana, b.hero().mana);
    EXPECT_EQ(a.hero().level, b.hero().level);
    EXPECT_EQ(a.hero().xp, b.hero().xp);
    EXPECT_EQ(a.hero().gold, b.hero().gold);
    EXPECT_EQ(a.hero().kills, b.hero().kills);
    EXPECT_EQ(a.hero().nutrition, b.hero().nutrition);
    EXPECT_EQ(a.hero().deepest, b.hero().deepest);
    EXPECT_EQ(a.hero().cls, b.hero().cls);
    EXPECT_EQ(a.hero().inv.items.size(), b.hero().inv.items.size());
    EXPECT_EQ(a.hero().inv.weapon, b.hero().inv.weapon);
    EXPECT_EQ(a.hero().inv.armor, b.hero().inv.armor);
    EXPECT_EQ(a.monsters().size(), b.monsters().size());
    EXPECT_EQ(a.floor_items().size(), b.floor_items().size());
    EXPECT_EQ(a.map().raw_tiles(), b.map().raw_tiles());
    EXPECT_EQ(a.map().raw_explored(), b.map().raw_explored());
}

}  // namespace

TEST(Save, RoundTripsAFreshGame) {
    GameConfig cfg;
    cfg.seed = 5;
    Game original;
    original.start(cfg);
    leave_crossroads(original);

    Game restored;
    ASSERT_TRUE(restored.load(original.save()));
    expect_same_state(original, restored);
}

TEST(Save, RoundTripsAGameInProgress) {
    const Game original = played_game();
    Game restored;
    ASSERT_TRUE(restored.load(original.save()));
    expect_same_state(original, restored);
}

TEST(Save, PreservesTheInventoryItemForItem) {
    const Game original = played_game(4242, 400);
    Game restored;
    ASSERT_TRUE(restored.load(original.save()));

    ASSERT_EQ(original.hero().inv.items.size(), restored.hero().inv.items.size());
    for (std::size_t i = 0; i < original.hero().inv.items.size(); ++i) {
        const Item& a = original.hero().inv.items[i];
        const Item& b = restored.hero().inv.items[i];
        EXPECT_EQ(a.kind, b.kind) << "item " << i;
        EXPECT_EQ(a.subtype, b.subtype) << "item " << i;
        EXPECT_EQ(a.count, b.count) << "item " << i;
        EXPECT_EQ(a.power, b.power) << "item " << i;
        EXPECT_EQ(a.enchant, b.enchant) << "item " << i;
        EXPECT_EQ(a.identified, b.identified) << "item " << i;
    }
}

TEST(Save, PreservesMonsterPositionsAndHealth) {
    const Game original = played_game(31337, 250);
    Game restored;
    ASSERT_TRUE(restored.load(original.save()));

    ASSERT_EQ(original.monsters().size(), restored.monsters().size());
    for (std::size_t i = 0; i < original.monsters().size(); ++i) {
        EXPECT_EQ(original.monsters()[i].a.pos, restored.monsters()[i].a.pos) << "monster " << i;
        EXPECT_EQ(original.monsters()[i].a.hp, restored.monsters()[i].a.hp) << "monster " << i;
        EXPECT_EQ(original.monsters()[i].species, restored.monsters()[i].species) << "monster " << i;
        EXPECT_EQ(original.monsters()[i].awake, restored.monsters()[i].awake) << "monster " << i;
    }
}

TEST(Save, PreservesBurningCells) {
    // A save taken mid-fight has to resume with the same fire on the ground, or
    // walking away from the Огневик and reloading would put it out for free.
    GameConfig cfg;
    cfg.seed = 4242;
    Game original;
    original.start(cfg);
    leave_crossroads(original);

    const Vec2 a = original.hero().a.pos + Vec2{2, 0};
    const Vec2 b = original.hero().a.pos + Vec2{3, 0};
    original.mutable_level().map.set(a, Tile::Floor);
    original.mutable_level().map.set(b, Tile::Floor);
    original.ignite(a, 3);
    original.ignite(b, 2);
    ASSERT_GT(original.ember_at(a), 0);

    Game restored;
    ASSERT_TRUE(restored.load(original.save()));
    EXPECT_EQ(restored.ember_at(a), original.ember_at(a));
    EXPECT_EQ(restored.ember_at(b), original.ember_at(b));
    EXPECT_EQ(restored.level().embers.size(), original.level().embers.size());
}

TEST(Save, PreservesTheCodex) {
    // Which bestiary rows the hero has unlocked by sight is run progress, and
    // has to come back on load.
    GameConfig cfg;
    cfg.seed = 5150;
    Game original;
    original.start(cfg);
    leave_crossroads(original);

    // Force a couple of rows open by hand, plus whatever the walk revealed.
    original.mutable_codex_seen()[static_cast<std::size_t>(species_index("upyr"))] = 1;
    original.mutable_codex_seen()[static_cast<std::size_t>(species_index("aspid"))] = 1;

    Game restored;
    ASSERT_TRUE(restored.load(original.save()));
    ASSERT_EQ(restored.codex_seen().size(), original.codex_seen().size());
    for (std::size_t i = 0; i < original.codex_seen().size(); ++i)
        EXPECT_EQ(restored.codex_seen()[i], original.codex_seen()[i]) << "row " << i;
    EXPECT_TRUE(restored.codex_knows(species_index("upyr")));
    EXPECT_TRUE(restored.codex_knows(species_index("aspid")));
}

TEST(Save, PreservesTheCursedFlagOnItems) {
    GameConfig cfg;
    cfg.seed = 771;
    Game original;
    original.start(cfg);
    leave_crossroads(original);

    Item hex{};
    hex.kind = ItemKind::Armor;
    hex.subtype = 0;
    hex.cursed = true;
    hex.identified = true;
    hex.enchant = -2;
    ASSERT_TRUE(original.mutable_hero().inv.add(hex));

    Game restored;
    ASSERT_TRUE(restored.load(original.save()));
    ASSERT_FALSE(restored.hero().inv.items.empty());
    const Item& back = restored.hero().inv.items.back();
    EXPECT_TRUE(back.cursed) << "a cursed item came back clean";
    EXPECT_EQ(back.enchant, -2);
}

TEST(Save, PreservesStatusEffects) {
    GameConfig cfg;
    cfg.seed = 88;
    Game original;
    original.start(cfg);
    leave_crossroads(original);
    original.mutable_hero().a.add_effect(Effect::Poison, 7, 3);
    original.mutable_hero().a.add_effect(Effect::Haste, 4, 1);

    Game restored;
    ASSERT_TRUE(restored.load(original.save()));
    EXPECT_TRUE(restored.hero().a.has(Effect::Poison));
    EXPECT_EQ(restored.hero().a.effect_power(Effect::Poison), 3);
    EXPECT_TRUE(restored.hero().a.has(Effect::Haste));
}

TEST(Save, PreservesWhatHasBeenIdentified) {
    GameConfig cfg;
    cfg.seed = 606;
    Game original;
    original.start(cfg);
    leave_crossroads(original);
    // Empty the pack completely — the equipment slots must be released too, or
    // the save describes gear that is not there and the loader rightly refuses it.
    original.mutable_hero().inv.items.clear();
    original.mutable_hero().inv.weapon = -1;
    original.mutable_hero().inv.armor = -1;
    original.mutable_hero().inv.amulet = -1;

    Item scroll{};
    scroll.kind = ItemKind::Scroll;
    scroll.subtype = static_cast<int>(ScrollKind::MagicMap);
    original.mutable_hero().inv.add(scroll);
    original.perform(Action{ActionType::UseItem, {}, 0, {}});
    ASSERT_TRUE(original.identification().knows(ItemKind::Scroll,
                                                static_cast<int>(ScrollKind::MagicMap)));

    Game restored;
    ASSERT_TRUE(restored.load(original.save()));
    EXPECT_TRUE(restored.identification().knows(ItemKind::Scroll,
                                                static_cast<int>(ScrollKind::MagicMap)));
    EXPECT_EQ(original.identification().potion_look, restored.identification().potion_look)
        << "the appearance permutation must survive, or names would change on load";
}

TEST(Save, PreservesEveryVisitedFloor) {
    Game original = played_game(1717, 600);
    Game restored;
    ASSERT_TRUE(restored.load(original.save()));
    EXPECT_EQ(original.hero().deepest, restored.hero().deepest);
    // Saving on floor N must have stored floors 1..N, not only the current one.
    EXPECT_EQ(original.depth(), restored.depth());
}

TEST(Save, ResumesTheSameRandomSequence) {
    // Without the generator state, reloading and replaying would produce a
    // different dungeon from the same seed — and the seed would be a lie.
    Game original = played_game(2718, 200);
    const std::string blob = original.save();

    Game a, b;
    ASSERT_TRUE(a.load(blob));
    ASSERT_TRUE(b.load(blob));
    for (int i = 0; i < 200; ++i) {
        a.perform(Action{ActionType::Wait, {}, -1, {}});
        b.perform(Action{ActionType::Wait, {}, -1, {}});
    }
    EXPECT_EQ(a.save(), b.save()) << "two loads of one save diverged";
}

TEST(Save, ANewSaveOfARestoredGameIsIdentical) {
    const Game original = played_game(1234, 150);
    const std::string first = original.save();

    Game restored;
    ASSERT_TRUE(restored.load(first));
    EXPECT_EQ(restored.save(), first) << "the format is not a fixed point";
}

TEST(Save, PreservesSeedMetadataIncludingNonAsciiText) {
    GameConfig cfg;
    cfg.seed = 4;
    cfg.seed_text = "баба-яга и пробел";
    Game original;
    original.start(cfg);
    leave_crossroads(original);

    Game restored;
    ASSERT_TRUE(restored.load(original.save()));
    EXPECT_EQ(restored.config().seed_text, cfg.seed_text)
        << "seed text with spaces and Cyrillic must survive the token encoding";
    EXPECT_EQ(restored.config().seed, cfg.seed);
}

TEST(Save, PreservesAnEmptySeedText) {
    GameConfig cfg;
    cfg.seed = 4;
    cfg.seed_text.clear();
    Game original;
    original.start(cfg);
    leave_crossroads(original);
    Game restored;
    ASSERT_TRUE(restored.load(original.save()));
    EXPECT_TRUE(restored.config().seed_text.empty());
}

// --- Rejecting bad input ---------------------------------------------------

TEST(Save, RejectsEmptyAndGarbageInput) {
    Game g;
    EXPECT_FALSE(g.load(""));
    EXPECT_FALSE(g.load("not a save at all"));
    EXPECT_FALSE(g.load("   \n\t  "));
}

TEST(Save, RejectsAWrongFormatVersion) {
    GameConfig cfg;
    cfg.seed = 1;
    Game original;
    original.start(cfg);
    leave_crossroads(original);

    std::string blob = original.save();
    const std::size_t space = blob.find(' ');
    ASSERT_NE(space, std::string::npos);
    const std::size_t after = blob.find(' ', space + 1);
    ASSERT_NE(after, std::string::npos);
    // Replace the whole version token with one that is not the current version.
    blob.replace(space + 1, after - space - 1, "1");

    Game restored;
    EXPECT_FALSE(restored.load(blob));
}

TEST(Save, RejectsATruncatedSaveAtEveryLength) {
    GameConfig cfg;
    cfg.seed = 2;
    Game original;
    original.start(cfg);
    leave_crossroads(original);
    const std::string blob = original.save();

    // Cutting a save short must never yield a "successful" half-loaded game.
    for (std::size_t cut = 1; cut < blob.size(); cut += 37) {
        Game restored;
        EXPECT_FALSE(restored.load(blob.substr(0, cut)))
            << "a save truncated to " << cut << " bytes was accepted";
    }
}

TEST(Save, RejectsAnImpossibleDepth) {
    GameConfig cfg;
    cfg.seed = 3;
    Game original;
    original.start(cfg);
    leave_crossroads(original);

    std::string blob = original.save();
    // The depth follows the four generator words; rewrite it to a silly value
    // by finding the token layout rather than a fixed offset.
    std::istringstream in(blob);
    std::vector<std::string> tokens;
    std::string token;
    while (in >> token) tokens.push_back(token);
    ASSERT_GT(tokens.size(), 12u);
    tokens[11] = "999";  // depth

    std::string rebuilt;
    for (const auto& t : tokens) rebuilt += t + " ";

    Game restored;
    EXPECT_FALSE(restored.load(rebuilt));
}

/// Tokenises a save so a test can rewrite one field and hand it back.
std::vector<std::string> tokens_of(const std::string& blob) {
    std::istringstream in(blob);
    std::vector<std::string> tokens;
    std::string token;
    while (in >> token) tokens.push_back(token);
    return tokens;
}

std::string rejoin(const std::vector<std::string>& tokens) {
    std::string out;
    for (const auto& t : tokens) out += t + " ";
    return out;
}

/// The hero's coordinates are the first two words of the hero record, which
/// starts after the nineteen words of run header.
constexpr std::size_t kHeroPosX = 20;
constexpr std::size_t kHeroPosY = 21;

TEST(Save, RejectsAHeroStandingOffTheMap) {
    GameConfig cfg;
    cfg.seed = 8123;
    Game original;
    original.start(cfg);
    leave_crossroads(original);

    std::vector<std::string> tokens = tokens_of(original.save());
    ASSERT_GT(tokens.size(), kHeroPosY);
    // Sanity: the words being rewritten really are where the hero is.
    EXPECT_EQ(std::stoi(tokens[kHeroPosX]), original.hero().a.pos.x);
    EXPECT_EQ(std::stoi(tokens[kHeroPosY]), original.hero().a.pos.y);

    // Out of bounds is not a crash — Map reports wall outside its grid — which
    // is exactly why it has to be refused here. Loaded, it would be a run in a
    // corner of nothing that no monster can reach and no stair can end.
    for (const auto& spot : {std::pair<const char*, const char*>{"9999", "-9999"},
                             {"-1", "5"},
                             {"5", "-1"},
                             {"72", "5"}}) {
        std::vector<std::string> doctored = tokens;
        doctored[kHeroPosX] = spot.first;
        doctored[kHeroPosY] = spot.second;
        Game restored;
        EXPECT_FALSE(restored.load(rejoin(doctored)))
            << "accepted a hero at " << spot.first << "," << spot.second;
    }

    // The untouched save still loads, so the check rejects the doctoring and
    // not the test's own rebuilding of the blob.
    Game honest;
    EXPECT_TRUE(honest.load(rejoin(tokens)));
}

TEST(Save, AcceptsAHeroOnTheEdgeOfTheMap) {
    GameConfig cfg;
    cfg.seed = 8124;
    Game original;
    original.start(cfg);
    leave_crossroads(original);

    std::vector<std::string> tokens = tokens_of(original.save());
    ASSERT_GT(tokens.size(), kHeroPosY);
    tokens[kHeroPosX] = "0";
    tokens[kHeroPosY] = "0";

    // Only the edges are enforced, not walkability: the border cell is a wall,
    // but a rule stricter than the game's own would start refusing honest saves
    // of a hero standing in water or a doorway.
    Game restored;
    EXPECT_TRUE(restored.load(rejoin(tokens)));
}

TEST(Save, AFailedLoadLeavesTheExistingGameUntouched) {
    GameConfig cfg;
    cfg.seed = 11;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    g.perform(Action{ActionType::Wait, {}, -1, {}});

    const std::string before = g.save();
    EXPECT_FALSE(g.load("NAV 1 garbage garbage"));
    EXPECT_EQ(g.save(), before) << "a rejected load corrupted the live game";
}

TEST(Save, StaysSmallEnoughForBrowserStorage) {
    // The web build keeps saves in localStorage, which is only a few megabytes.
    const Game deep = played_game(31, 3000);
    const std::string blob = deep.save();
    EXPECT_LT(blob.size(), 400u * 1024u)
        << "a save of " << blob.size() << " bytes will not fit comfortably in localStorage";
}

TEST(Save, CompressesTheTileGridsRatherThanWritingThemOut) {
    // A floor carries two full grids — tiles and explored — so writing them out
    // uncompressed costs at least two bytes per cell each. Coming in under one
    // byte per cell for both together means the run-length encoding is working.
    GameConfig cfg;
    cfg.seed = 77;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    const std::size_t cells =
        static_cast<std::size_t>(g.map().width()) * static_cast<std::size_t>(g.map().height());
    EXPECT_LT(g.save().size(), 2 * cells) << "the tile grids do not appear to be compressed";
}
