// SPDX-License-Identifier: MIT
//
// The demo bot.
//
// Its job is not to play well. Its job is to reach the bottom often enough that
// the second half of the dungeon is exercised by a whole game rather than only
// by arena tests — where the hero is invulnerable, the descent is forced and the
// floor is empty. Until something could finish a run, "is the game beatable
// past the fifth floor" had no answer at all.
//
// It cheats in exactly one way, and knowingly: it reads the true map instead of
// only what it has explored. Teaching it to explore would make it a worse
// measuring instrument, not a better one — the question here is whether the
// fights and the curve are survivable, not whether a pathfinder can find a
// staircase. Everything else it does, a player could do.
#include "bot.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include "nav/fov.hpp"

namespace nav {
namespace {

/// Health fractions the policy turns on. Named because they are the balance
/// knobs of the bot itself, and a run of tuning should touch these and not the
/// code around them.
constexpr int kDrinkBelowPercent   = 50;   ///< Reach for a potion at or under this.
constexpr int kFleeBelowPercent    = 40;   ///< Break off and back away.
constexpr int kBreakOffBossPercent = 55;   ///< Leave a guardian alone below this.
constexpr int kRestUntilPercent    = 90;   ///< Sit still until this healthy again.
constexpr int kEngageBossPercent   = 80;   ///< Do not start a guardian below this.
constexpr int kFloorTurnBudget     = 900;  ///< Give up on clearing and move on.
constexpr int kRestTurnBudget      = 260;  ///< Never rest longer than this on one floor.
constexpr int kRestFoodFloor       = 450;  ///< Resting burns food; stop well before empty.

int percent(int value, int max) { return max > 0 ? value * 100 / max : 100; }

const Species& species_of(const Monster& m) {
    return bestiary()[static_cast<std::size_t>(m.species)];
}

/// What the bot thinks a piece of gear is worth in a slot.
///
/// Raw power plus a bonus for continuing a set the hero is already part-way
/// into. Without the second half the bot would swap a set piece out for one more
/// point of armour and never complete anything, which would leave the sets — a
/// whole system — untested by any full game.
int gear_value(const Game& g, const Item& it, GearSet wearing_a, GearSet wearing_b) {
    if (!it.is_gear()) return -1;
    const auto& table = gear_table();
    const std::size_t i = static_cast<std::size_t>(it.subtype);
    if (i >= table.size()) return -1;
    int value = it.total_power() * 4;
    const GearSet set = table[i].set;
    if (set != GearSet::None && (set == wearing_a || set == wearing_b)) value += 14;
    if (table[i].powers != GpNone) value += 3;
    (void)g;
    return value;
}

GearSet set_in_slot(const Inventory& inv, int slot) {
    if (slot < 0 || slot >= static_cast<int>(inv.items.size())) return GearSet::None;
    const Item& it = inv.items[static_cast<std::size_t>(slot)];
    if (!it.is_gear()) return GearSet::None;
    const auto& table = gear_table();
    const std::size_t i = static_cast<std::size_t>(it.subtype);
    return i < table.size() ? table[i].set : GearSet::None;
}

/// One game in progress, plus the policy that drives it.
class Bot {
public:
    Bot(std::uint64_t seed, HeroClass cls, BotMode mode)
        : mode_(mode), policy_(seed ^ 0x9E3779B97F4A7C15ULL) {
        GameConfig cfg;
        cfg.seed = seed;
        cfg.hero_class = cls;
        game_.start(cfg);
        result_.seed = seed;
        result_.cls = cls;
        if (mode_ == BotMode::Sweep) fortify();
    }

    /// Makes the hero strong enough to be a survey instrument.
    ///
    /// Deliberately blunt: more health, a heavier arm and a full larder. The
    /// point of a sweep is to walk the whole dungeon and meet everything in it,
    /// so anything that ends the run early defeats the measurement.
    /// Deliberately blunt: a heavy arm, thick skin and a full larder.
    ///
    /// Note what is *not* here: multiplying the health pool. Equipping anything
    /// recomputes `max_hp` from class and level, so a multiplier applied once
    /// evaporates the first time the bot puts on a hat — which is why the first
    /// sweeps kept dying to ordinary monsters with what looked like twenty
    /// times the health. The pool is topped up every turn instead.
    void fortify() {
        Hero& h = game_.mutable_hero();
        h.a.attack += 40;
        h.a.defence += 12;
        h.max_mana = std::max(h.max_mana, 60);
        h.mana = h.max_mana;
        h.nutrition = 20000;
    }

    BotResult run(int max_turns) {
        int last_depth = game_.depth();
        Vec2 previous = game_.hero().a.pos;

        for (int i = 0; i < max_turns && game_.state() == RunState::Playing; ++i) {
            if (game_.depth() != last_depth) {
                last_depth = game_.depth();
                floor_turns_ = 0;
                rested_ = 0;
            }
            ++floor_turns_;

            // A sweep is not allowed to starve or bleed out on the way: it is
            // measuring whether the dungeon can be walked, not whether this
            // hero can walk it.
            if (mode_ == BotMode::Sweep) {
                Hero& h = game_.mutable_hero();
                h.a.hp = h.a.max_hp;
                h.nutrition = 20000;
                h.a.clear_effect(Effect::Poison);
                h.a.clear_effect(Effect::Burn);
            }

            // A turn the policy could not spend is not the end of the run: the
            // hero waits and tries again. Treating it as the end quietly cut
            // runs short and made the depth histogram lie.
            if (!step() && !act(Action{ActionType::Wait, {}, -1, {}})) break;

            stuck_ = (game_.hero().a.pos == previous) ? stuck_ + 1 : 0;
            previous = game_.hero().a.pos;
        }

        finish();
        return result_;
    }

private:
    // --- perception ------------------------------------------------------

    const Monster* nearest_monster(int within = 1 << 30) const {
        const Monster* best = nullptr;
        int best_d = within;
        for (const auto& m : game_.monsters()) {
            if (!m.a.alive) continue;
            const int d = chebyshev(game_.hero().a.pos, m.a.pos);
            if (d < best_d) { best_d = d; best = &m; }
        }
        return best;
    }

    /// How many awake monsters are close enough to be a problem right now.
    int threats_near(int radius = 6) const {
        int n = 0;
        for (const auto& m : game_.monsters())
            if (m.a.alive && chebyshev(game_.hero().a.pos, m.a.pos) <= radius) ++n;
        return n;
    }

    int hp_percent() const { return percent(game_.hero().a.hp, game_.hero().a.max_hp); }

    // --- actions ---------------------------------------------------------

    bool act(const Action& a) { return game_.perform(a); }

    bool drink_healing() {
        const auto& inv = game_.hero().inv;
        for (std::size_t k = 0; k < inv.items.size(); ++k) {
            const Item& it = inv.items[k];
            if (it.kind != ItemKind::Potion) continue;
            if (it.subtype != static_cast<int>(PotionKind::Heal) &&
                it.subtype != static_cast<int>(PotionKind::GreaterHeal))
                continue;
            if (act(Action{ActionType::UseItem, {}, static_cast<int>(k), {}})) return true;
        }
        return false;
    }

    bool eat() {
        const auto& inv = game_.hero().inv;
        for (std::size_t k = 0; k < inv.items.size(); ++k)
            if (inv.items[k].kind == ItemKind::Food &&
                act(Action{ActionType::UseItem, {}, static_cast<int>(k), {}}))
                return true;
        return false;
    }

    /// Puts on anything better than what is worn, one piece a turn.
    bool equip_upgrade() {
        const Inventory& inv = game_.hero().inv;
        const GearSet a = set_in_slot(inv, inv.weapon);
        const GearSet b = set_in_slot(inv, inv.armor);
        const GearSet c = set_in_slot(inv, inv.amulet);

        for (std::size_t k = 0; k < inv.items.size(); ++k) {
            const Item& it = inv.items[k];
            if (!it.is_gear() || inv.is_equipped(static_cast<int>(k))) continue;

            int worn_slot = -1;
            if (it.kind == ItemKind::Weapon) worn_slot = inv.weapon;
            else if (it.kind == ItemKind::Armor) worn_slot = inv.armor;
            else if (it.kind == ItemKind::Amulet) worn_slot = inv.amulet;

            // Compare against the other two slots' sets, so a candidate is
            // judged on the set it would join rather than the one it replaces.
            const GearSet other_a = (it.kind == ItemKind::Weapon) ? b : a;
            const GearSet other_b = (it.kind == ItemKind::Amulet) ? b : c;

            const int mine = gear_value(game_, it, other_a, other_b);
            int worn = -1;
            if (worn_slot >= 0 && worn_slot < static_cast<int>(inv.items.size()))
                worn = gear_value(game_, inv.items[static_cast<std::size_t>(worn_slot)],
                                  other_a, other_b);
            if (mine <= worn) continue;
            if (act(Action{ActionType::EquipItem, {}, static_cast<int>(k), {}})) return true;
        }
        return false;
    }

    /// Steps to whichever neighbouring cell is furthest from the nearest threat.
    bool back_away() {
        const Monster* threat = nearest_monster(10);
        if (!threat) return false;
        const Vec2 me = game_.hero().a.pos;
        Vec2 best{0, 0};
        int best_gain = 0;
        for (Vec2 d : directions8()) {
            const Vec2 step = me + d;
            if (!game_.map().walkable(step) || game_.monster_at(step)) continue;
            const int gain = chebyshev(step, threat->a.pos) - chebyshev(me, threat->a.pos);
            if (gain > best_gain) { best_gain = gain; best = d; }
        }
        if (best_gain <= 0) return false;
        return act(Action{ActionType::Move, best, -1, {}});
    }

    /// Вий's counter-play: get out of the line of the gaze before it opens.
    bool dodge_gaze() {
        const Vec2 me = game_.hero().a.pos;
        for (const auto& m : game_.monsters()) {
            const Species& sp = species_of(m);
            if (std::strcmp(sp.key, "viy") != 0 || !m.a.alive) continue;
            // The warning comes one turn early, and the cycle shortens as he is
            // worn down — so react from the phase, not from a fixed number.
            const int cycle = m.phase >= 3 ? 2 : (m.phase == 2 ? 3 : 4);
            // Move a turn before the warning, not on it. The fight has a
            // rhythm — strike while the lids are down, be gone before they
            // rise — and a player who waits for the shout is already too late.
            if (m.charge < cycle - 2) continue;
            if (!has_line_of_sight(game_.map(), me, m.a.pos, sp.sight)) return false;
            for (Vec2 d : directions8()) {
                const Vec2 step = me + d;
                if (!game_.map().walkable(step) || game_.monster_at(step)) continue;
                if (has_line_of_sight(game_.map(), step, m.a.pos, sp.sight)) continue;
                if (act(Action{ActionType::Move, d, -1, {}})) return true;
            }
            // Nothing nearby breaks the line. Out-running the gaze is the other
            // half of the counter-play and the one a cornered player reaches
            // for: get beyond what he can see.
            if (chebyshev(me, m.a.pos) >= sp.sight - 1) return false;
            return back_away();
        }
        return false;
    }

    bool attack_adjacent() {
        const Vec2 me = game_.hero().a.pos;
        for (Vec2 d : directions8()) {
            const Monster* m = game_.monster_at(me + d);
            if (!m || !m->a.alive) continue;
            return act(Action{ActionType::Move, d, -1, {}});
        }
        return false;
    }

    /// Reads whichever scroll hurts everything nearby.
    ///
    /// Scrolls are area effects centred on the hero, so they are worth spending
    /// exactly when the hero is in trouble — several things close, or a
    /// guardian in the way. A bot that hoards consumables dies rich.
    bool read_attack_scroll() {
        const auto& inv = game_.hero().inv;
        for (std::size_t k = 0; k < inv.items.size(); ++k) {
            const Item& it = inv.items[k];
            if (it.kind != ItemKind::Scroll) continue;
            const ScrollKind kind = static_cast<ScrollKind>(it.subtype);
            if (kind != ScrollKind::Fireball && kind != ScrollKind::Lightning &&
                kind != ScrollKind::Frost)
                continue;
            if (act(Action{ActionType::UseItem, {}, static_cast<int>(k), {}})) return true;
        }
        return false;
    }

    /// Drinks whatever makes the next few turns hit harder, before a guardian.
    bool quaff_buff() {
        const auto& inv = game_.hero().inv;
        for (std::size_t k = 0; k < inv.items.size(); ++k) {
            const Item& it = inv.items[k];
            if (it.kind != ItemKind::Potion) continue;
            const PotionKind kind = static_cast<PotionKind>(it.subtype);
            if (kind != PotionKind::Might && kind != PotionKind::Haste) continue;
            if (game_.hero().a.has(kind == PotionKind::Might ? Effect::Might : Effect::Haste))
                continue;
            if (act(Action{ActionType::UseItem, {}, static_cast<int>(k), {}})) return true;
        }
        return false;
    }

    /// Casts Heal when there is no potion left. A ведун who never heals is a
    /// ведун who dies on the third floor with a full mana bar.
    bool cast_heal() {
        for (Spell s : game_.castable_spells())
            if (s == Spell::Heal)
                return act(Action{ActionType::CastSpell, {}, static_cast<int>(s), {}});
        return false;
    }

    /// Puts the ward up before walking into a guardian, if it is known.
    bool cast_ward() {
        if (game_.hero().a.has(Effect::Shield)) return false;
        for (Spell s : game_.castable_spells())
            if (s == Spell::Ward)
                return act(Action{ActionType::CastSpell, {}, static_cast<int>(s), {}});
        return false;
    }

    bool cast_something() {
        const auto spells = game_.castable_spells();
        if (spells.empty()) return false;
        // Heal is for the health branch, not for the shooting branch.
        std::vector<Spell> offensive;
        for (Spell s : spells)
            if (s != Spell::Heal && s != Spell::Ward) offensive.push_back(s);
        if (offensive.empty() || !policy_.chance(55)) return false;

        const Spell s = offensive[static_cast<std::size_t>(
            policy_.below(static_cast<int>(offensive.size())))];
        const auto targets = game_.spell_targets(s);
        if (spell_info(s).needs_target && targets.empty()) return false;
        const Vec2 aim = targets.empty() ? Vec2{-1, -1} : targets.front();
        return act(Action{ActionType::CastSpell, {}, static_cast<int>(s), aim});
    }

    /// Walks one step towards `goal`, falling back to a random shove when the
    /// path is blocked for long enough to look like a corner.
    bool walk_towards(Vec2 goal) {
        const Vec2 me = game_.hero().a.pos;
        if (me == goal) return false;
        if (stuck_ < 10) {
            const auto path = find_path(game_.map(), me, goal, 4000);
            if (!path.empty() && act(Action{ActionType::Move, path.front() - me, -1, {}}))
                return true;
        }
        const Vec2 dir = directions8()[static_cast<std::size_t>(policy_.below(8))];
        if (act(Action{ActionType::Move, dir, -1, {}})) return true;
        return act(Action{ActionType::Wait, {}, -1, {}});
    }

    /// The nearest item worth walking to. Loot is how a run survives the fourth
    /// belt, and the old bot only ever picked up what it happened to stand on.
    const Item* worth_fetching() const {
        const Item* best = nullptr;
        int best_d = 25;
        for (const Item& it : game_.floor_items()) {
            const int d = chebyshev(game_.hero().a.pos, it.pos);
            if (d < best_d) { best_d = d; best = &it; }
        }
        return best;
    }

    /// Кощей's needle: find it, take it, break it.
    ///
    /// Without this the fight has no end — he simply gets up again, and every
    /// sweep died on the twelfth floor with the last four never seen by
    /// anything. A mechanic that gates the rest of the game is exactly the
    /// mechanic a harness has to know, or the harness stops measuring at it.
    bool pursue_the_needle() {
        if (!game_.needle_intact()) return false;

        const Inventory& inv = game_.hero().inv;
        for (std::size_t k = 0; k < inv.items.size(); ++k)
            if (inv.items[k].kind == ItemKind::Needle &&
                act(Action{ActionType::UseItem, {}, static_cast<int>(k), {}}))
                return true;

        for (const Item& it : game_.floor_items()) {
            if (it.kind != ItemKind::Needle) continue;
            if (game_.hero().a.pos == it.pos)
                return act(Action{ActionType::PickUp, {}, -1, {}});
            return walk_towards(it.pos);
        }
        return false;
    }

    // --- the crossroads ---------------------------------------------------

    /// Takes the one thing on offer and goes down.
    ///
    /// The old bot walked straight past three free pieces of gear because its
    /// descent rule happened to fire on the first turn — which is exactly the
    /// sort of thing a harness hides until someone reads its numbers.
    bool crossroads_turn() {
        const Vec2 me = game_.hero().a.pos;
        if (game_.item_index_at(me) >= 0 && act(Action{ActionType::PickUp, {}, -1, {}}))
            return true;
        if (!game_.floor_items().empty()) {
            // Prefer a weapon: at level one the first fight is decided by damage.
            const Item* pick = &game_.floor_items().front();
            for (const Item& it : game_.floor_items())
                if (it.kind == ItemKind::Weapon) { pick = &it; break; }
            return walk_towards(pick->pos);
        }
        if (equip_upgrade()) return true;
        if (game_.map().at(me) == Tile::StairsDown &&
            act(Action{ActionType::Descend, {}, -1, {}}))
            return true;
        return walk_towards(game_.level().exit);
    }

    // --- one turn ---------------------------------------------------------

    bool step() {
        if (game_.in_lobby()) return crossroads_turn();

        const Vec2 me = game_.hero().a.pos;
        const int hp = hp_percent();

        // 1. Stay alive. A potion first, then distance, then — if nothing is
        //    chasing — sit still and let the slow regeneration work. Resting is
        //    what makes the difference between dying on the fourth floor and
        //    seeing the sixteenth, and it costs food, which is the real limit.
        if (hp <= kDrinkBelowPercent && drink_healing()) return true;
        if (hp <= kDrinkBelowPercent && cast_heal()) return true;
        if (hp <= kFleeBelowPercent && nearest_monster(2) && back_away()) return true;

        // Confused, the hero walks where the confusion says rather than where
        // the bot does, so swinging is a coin flip and stepping away is at
        // least an attempt. Blind is the same problem with the sight radius.
        const bool addled = game_.hero().a.has(Effect::Confusion) ||
                            game_.hero().a.has(Effect::Blind);
        if (addled && hp < kEngageBossPercent && nearest_monster(3) && back_away()) return true;

        const bool calm = threats_near(8) == 0;
        if (calm && hp < kRestUntilPercent && game_.hero().nutrition > kRestFoodFloor &&
            rested_ < kRestTurnBudget) {
            ++rested_;
            return act(Action{ActionType::Wait, {}, -1, {}});
        }

        if (game_.hero().nutrition < 350 && eat()) return true;

        // 2. Dress before fighting, not after.
        if (calm && equip_upgrade()) return true;

        // 3. Вий, specifically.
        if (dodge_gaze()) return true;

        // 4. Do not start a guardian while hurt: back off and heal first. This
        //    is the single change that made finishing a run possible at all.
        const Monster* boss = game_.active_boss();
        const bool boss_close = boss && chebyshev(me, boss->a.pos) <= species_of(*boss).sight;

        // Кощей does not stay down while the needle is whole, so breaking it
        // comes before hitting him — and before backing away from him. Put the
        // retreat first and the bot spends the fight walking backwards while he
        // gets up again, which is how the first three sweeps died.
        if (boss && std::strcmp(species_of(*boss).key, "koschei") == 0 && pursue_the_needle())
            return true;

        if (boss_close && hp < kBreakOffBossPercent) {
            if (drink_healing()) return true;
            if (cast_heal()) return true;
            if (back_away()) return true;
        }
        if (boss_close && hp >= kEngageBossPercent && cast_ward()) return true;

        // Spend the consumables where they matter: crowded, or a guardian.
        if ((threats_near(4) >= 3 || (boss_close && hp < kEngageBossPercent)) &&
            read_attack_scroll())
            return true;
        if (boss_close && hp >= kEngageBossPercent && quaff_buff()) return true;

        if (attack_adjacent()) return true;
        if (cast_something()) return true;

        // 5. Loot underfoot, then loot nearby.
        if (game_.item_index_at(me) >= 0 && act(Action{ActionType::PickUp, {}, -1, {}}))
            return true;

        // 6. Clear the floor, then take the stairs — but only in one piece.
        // A floor that has swallowed twice its budget is a stall, not a fight:
        // stop weighing health and food and simply leave. Without this, a run
        // could spend twenty thousand turns on the second floor and report
        // itself as "alive", which is a stall wearing a success's clothes.
        const bool bail_out = floor_turns_ > kFloorTurnBudget * 2;
        if (bail_out) {
            if (game_.map().at(me) == Tile::StairsDown &&
                act(Action{ActionType::Descend, {}, -1, {}}))
                return true;
            return walk_towards(game_.level().exit);
        }

        const bool floor_done = nearest_monster() == nullptr || floor_turns_ > kFloorTurnBudget;
        if (floor_done) {
            if (const Item* loot = worth_fetching()) return walk_towards(loot->pos);
            if (game_.map().at(me) == Tile::StairsDown) {
                if (hp < kEngageBossPercent && game_.hero().nutrition > kRestFoodFloor &&
                    rested_ < kRestTurnBudget) {
                    ++rested_;
                    return act(Action{ActionType::Wait, {}, -1, {}});
                }
                if (act(Action{ActionType::Descend, {}, -1, {}})) return true;
            }
            return walk_towards(game_.level().exit);
        }

        // 7. Otherwise go and find something to hit — the nearest thing that is
        //    not the floor's guardian while we are not ready for it.
        const Monster* target = nullptr;
        int best = 1 << 30;
        for (const auto& m : game_.monsters()) {
            if (!m.a.alive) continue;
            if (boss && &m == boss && hp < kEngageBossPercent) continue;
            const int d = dist_sq(me, m.a.pos);
            if (d < best) { best = d; target = &m; }
        }
        if (!target) {
            if (const Item* loot = worth_fetching()) return walk_towards(loot->pos);
            return walk_towards(game_.level().exit);
        }
        return walk_towards(target->a.pos);
    }

    // --- results ----------------------------------------------------------

    void finish() {
        const Hero& h = game_.hero();
        result_.turns = game_.turn();
        result_.depth = game_.depth();
        result_.deepest = h.deepest;
        result_.level = h.level;
        result_.max_hp = h.a.max_hp;
        result_.kills = h.kills;
        result_.gold = h.gold;
        result_.score = game_.score();
        result_.state = game_.state();
        result_.needle_broken = !game_.needle_intact();

        // Who finished the run off, read out of the log rather than tracked:
        // the engine already writes the cause into the death line.
        if (game_.state() == RunState::Dead) {
            for (auto it = game_.log().rbegin(); it != game_.log().rend(); ++it) {
                const std::string& line = it->text.en;
                const std::string marker = "You died. Cause: ";
                const std::size_t at = line.find(marker);
                if (at == std::string::npos) continue;
                result_.killed_by = line.substr(at + marker.size());
                if (!result_.killed_by.empty() && result_.killed_by.back() == '.')
                    result_.killed_by.pop_back();
                break;
            }
        }

        // A guardian counts as beaten when its floor holds no living copy of it
        // and the hero has been there. Read off the levels rather than tracked
        // during play, so it cannot drift from what actually happened.
        for (int depth = 1; depth <= kMaxDepth; ++depth) {
            const char* key = boss_for_depth(depth);
            if (!key || depth > h.deepest) continue;
            const int index = species_index(key);
            bool alive = false;
            for (const auto& m : game_.level_at(depth).monsters)
                if (m.a.alive && m.species == index) alive = true;
            if (!alive && game_.level_at(depth).generated) result_.bosses_slain.push_back(key);
        }

        const std::string blob = game_.save();
        result_.save_bytes = blob.size();
        Game restored;
        result_.save_round_trips = restored.load(blob) && restored.depth() == game_.depth() &&
                                  restored.turn() == game_.turn() &&
                                  restored.hero().a.hp == game_.hero().a.hp;
    }

    Game game_;
    BotMode mode_;
    Rng policy_;
    BotResult result_;
    int floor_turns_{0};
    int rested_{0};
    int stuck_{0};
};

}  // namespace

BotResult play_one(std::uint64_t seed, HeroClass cls, int max_turns, BotMode mode) {
    return Bot(seed, cls, mode).run(max_turns);
}

}  // namespace nav
