// SPDX-License-Identifier: MIT
//
// Running and auto-exploring.
//
// Sixteen floors crossed one keypress at a time is the single most tiring thing
// about this game, and it is not tiring in an interesting way: nobody makes a
// decision while walking down an empty corridor. So the engine learns to take
// several steps at once — and, much more importantly, learns exactly when to
// stop, because a travel command that walks the hero into a fight is worse than
// no travel command at all.
//
// Both commands are built out of ordinary moves. The rules do not gain a new
// way to walk; they simply stop being asked one step at a time. That means
// every trap, every effect, every monster turn and every invariant holds during
// a run exactly as it does during a walk, which is why this file is short.
#include <algorithm>
#include <vector>

#include "nav/game.hpp"
#include "nav/pathfind.hpp"

namespace nav {
namespace {

/// A run is bounded so that a bug in a stop rule shows up as a hero who walked
/// too far, not as a browser tab that never comes back.
constexpr int kRunSteps = 80;
constexpr int kExploreSteps = 220;

}  // namespace

bool Game::foe_in_view() const {
    for (const Monster& m : monsters())
        if (m.a.alive && map().visible(m.a.pos)) return true;
    return false;
}

Game::TravelWatch Game::travel_watch() const {
    TravelWatch w;
    w.hp = hero_.a.hp;
    w.depth = depth_;
    for (int e = 0; e < static_cast<int>(Effect::Count); ++e)
        if (hero_.a.has(static_cast<Effect>(e))) w.effects |= 1u << e;
    return w;
}

bool Game::travel_should_stop(const TravelWatch& before) const {
    if (state_ != RunState::Playing) return true;
    if (depth_ != before.depth) return true;
    if (hero_.a.hp < before.hp) return true;
    if (foe_in_view()) return true;

    // A new effect is news whether it is good or bad: something happened, and
    // the player should be the one deciding what to do about it.
    const TravelWatch now = travel_watch();
    if ((now.effects & ~before.effects) != 0) return true;

    // Anything worth stopping for underfoot.
    const Tile t = map().at(hero_.a.pos);
    if (t == Tile::StairsDown || t == Tile::StairsUp || t == Tile::Altar) return true;
    if (item_index_at(hero_.a.pos) >= 0) return true;
    return false;
}

int Game::open_neighbours(Vec2 p) const {
    int n = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            if (map().walkable(Vec2{p.x + dx, p.y + dy})) ++n;
        }
    return n;
}

bool Game::act_run(Vec2 dir) {
    if (dir.x == 0 && dir.y == 0) return false;

    // With someone in sight, a run is one step. That is not a refusal: stepping
    // into a monster is how this game attacks, so Shift and a direction still
    // does the obvious thing when the obvious thing is to hit something.
    if (foe_in_view()) return perform_single(Action{ActionType::Move, dir, -1, {}});

    // Corridors and rooms want different rules. In a corridor the interesting
    // event is a side passage opening up; in a room there are openings
    // everywhere and the rule would stop the run before it started.
    const bool in_corridor = open_neighbours(hero_.a.pos) <= 2;

    int steps = 0;
    bool moved = false;
    while (steps < kRunSteps) {
        const Vec2 next{hero_.a.pos.x + dir.x, hero_.a.pos.y + dir.y};
        if (!map().walkable(next)) break;
        // A closed door ends a run rather than being shouldered open at speed.
        if (map().at(next) == Tile::Door) break;

        const TravelWatch before = travel_watch();
        if (!perform_single(Action{ActionType::Move, dir, -1, {}})) break;
        moved = true;
        ++steps;
        if (travel_should_stop(before)) break;

        if (in_corridor) {
            // Stop *on* the junction, not one square short of it. The test is
            // the two cells square across the direction of travel: a corridor
            // that opens to the side is a choice the player has to make, while
            // a diagonal gap glimpsed a step early is just the same junction
            // seen from the previous cell.
            const Vec2 left{hero_.a.pos.x - dir.y, hero_.a.pos.y + dir.x};
            const Vec2 right{hero_.a.pos.x + dir.y, hero_.a.pos.y - dir.x};
            const bool side_opening = map().walkable(left) || map().walkable(right);
            if (side_opening) break;
        }
    }
    return moved;
}

std::vector<Vec2> Game::explore_frontier() const {
    // A frontier cell is one the hero has seen, standing next to one they have
    // not. Pathing only over explored ground is the honest version of
    // auto-explore: the hero walks towards the edge of what they know rather
    // than through walls they have never laid eyes on.
    std::vector<Vec2> out;
    const Map& m = map();
    for (int y = 0; y < m.height(); ++y)
        for (int x = 0; x < m.width(); ++x) {
            const Vec2 p{x, y};
            if (!m.explored(p) || !m.walkable(p)) continue;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const Vec2 c{x + dx, y + dy};
                    if (!m.in_bounds(c) || m.explored(c)) continue;
                    if (m.at(c) == Tile::Wall) continue;
                    out.push_back(p);
                    dy = 2;
                    break;
                }
        }
    return out;
}

bool Game::act_explore() {
    if (foe_in_view()) {
        message(Text{"Не до прогулок — рядом кто-то есть.",
                     "No wandering with company in sight."}, Severity::Bad);
        return false;
    }

    int steps = 0;
    bool moved = false;
    while (steps < kExploreSteps) {
        std::vector<Vec2> goals = explore_frontier();
        bool to_stairs = false;
        if (goals.empty()) {
            // Nothing left to find. The useful thing to do next is almost
            // always to walk to the stairs, so the command does that instead of
            // shrugging.
            const Vec2 down = level().exit;
            if (!map().explored(down) || (hero_.a.pos.x == down.x && hero_.a.pos.y == down.y)) break;
            goals.push_back(down);
            to_stairs = true;
        }

        // One Dijkstra map over explored ground answers "which goal is nearest
        // and which way is it" in one pass, however many goals there are.
        const auto passable = [&](Vec2 p) {
            return map().walkable(p) && map().explored(p) &&
                   (monster_at(p) == nullptr || (p.x == hero_.a.pos.x && p.y == hero_.a.pos.y));
        };
        DijkstraMap flow;
        flow.build(map(), goals, passable);
        if (flow.at(hero_.a.pos) >= DijkstraMap::kUnreachable) break;

        const Vec2 step = flow.best_step(hero_.a.pos, passable, true);
        if (step.x == hero_.a.pos.x && step.y == hero_.a.pos.y) break;

        const TravelWatch before = travel_watch();
        if (!perform_single(Action{ActionType::Move,
                                   Vec2{step.x - hero_.a.pos.x, step.y - hero_.a.pos.y}, -1, {}}))
            break;
        moved = true;
        ++steps;
        if (travel_should_stop(before)) break;
        if (to_stairs && map().at(hero_.a.pos) == Tile::StairsDown) break;
    }

    if (!moved)
        message(Text{"Здесь всё исхожено.", "Nothing left here to find."}, Severity::Info);
    return moved;
}

}  // namespace nav
