// SPDX-License-Identifier: MIT
//
// Each boss fight is built on a mechanic from the story it comes from, so each
// gets tests for the mechanic itself and for its counter-play. A boss whose
// only property is a large pile of health needs no tests — and is no fight.
#include <gtest/gtest.h>

#include <cstring>

#include "nav/game.hpp"

#include "support.hpp"

using namespace nav;

namespace {

/// An open floor with a durable hero, so a test measures what the boss does
/// rather than how long the hero survives it.
class BossArena {
public:
    explicit BossArena(std::uint64_t seed = 5150) {
        GameConfig cfg;
        cfg.seed = seed;
        cfg.hero_class = HeroClass::Vityaz;
        game.start(cfg);
        leave_crossroads(game);

        Level& lvl = game.mutable_level();
        lvl.monsters.clear();
        lvl.items.clear();
        for (int y = 1; y < lvl.map.height() - 1; ++y)
            for (int x = 1; x < lvl.map.width() - 1; ++x) {
                const Tile t = lvl.map.at({x, y});
                if (t != Tile::StairsUp && t != Tile::StairsDown) lvl.map.set({x, y}, Tile::Floor);
            }
        game.mutable_hero().a.pos = {20, 15};
        game.mutable_hero().a.max_hp = 100000;
        game.mutable_hero().a.hp = 100000;
        game.mutable_hero().nutrition = 100000;
        game.refresh_view();
    }

    Monster& spawn(const char* key, Vec2 pos, int hp = -1) {
        const int index = species_index(key);
        EXPECT_GE(index, 0) << key;
        const Species& sp = bestiary()[static_cast<std::size_t>(index)];
        Monster m{};
        m.species = index;
        m.a.pos = pos;
        m.a.hp = m.a.max_hp = hp > 0 ? hp : sp.hp;
        m.a.attack = sp.attack;
        m.a.defence = sp.defence;
        m.a.speed = sp.speed;
        m.awake = true;
        game.mutable_level().monsters.push_back(m);
        game.refresh_view();
        return game.mutable_level().monsters.back();
    }

    Monster* find(const char* key) {
        const int index = species_index(key);
        for (auto& m : game.mutable_level().monsters)
            if (m.species == index) return &m;
        return nullptr;
    }

    void wait(int turns) {
        for (int i = 0; i < turns; ++i) game.perform(Action{ActionType::Wait, {}, -1, {}});
    }

    Game game;
};

/// Descends to `depth` by force, leaving the floor as it was generated.
Game descend_to(int depth, std::uint64_t seed) {
    GameConfig cfg;
    cfg.seed = seed;
    Game g;
    g.start(cfg);
    leave_crossroads(g);
    for (int d = 1; d < depth; ++d) {
        g.mutable_hero().a.pos = g.level().exit;
        g.refresh_view();
        EXPECT_TRUE(g.perform(Action{ActionType::Descend, {}, -1, {}}));
    }
    return g;
}

}  // namespace

// ---------------------------------------------------------------------------
// Вий — «поднимите мне веки»
// ---------------------------------------------------------------------------

TEST(Viy, OpensHisEyesOnACycleAndStrikesWhatHeCanSee) {
    BossArena a;
    a.spawn("viy", {24, 15});
    const int hp_before = a.game.hero().a.hp;

    a.wait(10);  // more than two full cycles

    EXPECT_LT(a.game.hero().a.hp, hp_before) << "the gaze never landed";
    bool warned = false, struck = false;
    for (const auto& entry : a.game.log()) {
        if (entry.text.ru.find("веки") != std::string::npos) warned = true;
        if (entry.text.ru.find("Поднимите мне веки") != std::string::npos) struck = true;
    }
    EXPECT_TRUE(warned) << "the gaze is never telegraphed";
    EXPECT_TRUE(struck) << "the gaze never actually fired";
}

TEST(Viy, TheGazeBlindsItsTarget) {
    BossArena a;
    a.spawn("viy", {24, 15});
    bool blinded = false;
    for (int i = 0; i < 12 && !blinded; ++i) {
        a.wait(1);
        blinded = a.game.hero().a.has(Effect::Blind);
    }
    EXPECT_TRUE(blinded) << "Viy's gaze should leave the hero blind";
}

TEST(Viy, BreakingLineOfSightIsTheCounterPlay) {
    // The whole fight is "do not be in view when the eyelids rise". A hero
    // behind a wall must take nothing from the gaze.
    BossArena a;
    a.spawn("viy", {24, 15});
    for (int y = 10; y <= 20; ++y) a.game.mutable_level().map.set({22, y}, Tile::Wall);
    a.game.refresh_view();

    const int hp_before = a.game.hero().a.hp;
    a.wait(14);

    bool missed = false;
    for (const auto& entry : a.game.log())
        if (entry.text.ru.find("впустую") != std::string::npos) missed = true;
    EXPECT_TRUE(missed) << "the gaze should have found nothing to look at";
    EXPECT_EQ(a.game.hero().a.hp, hp_before) << "a walled-off hero was hit by the gaze anyway";
}

TEST(Viy, IsSofterWhileHisEyelidsAreDown) {
    // The window between gazes is when the fight is winnable, so a blow landed
    // then has to be worth more. Averaged over many swings to see past the roll.
    auto total_damage = [](int charge) {
        long long total = 0;
        for (std::uint64_t seed = 0; seed < 60; ++seed) {
            BossArena a(seed + 11);
            Monster& viy = a.spawn("viy", a.game.hero().a.pos + Vec2{1, 0}, 100000);
            viy.charge = charge;
            const int before = viy.a.hp;
            a.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
            const Monster* after = a.find("viy");
            if (after) total += before - after->a.hp;
        }
        return total;
    };

    const long long eyes_shut = total_damage(0);
    const long long eyes_opening = total_damage(3);
    EXPECT_GT(eyes_shut, eyes_opening)
        << "hitting Viy while his eyes are shut should hurt him more (" << eyes_shut << " vs "
        << eyes_opening << ")";
}

TEST(Viy, GuardsTheFourthFloor) {
    Game g = descend_to(4, 909);
    ASSERT_EQ(g.depth(), 4);
    bool found = false;
    for (const auto& m : g.monsters())
        if (std::strcmp(bestiary()[static_cast<std::size_t>(m.species)].key, "viy") == 0) found = true;
    EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// Баба-Яга — пока стоит изба
// ---------------------------------------------------------------------------

TEST(BabaYaga, ArrivesWithHerHuts) {
    Game g = descend_to(8, 4321);
    ASSERT_EQ(g.depth(), 8);

    int yaga = 0, huts = 0;
    for (const auto& m : g.monsters()) {
        const char* key = bestiary()[static_cast<std::size_t>(m.species)].key;
        if (std::strcmp(key, "babayaga") == 0) ++yaga;
        if (std::strcmp(key, "izbushka") == 0) ++huts;
    }
    EXPECT_EQ(yaga, 1);
    EXPECT_GT(huts, 0) << "Баба-Яга is standing on her own floor without a hut";
}

TEST(BabaYaga, IsAllButUntouchableWhileAHutStands) {
    auto damage_dealt = [](bool with_hut) {
        long long total = 0;
        for (std::uint64_t seed = 0; seed < 40; ++seed) {
            BossArena a(seed + 77);
            Monster& yaga = a.spawn("babayaga", a.game.hero().a.pos + Vec2{1, 0}, 100000);
            const int before = yaga.a.hp;
            if (with_hut) a.spawn("izbushka", a.game.hero().a.pos + Vec2{-3, -3});
            a.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
            const Monster* after = a.find("babayaga");
            if (after) total += before - after->a.hp;
        }
        return total;
    };

    const long long shielded = damage_dealt(true);
    const long long exposed = damage_dealt(false);
    EXPECT_GT(exposed, shielded * 2)
        << "the huts barely protect her (" << shielded << " shielded vs " << exposed << " exposed)";
    EXPECT_GT(shielded, 0) << "even shielded, a blow must land for at least one point";
}

TEST(BabaYaga, BecomesVulnerableOnceTheLastHutFalls) {
    BossArena a;
    a.spawn("babayaga", a.game.hero().a.pos + Vec2{1, 0}, 100000);
    a.spawn("izbushka", a.game.hero().a.pos + Vec2{-4, -4}, 1);
    ASSERT_NE(a.find("izbushka"), nullptr);

    // Knock the hut down: it is stationary, so walking over and hitting it works.
    a.game.mutable_level().monsters.back().a.pos = a.game.hero().a.pos + Vec2{0, -1};
    a.game.refresh_view();
    ASSERT_TRUE(a.game.perform(Action{ActionType::Move, {0, -1}, -1, {}}));
    EXPECT_EQ(a.find("izbushka"), nullptr) << "the hut survived a direct blow at one health";

    bool announced = false;
    for (const auto& entry : a.game.log())
        if (entry.text.ru.find("без защиты") != std::string::npos) announced = true;
    EXPECT_TRUE(announced) << "the player is never told the shield is down";
}

TEST(BabaYaga, HerHutsDoNotWanderOff) {
    BossArena a;
    Monster& hut = a.spawn("izbushka", {26, 15});
    const Vec2 start = hut.a.pos;
    a.wait(12);
    const Monster* after = a.find("izbushka");
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->a.pos, start) << "a hut on hen's legs is still supposed to stay put";
}

// ---------------------------------------------------------------------------
// Кощей — смерть на конце иглы
// ---------------------------------------------------------------------------

TEST(Koschei, TheNeedleIsAlwaysOnHisFloor) {
    // Asked for by his name, not by a depth number. The number was once the
    // same as the bottom of the dungeon; the dungeon grew, this test kept
    // passing, and the needle ended up four floors below the only creature it
    // works on (NAV-011).
    const int floor = boss_depth("koschei");
    ASSERT_GT(floor, 0);
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        Game g = descend_to(floor, seed + 100);
        ASSERT_EQ(g.depth(), floor) << "seed " << seed;

        int needles = 0;
        for (const auto& it : g.floor_items())
            if (it.kind == ItemKind::Needle) ++needles;
        EXPECT_EQ(needles, 1) << "seed " << seed << ": the needle is missing or duplicated";
    }
}

TEST(Koschei, TheNeedleIsReachable) {
    const int floor = boss_depth("koschei");
    for (std::uint64_t seed = 0; seed < 6; ++seed) {
        Game g = descend_to(floor, seed + 500);
        Vec2 needle{-1, -1};
        for (const auto& it : g.floor_items())
            if (it.kind == ItemKind::Needle) needle = it.pos;
        ASSERT_GE(needle.x, 0) << "seed " << seed;
        EXPECT_FALSE(find_path(g.map(), g.hero().a.pos, needle, 30000).empty())
            << "seed " << seed << ": the needle cannot be walked to";
    }
}

TEST(Koschei, KeepsRisingUntilTheNeedleBreaks) {
    BossArena a;
    Monster& koschei = a.spawn("koschei", a.game.hero().a.pos + Vec2{1, 0}, 1);
    (void)koschei;

    for (int i = 0; i < 3; ++i) {
        // Put him back at one health beside the hero and swing again.
        Monster* target = a.find("koschei");
        ASSERT_NE(target, nullptr) << "he stayed dead on strike " << i;
        target->a.hp = 1;
        target->a.pos = a.game.hero().a.pos + Vec2{1, 0};
        a.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
    }

    const Monster* survivor = a.find("koschei");
    ASSERT_NE(survivor, nullptr);
    EXPECT_EQ(survivor->revives, 3);
    EXPECT_EQ(a.game.state(), RunState::Playing);
}

TEST(Koschei, TheFirstResurrectionRevealsWhereTheNeedleIs) {
    // A mechanic the player cannot discover is only unfair, so the floor gives
    // itself up the first time he gets back on his feet.
    BossArena a;
    Monster& koschei = a.spawn("koschei", a.game.hero().a.pos + Vec2{1, 0}, 1);
    (void)koschei;

    const Vec2 far_corner{a.game.map().width() - 2, a.game.map().height() - 2};
    ASSERT_FALSE(a.game.map().explored(far_corner));

    a.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
    EXPECT_TRUE(a.game.map().explored(far_corner)) << "the floor was not revealed";
}

TEST(Koschei, BreakingTheNeedleIsWhatEndsHim) {
    BossArena a;
    Item needle{};
    needle.kind = ItemKind::Needle;
    needle.identified = true;
    ASSERT_TRUE(a.game.mutable_hero().inv.add(needle));
    ASSERT_TRUE(a.game.needle_intact());

    ASSERT_TRUE(a.game.perform(Action{ActionType::UseItem, {},
                                      static_cast<int>(a.game.hero().inv.items.size()) - 1, {}}));
    EXPECT_FALSE(a.game.needle_intact());

    a.spawn("koschei", a.game.hero().a.pos + Vec2{1, 0}, 1);
    a.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});

    EXPECT_EQ(a.find("koschei"), nullptr);
    // He stays down — but he is no longer the end of the dungeon, so the run
    // carries on. The ending belongs to whatever waits on the bottom floor.
    EXPECT_EQ(a.game.state(), RunState::Playing);
}

TEST(Koschei, AFullPackCannotLockThePlayerOutOfHisDeath) {
    // Twenty things in hand and the needle underfoot used to mean Кощей could
    // not be killed at all, with nothing on screen to explain it (NAV-012).
    BossArena a;
    Inventory& inv = a.game.mutable_hero().inv;
    while (inv.items.size() < Inventory::kCapacity) {
        Item filler{};
        filler.kind = ItemKind::Weapon;
        filler.subtype = 0;
        filler.identified = true;
        ASSERT_TRUE(inv.add(filler));
    }
    ASSERT_TRUE(inv.full());

    Item needle{};
    needle.kind = ItemKind::Needle;
    needle.identified = true;
    needle.pos = a.game.hero().a.pos;
    a.game.mutable_level().items.push_back(needle);

    ASSERT_TRUE(a.game.needle_intact());
    ASSERT_TRUE(a.game.perform(Action{ActionType::PickUp, {}, -1, {}}));
    EXPECT_FALSE(a.game.needle_intact()) << "a full pack still hides the ending";
}

TEST(Koschei, TheEndingBelongsToTheBottomFloorsGuardian) {
    // Whoever is last is asked for by depth. Naming Кощей was true for exactly
    // as long as he was the last thing down there (NAV-013).
    const char* last = boss_for_depth(kMaxDepth);
    ASSERT_NE(last, nullptr);
    EXPECT_STRNE(last, "koschei") << "if this ever changes, so must the ending";

    BossArena a;
    a.spawn(last, a.game.hero().a.pos + Vec2{1, 0}, 1);
    a.game.mutable_hero().a.attack = 500;
    a.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
    EXPECT_EQ(a.game.state(), RunState::Ascended);
}

TEST(Koschei, TheBrokenNeedleSurvivesASaveAndLoad) {
    BossArena a;
    Item needle{};
    needle.kind = ItemKind::Needle;
    a.game.mutable_hero().inv.add(needle);
    a.game.perform(Action{ActionType::UseItem, {},
                          static_cast<int>(a.game.hero().inv.items.size()) - 1, {}});
    ASSERT_FALSE(a.game.needle_intact());

    Game restored;
    ASSERT_TRUE(restored.load(a.game.save()));
    EXPECT_FALSE(restored.needle_intact())
        << "reloading handed Кощей his immortality back";
}

TEST(Bosses, EachOneStillLeavesLootWorthTheFight) {
    BossArena a;
    a.spawn("viy", a.game.hero().a.pos + Vec2{1, 0}, 1);
    a.game.perform(Action{ActionType::Move, {1, 0}, -1, {}});
    ASSERT_EQ(a.find("viy"), nullptr);

    bool enchanted_drop = false;
    for (const auto& it : a.game.floor_items())
        if (it.is_gear() && it.enchant > 0) enchanted_drop = true;
    EXPECT_TRUE(enchanted_drop) << "a slain boss left nothing behind";
}
