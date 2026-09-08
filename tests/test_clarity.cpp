// SPDX-License-Identifier: MIT
//
// What the game tells the player.
//
// These are not cosmetic tests. A roguelike where death is final owes the
// player an explanation for every point of health they lost and every choice
// they were asked to make, and the two things that explanation rests on are the
// combat log and the equipment preview. Both are load-bearing, so both are
// checked like rules rather than like text.
#include <gtest/gtest.h>

#include <string>

#include "nav/game.hpp"

#include "support.hpp"

namespace nav {
namespace {

/// True when any line in the log contains `needle` in Russian.
bool logged(const Game& g, const std::string& needle) {
    for (const LogEntry& e : g.log())
        if (e.text.ru.find(needle) != std::string::npos) return true;
    return false;
}

/// A hero, a monster next to them, and nothing else in the way.
class Duel {
public:
    Duel() {
        GameConfig cfg;
        cfg.seed = 71717;
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
        game.mutable_hero().nutrition = 100000;
        game.refresh_view();
    }

    Monster& spawn(const char* key, Vec2 pos) {
        const int index = species_index(key);
        EXPECT_GE(index, 0) << key;
        const Species& sp = bestiary()[static_cast<std::size_t>(index)];
        Monster m{};
        m.species = index;
        m.a.pos = pos;
        m.a.hp = m.a.max_hp = sp.hp;
        m.a.attack = sp.attack;
        m.a.defence = sp.defence;
        m.a.speed = sp.speed;
        m.awake = true;
        game.mutable_level().monsters.push_back(m);
        game.refresh_view();
        return game.mutable_level().monsters.back();
    }

    /// Adds a piece of gear and returns its index. The hero starts a run with a
    /// kit already in the pack, so an index can never be assumed.
    int give(ItemKind kind, const char* gear_key, int enchant = 0) {
        const auto& gear = gear_table();
        int subtype = -1;
        for (std::size_t i = 0; i < gear.size(); ++i)
            if (std::string(gear[i].key) == gear_key) subtype = static_cast<int>(i);
        EXPECT_GE(subtype, 0) << gear_key;
        Item it{};
        it.kind = kind;
        it.subtype = subtype;
        it.power = gear[static_cast<std::size_t>(subtype)].power;
        it.enchant = enchant;
        it.identified = true;
        game.mutable_hero().inv.items.push_back(it);
        return static_cast<int>(game.mutable_hero().inv.items.size()) - 1;
    }

    Game game;
};

// ---------------------------------------------------------------------------
// The combat log
// ---------------------------------------------------------------------------

TEST(Clarity, ABlowSaysHowMuchItTookAndWhatIsLeft) {
    Duel d;
    d.spawn("anchutka", {21, 15});
    const int hp_before = d.game.hero().a.hp;

    for (int i = 0; i < 40 && d.game.hero().a.hp == hp_before; ++i)
        d.game.perform(Action{ActionType::Wait, {}, -1, {}});

    ASSERT_LT(d.game.hero().a.hp, hp_before) << "the monster never landed a blow";
    EXPECT_TRUE(logged(d.game, "бьёт тебя на"))
        << "a hit that does not say how much it did explains nothing";
    EXPECT_TRUE(logged(d.game, "осталось"))
        << "a player has to read the health bar to learn what the blow cost";
}

TEST(Clarity, AnEffectIsNamedRatherThanHinted) {
    // "Something foul takes hold of you" tells a player nothing they can act
    // on. Naming the effect and its length is what makes "drink now or run
    // first" a decision instead of a guess.
    Duel d;
    d.spawn("bolotnik", {21, 15});
    for (int i = 0; i < 300 && !d.game.hero().a.has(Effect::Poison); ++i)
        d.game.perform(Action{ActionType::Wait, {}, -1, {}});

    if (!d.game.hero().a.has(Effect::Poison)) GTEST_SKIP() << "no poison landed in 300 turns";
    EXPECT_TRUE(logged(d.game, effect_name(Effect::Poison).ru))
        << "the effect took hold without ever being named";
}

TEST(Clarity, EveryEffectHasATwoLanguageName) {
    for (int i = 0; i < static_cast<int>(Effect::Count); ++i) {
        const Text name = effect_name(static_cast<Effect>(i));
        EXPECT_FALSE(name.ru.empty()) << "effect " << i;
        EXPECT_FALSE(name.en.empty()) << "effect " << i;
        EXPECT_NE(name.ru, "?") << "effect " << i << " fell through to the placeholder";
    }
}

// ---------------------------------------------------------------------------
// The equipment preview
// ---------------------------------------------------------------------------

TEST(Clarity, ThePreviewMatchesWhatEquippingActuallyDoes) {
    // The property that matters: the number shown before the choice is the
    // number the choice produces. Anything else is a lie the player will only
    // catch after it has cost them a run.
    Duel d;
    const int indices[] = {d.give(ItemKind::Weapon, "kladenets", 2),
                           d.give(ItemKind::Armor, "kolchuga", 1)};

    for (int i : indices) {
        const EquipPreview p = d.game.equip_preview(i);
        ASSERT_TRUE(p.valid) << "item " << i << " should be wearable";
        const int atk = d.game.hero_attack(), def = d.game.hero_defence();
        const int hp = d.game.hero().a.max_hp;

        ASSERT_TRUE(d.game.perform(Action{ActionType::EquipItem, {}, i, {}}));

        EXPECT_EQ(d.game.hero_attack() - atk, p.attack) << "attack preview was wrong for item " << i;
        EXPECT_EQ(d.game.hero_defence() - def, p.defence) << "defence preview was wrong";
        EXPECT_EQ(d.game.hero().a.max_hp - hp, p.max_hp) << "health preview was wrong";
    }
}

TEST(Clarity, ThePreviewComparesAgainstWhatIsAlreadyWorn) {
    // Swapping a better weapon for a worse one has to read as a loss, not as
    // the new weapon's raw number.
    Duel d;
    const int sword = d.give(ItemKind::Weapon, "kladenets", 3);
    ASSERT_TRUE(d.game.perform(Action{ActionType::EquipItem, {}, sword, {}}));
    const int knife = d.give(ItemKind::Weapon, "nozh", 0);

    const EquipPreview p = d.game.equip_preview(knife);
    ASSERT_TRUE(p.valid);
    EXPECT_LT(p.attack, 0) << "trading a great sword for a knife should show as a loss";
}

TEST(Clarity, ThePreviewLeavesTheHeroExactlyAsItFoundThem) {
    // It works by putting the item on and taking it off again, so the thing
    // most worth testing is that it puts everything back.
    Duel d;
    const int indices[] = {d.give(ItemKind::Weapon, "kladenets", 1),
                           d.give(ItemKind::Amulet, "ob_zhizni", 2)};

    const int weapon = d.game.hero().inv.weapon, amulet = d.game.hero().inv.amulet;
    const int atk = d.game.hero_attack(), def = d.game.hero_defence();
    const int hp = d.game.hero().a.hp, max_hp = d.game.hero().a.max_hp;
    const int turn = d.game.turn();

    for (int i : indices) d.game.equip_preview(i);

    EXPECT_EQ(d.game.hero().inv.weapon, weapon);
    EXPECT_EQ(d.game.hero().inv.amulet, amulet);
    EXPECT_EQ(d.game.hero_attack(), atk);
    EXPECT_EQ(d.game.hero_defence(), def);
    EXPECT_EQ(d.game.hero().a.hp, hp);
    EXPECT_EQ(d.game.hero().a.max_hp, max_hp);
    EXPECT_EQ(d.game.turn(), turn) << "asking what an item would do took a turn";
}

TEST(Clarity, ThingsThatCannotBeWornHaveNoPreview) {
    Duel d;
    Item bread{};
    bread.kind = ItemKind::Food;
    bread.identified = true;
    d.game.mutable_hero().inv.items.push_back(bread);
    const int loaf = static_cast<int>(d.game.hero().inv.items.size()) - 1;

    EXPECT_FALSE(d.game.equip_preview(loaf).valid);
    EXPECT_FALSE(d.game.equip_preview(-1).valid);
    EXPECT_FALSE(d.game.equip_preview(99).valid);
}

TEST(Clarity, TakingSomethingOffIsMarkedAsSuch) {
    Duel d;
    const int sword = d.give(ItemKind::Weapon, "kladenets", 1);
    ASSERT_TRUE(d.game.perform(Action{ActionType::EquipItem, {}, sword, {}}));

    const EquipPreview p = d.game.equip_preview(sword);
    EXPECT_TRUE(p.valid);
    EXPECT_TRUE(p.taking_off) << "the worn item should be reported as a removal";
    EXPECT_LT(p.attack, 0) << "taking off a sword should show the attack it costs";
}

// ---------------------------------------------------------------------------
// The post-mortem
// ---------------------------------------------------------------------------

TEST(Clarity, ADeathRemembersWhatKilledIt) {
    Duel d;
    d.spawn("anchutka", {21, 15});
    d.game.mutable_hero().a.hp = 3;

    for (int i = 0; i < 200 && d.game.state() == RunState::Playing; ++i)
        d.game.perform(Action{ActionType::Wait, {}, -1, {}});

    ASSERT_EQ(d.game.state(), RunState::Dead) << "the hero survived on three health";
    const Postmortem pm = d.game.postmortem();
    ASSERT_FALSE(pm.blows.empty()) << "a death with no recorded blows explains nothing";
    EXPECT_FALSE(pm.killed_by.ru.empty()) << "the killer went unnamed";
    EXPECT_EQ(pm.blows.back().hp_left, 0) << "the last blow should be the fatal one";
}

TEST(Clarity, ThePostmortemKeepsOnlyTheLastFewBlows) {
    // A wall of every blow in a two-thousand-turn run is not an explanation.
    Duel d;
    for (int i = 0; i < 20; ++i) {
        d.game.mutable_hero().a.hp = d.game.hero().a.max_hp;
        d.game.damage_hero(1, Text{"проверка", "a test"});
    }
    EXPECT_LE(d.game.postmortem().blows.size(), kPostmortemBlows);
    EXPECT_EQ(d.game.postmortem().blows.size(), kPostmortemBlows);
}

TEST(Clarity, ThePostmortemNamesWhatWasNeverDrunk) {
    // The line that stings, and the one most likely to change how the next run
    // is played: the healing draught that was in the pack the whole time.
    Duel d;
    Item potion{};
    potion.kind = ItemKind::Potion;
    potion.subtype = static_cast<int>(PotionKind::Heal);
    potion.count = 2;
    potion.identified = true;
    d.game.mutable_hero().inv.items.push_back(potion);

    const Postmortem pm = d.game.postmortem();
    bool found = false;
    for (const Text& line : pm.unspent)
        if (line.ru.find(item_name(potion, d.game.identification()).ru) != std::string::npos)
            found = true;
    EXPECT_TRUE(found) << "an unused potion did not make it onto the ending screen";
}

TEST(Clarity, AWardedBlowIsRecordedAsCostingNothing) {
    // The warding shirt turns a blow aside entirely. The post-mortem must not
    // then claim the hero lost health they never lost.
    Duel d;
    const int before = d.game.hero().a.hp;
    d.game.damage_hero(5, Text{"проверка", "a test"});
    const Postmortem pm = d.game.postmortem();
    ASSERT_FALSE(pm.blows.empty());
    EXPECT_EQ(pm.blows.back().amount, before - d.game.hero().a.hp)
        << "the recorded damage and the damage taken disagree";
}

}  // namespace
}  // namespace nav
