// SPDX-License-Identifier: MIT
//
// The eight guardians, on a clock.
//
// Each keeps the mechanic its turn-based self is built on, translated rather
// than dropped: Вий still has to lift his eyelids, Кощей still cannot die
// while his needle is whole, Баба-Яга still hides behind her huts. What
// changes is the currency. A turn-based warning said "next turn"; here it is
// a mark on the ground that fills up, and the answer is where the player
// stands when it does.
//
// The rule from the turn-based design stands unchanged: a mechanic the player
// cannot work out is not a puzzle, it is spite. Every attack that can really
// hurt is announced by a Telegraph first, for long enough to react to.
#include <algorithm>
#include <cmath>
#include <string>

#include "nav/rt.hpp"

namespace nav {
namespace rt {
namespace {

Vec2 cell_of(V p) {
    return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y))};
}

V centre(Vec2 c) { return {static_cast<float>(c.x) + 0.5f, static_cast<float>(c.y) + 0.5f}; }

float dist(V a, V b) { return length(a - b); }

constexpr float kPi = 3.14159265f;

/// Health on a clock. The bestiary's numbers were tuned for a hero who hits
/// once a turn; a crossbow on a clock does thirty a second, so the guardians
/// are sized here for a fight of half a minute rather than three seconds.
struct BossHealth {
    const char* key;
    int hp;
};
const BossHealth kHealth[] = {
    {species_key::kMara, 320},     {species_key::kViy, 650},
    {species_key::kVodyanoy, 520}, {species_key::kBabaYaga, 600},
    {species_key::kMorozko, 640},  {species_key::kKoschei, 520},
    {species_key::kPolozh, 700},   {species_key::kGorynych, 1150},
};

int boss_health(const char* key, int fallback) {
    for (const BossHealth& b : kHealth)
        if (std::string(b.key) == key) return b.hp;
    return fallback * 8;
}

/// Circling the hero at `range`, edging in or out to hold it.
V orbit(V self, V hero, float range, float strafe) {
    const V to = normalized(hero - self);
    const V around{-to.y * strafe, to.x * strafe};
    const float pull = std::clamp((dist(self, hero) - range) / 2.0f, -1.0f, 1.0f);
    return normalized(around + to * pull);
}

}  // namespace

Enemy& World::add_boss(int species, V pos, int room) {
    const Species& sp = bestiary()[static_cast<std::size_t>(species)];
    Enemy boss = make_enemy(species, pos, room);
    boss.role = Role::Boss;
    boss.max_hp = boss.hp = boss_health(sp.key, sp.hp);
    boss.phases = std::max(1, sp.phases);
    boss.radius = 0.55f;
    boss.damage = std::max(3, sp.attack / 2);
    boss.timer = 1.5f;   // a beat before the first attack, to see what it is
    boss.timer2 = 4.0f;
    const int id = boss.id;
    enemies_.push_back(boss);
    boss_id_ = id;

    // A walkable cell as near as possible to `want`, for what a guardian
    // brings with it.
    const auto stand = [this](V want) {
        Vec2 best{-1, -1};
        float best_d = 1e9f;
        const Vec2 c = cell_of(want);
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx) {
                const Vec2 p{c.x + dx, c.y + dy};
                if (walk_blocked(p) || p == exit_) continue;
                const float d = dist(centre(p), want);
                if (d < best_d) { best_d = d; best = p; }
            }
        return best.x >= 0 ? centre(best) : want;
    };

    if (species == species_index(species_key::kBabaYaga)) {
        // Three huts around her. While one stands she can barely be hurt.
        const int hut = species_index(species_key::kIzbushka);
        for (int k = 0; k < 3 && hut >= 0; ++k) {
            const float a = 2.0f * kPi * static_cast<float>(k) / 3.0f + 0.5f;
            Enemy h = make_enemy(hut, stand(pos + V{std::cos(a) * 4.0f, std::sin(a) * 4.0f}), room);
            h.role = Role::Hut;
            h.max_hp = h.hp = 110;
            h.radius = 0.45f;
            h.owner = id;
            h.timer = 3.0f + static_cast<float>(k);
            enemies_.push_back(h);
        }
    }
    if (species == species_index(species_key::kKoschei)) {
        // His death, on a needle, as far across his hall from him as it goes.
        // Hidden until he first rises: it is found, not guessed at.
        V spot = pos;
        float far = -1;
        const Rect area = room >= 0 ? rooms_[static_cast<std::size_t>(room)].interior
                                    : Rect{cell_of(pos).x - 8, cell_of(pos).y - 8, 17, 17};
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x) {
                const Vec2 p{x, y};
                if (walk_blocked(p) || p == exit_) continue;
                const float d = dist(centre(p), pos);
                if (d > far) { far = d; spot = centre(p); }
            }
        Enemy n = make_enemy(species, spot, room);
        n.role = Role::Needle;
        n.max_hp = n.hp = 60;
        n.radius = 0.32f;
        n.hidden = true;
        n.owner = id;
        enemies_.push_back(n);
    }

    for (Enemy& e : enemies_)
        if (e.id == id) return e;
    return enemies_.back();
}

void World::place_boss() {
    const char* key = boss_for_depth(depth_);
    if (key == nullptr) return;
    const int species = species_index(key);
    if (species < 0) return;

    const int r = room_of(exit_);
    if (r >= 0) {
        Room& room = rooms_[static_cast<std::size_t>(r)];
        room.boss = true;
        room.cleared = false;
        // The hall is the guardian's: ordinary guards make way.
        enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                                      [r](const Enemy& e) { return e.room == r; }),
                       enemies_.end());
        // Four pillars, where the hall is big enough for them. Cover is what
        // turns a gaze or a volley into something to play around rather than
        // something to endure — Вий cannot be fought without it.
        const Rect& in = room.interior;
        if (in.w >= 9 && in.h >= 7) {
            const Vec2 posts[] = {{in.left() + 2, in.top() + 2}, {in.right() - 2, in.top() + 2},
                                  {in.left() + 2, in.bottom() - 2}, {in.right() - 2, in.bottom() - 2}};
            for (Vec2 p : posts)
                if (p != exit_) map_.set(p, Tile::Wall);
        }
        Vec2 c = in.center();
        if (c == exit_ || walk_blocked(c)) {
            for (Vec2 d : directions8())
                if (!walk_blocked(c + d) && c + d != exit_) { c = c + d; break; }
        }
        add_boss(species, centre(c), r);
        return;
    }

    // Caves have no halls to shut. The guardian waits on open ground near the
    // stairs, and the stairs stay sealed while it lives.
    Vec2 best = exit_;
    for (int ring = 3; ring <= 6 && best == exit_; ++ring)
        for (Vec2 d : directions8()) {
            const Vec2 p{exit_.x + d.x * ring, exit_.y + d.y * ring};
            if (!walk_blocked(p)) { best = p; break; }
        }
    add_boss(species, centre(best), -1);
}

void World::think_boss(Enemy& e, float d, bool sees) {
    static const int kMara = species_index(species_key::kMara);
    static const int kViy = species_index(species_key::kViy);
    static const int kVodyanoy = species_index(species_key::kVodyanoy);
    static const int kYaga = species_index(species_key::kBabaYaga);
    static const int kHut = species_index(species_key::kIzbushka);
    static const int kMorozko = species_index(species_key::kMorozko);
    static const int kKoschei = species_index(species_key::kKoschei);
    static const int kPolozh = species_index(species_key::kPolozh);
    static const int kGorynych = species_index(species_key::kGorynych);

    // --- Waking --------------------------------------------------------------------
    if (!e.awake) {
        bool wakes = false;
        if (e.role == Role::Boss) {
            wakes = e.room >= 0 ? rooms_[static_cast<std::size_t>(e.room)].locked : (sees && d < 9.0f);
        } else {
            for (const Enemy& o : enemies_)
                if (o.id == e.owner && o.awake) { wakes = true; break; }
        }
        if (!wakes) return;
        e.awake = true;
        if (e.role == Role::Boss) emit(EventKind::BossAppears, e.pos, e.phases, e.species);
    }

    e.timer -= kDt;
    e.timer2 -= kDt;
    e.facing = V{hero_.pos.x >= e.pos.x ? 1.0f : -1.0f, 0};

    const auto walk = [&](V want, float pace) {
        move_circle(e.pos, (e.knock + want * (e.speed * pace)) * kDt, e.radius);
    };
    // Keeping a distance works only in sight of the hero: circling someone
    // behind a wall is walking into the wall. Out of sight, the way round.
    const auto hover = [&](float range, float pace) {
        walk(sees ? orbit(e.pos, hero_.pos, range, e.strafe) : steer(e), pace);
    };
    const auto warn = [&](TelegraphKind kind, float x, float y, float a, float b, float total) {
        Telegraph t;
        t.kind = kind;
        t.x = x;
        t.y = y;
        t.a = a;
        t.b = b;
        t.progress = std::clamp(1.0f - e.timer / total, 0.0f, 1.0f);
        telegraphs_.push_back(t);
    };
    const auto fan = [&](int count, float spread, float speed, int damage, bool ember) {
        const V aim = normalized(hero_.pos - e.pos);
        for (int i = 0; i < count; ++i) {
            const float off = count > 1 ? -spread * 0.5f + spread * static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
            shoot(e.pos, rotated(aim, off), speed, damage);
            bullets_.back().ember = ember;
        }
        emit(EventKind::EnemyShot, e.pos, 0, e.id);
    };
    // Somewhere to go: inside its own hall if it has one, never on the stairs.
    const auto somewhere = [&](float near, float far) {
        for (int tries = 0; tries < 40; ++tries) {
            const float a = static_cast<float>(rng_.unit()) * 2.0f * kPi;
            const float r = near + static_cast<float>(rng_.unit()) * (far - near);
            const V p = hero_.pos + V{std::cos(a) * r, std::sin(a) * r};
            const Vec2 c = cell_of(p);
            if (walk_blocked(c) || c == exit_) continue;
            if (e.room >= 0 && !rooms_[static_cast<std::size_t>(e.room)].interior.contains(c)) continue;
            return centre(c);
        }
        return e.pos;
    };

    // --- What guardians bring -----------------------------------------------------------
    if (e.role == Role::Needle) return;
    if (e.role == Role::Hut) {
        // A hut calls up an imp every few seconds, up to a small crowd.
        if (e.timer <= 0) {
            e.timer = 6.0f;
            int crowd = 0;
            for (const Enemy& o : enemies_)
                if (o.role == Role::Normal && o.owner == e.owner && o.hp > 0) ++crowd;
            const int imp = species_index("anchutka");
            if (crowd < 5 && imp >= 0) spawn_now(imp, e.pos + V{0.0f, 0.9f}, e.owner);
        }
        return;
    }
    if (e.role == Role::Illusion) {
        hover(5.5f, 0.8f);
        if (e.timer < 0.45f) warn(TelegraphKind::Ring, e.pos.x, e.pos.y, 0.8f, 0, 0.45f);
        if (e.timer <= 0) {
            e.timer = 2.6f;
            ring(e.pos, 6, 4.0f, std::max(1, e.damage / 2), static_cast<float>(rng_.unit()) * kPi);
        }
        return;
    }

    // --- Мара: морок --------------------------------------------------------------------
    //
    // Keeps her distance and fills the room with rings of slow bolts, slipping
    // to somewhere new every few seconds. Past half her health she splits:
    // two copies that throw thinner rings of their own. The answer is to read
    // the gaps in a ring, and to find the real one.
    if (e.species == kMara) {
        if (e.phase >= 2 && e.revives == 0) {
            e.revives = 1;
            for (int k = 0; k < 2; ++k) {
                Enemy& copy = spawn_now(e.species, somewhere(3.0f, 6.0f), e.id);
                copy.role = Role::Illusion;
                copy.max_hp = copy.hp = 24;
                copy.radius = 0.45f;
                copy.room = e.room;
                copy.timer = 1.2f + static_cast<float>(k);
            }
        }
        if (e.state == 2) {   // slipping away: the spot she will reappear on is marked
            warn(TelegraphKind::Spot, e.target.x, e.target.y, 0.9f, 0, 0.5f);
            if (e.timer <= 0) {
                e.pos = e.target;
                e.state = 0;
                e.timer = 1.2f;
                e.timer2 = 5.0f;
            }
            return;
        }
        hover(5.0f, 0.9f);
        if (e.state == 0 && e.timer2 <= 0) {
            e.state = 2;
            e.timer = 0.5f;
            e.target = somewhere(3.5f, 6.0f);
            return;
        }
        if (e.state == 0 && e.timer <= 0) {
            e.state = 1;
            e.timer = 0.55f;
        }
        if (e.state == 1) {
            warn(TelegraphKind::Ring, e.pos.x, e.pos.y, 1.3f, 0, 0.55f);
            if (e.timer <= 0) {
                const bool split = e.phase >= 2;
                ring(e.pos, split ? 16 : 12, 4.5f, e.damage, split && (e.revives++ % 2) ? kPi / 16.0f : 0.0f);
                e.state = 0;
                e.timer = split ? 1.7f : 2.2f;
            }
        }
    }

    // --- Вий: «поднимите мне веки» -----------------------------------------------------
    //
    // Eyelids down, he shuffles and is easy to hurt. Then he lifts them: a
    // line from him to the hero fills for a second, following wherever they
    // go — sidestepping does not help, only putting a wall between them does.
    // What he sees when it fills, he strikes and blinds.
    else if (e.species == kViy) {
        static const float kShut[] = {0.0f, 3.0f, 2.2f, 1.5f};
        static const float kLift[] = {0.0f, 1.2f, 1.0f, 0.85f};
        const int ph = std::clamp(e.phase, 1, 3);
        switch (e.state) {
            case 0:
                e.armour = 1.5f;   // eyes shut: the moment to hit him
                walk(steer(e), 0.6f);
                if (e.timer <= 0) {
                    e.state = 1;
                    e.timer = kLift[ph];
                }
                break;
            case 1:
                e.armour = 1.0f;
                walk({0, 0}, 0);
                warn(TelegraphKind::Line, e.pos.x, e.pos.y, hero_.pos.x, hero_.pos.y, kLift[ph]);
                if (e.timer <= 0) {
                    if (sees_hero_from(e.pos)) {
                        hurt_hero(ph >= 3 ? 7 : 10);
                        hero_.blind = ph >= 3 ? 1.2f : 2.0f;
                        emit(EventKind::GazeHit, hero_.pos, 0, e.id);
                    } else {
                        // Told, because ducking behind a pillar in time is
                        // playing the fight right, and deserves to be known.
                        emit(EventKind::GazeMissed, e.pos, 0, e.id);
                    }
                    e.state = 2;
                    e.timer = 0.45f;
                }
                break;
            default:
                walk({0, 0}, 0);
                if (e.timer <= 0) {
                    e.state = 0;
                    e.timer = kShut[ph];
                }
                break;
        }
    }

    // --- Водяной ----------------------------------------------------------------------
    //
    // Mends while he stands in water, so the fight is about keeping him out of
    // it. Past half his health he floods the ground under the hero — a marked
    // circle turns to water a moment later — which slows them and gives him
    // somewhere new to heal.
    else if (e.species == kVodyanoy) {
        walk(steer(e), 0.8f);
        if (map_.at(cell_of(e.pos)) == Tile::Water && e.hp < e.max_hp && e.timer2 <= 0) {
            e.timer2 = 0.25f;
            e.hp = std::min(e.max_hp, e.hp + 4);
        }
        if (sees && e.fire_cd <= 0) {
            e.fire_cd = 1.8f;
            fan(3, 0.4f, 5.0f, e.damage, false);
        }
        if (e.phase >= 2) {
            if (e.state == 0 && e.timer <= 0) {
                e.state = 1;
                e.timer = 0.9f;
                e.target = hero_.pos;
            }
            if (e.state == 1) {
                warn(TelegraphKind::Spot, e.target.x, e.target.y, 1.8f, 0, 0.9f);
                if (e.timer <= 0) {
                    const Vec2 c = cell_of(e.target);
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx) {
                            const Vec2 p{c.x + dx, c.y + dy};
                            if (map_.in_bounds(p) && map_.at(p) == Tile::Floor &&
                                dist(centre(p), e.target) <= 1.8f)
                                map_.set(p, Tile::Water);
                        }
                    hero_cell_ = {-1, -1};   // the floor changed: the page redraws it
                    refresh_view();
                    e.state = 0;
                    e.timer = 3.2f;
                }
            }
        }
    }

    // --- Баба-Яга: пока стоит изба -------------------------------------------------------
    //
    // While a hut stands, a blow on her lands at a fifth of its weight; the
    // huts meanwhile call up imps. The fight is the huts first. From her
    // second phase she rides the mortar, faster; in her third she raises one
    // more hut, once.
    else if (e.species == kYaga) {
        int huts = 0;
        for (const Enemy& o : enemies_)
            if (o.role == Role::Hut && o.owner == e.id && o.hp > 0) ++huts;
        for (const Enemy& o : spawned_)
            if (o.role == Role::Hut && o.owner == e.id) ++huts;
        e.armour = huts > 0 ? 0.2f : 1.0f;
        hover(5.5f, e.phase >= 2 ? 1.4f : 1.0f);
        if (e.phase >= 3 && e.revives == 0 && kHut >= 0) {
            e.revives = 1;
            Enemy& h = spawn_now(kHut, somewhere(2.0f, 4.0f), e.id);
            h.role = Role::Hut;
            h.max_hp = h.hp = 110;
            h.radius = 0.45f;
            h.timer = 2.0f;
        }
        if (e.state == 1) {   // the mortar spins up before the ring goes out
            warn(TelegraphKind::Ring, e.pos.x, e.pos.y, 1.0f, 0, 0.6f);
            if (e.timer <= 0) {
                e.state = 0;
                ring(e.pos, 10, 5.0f, e.damage, 0);
                fan(1, 0, 7.0f, e.damage, false);
            }
        } else if (sees && e.fire_cd <= 0) {
            if (e.phase == 1) {
                e.fire_cd = 1.5f;
                fan(1, 0, 6.0f, e.damage, false);
            } else if (e.phase == 2) {
                e.fire_cd = 1.3f;
                fan(3, 0.35f, 6.0f, e.damage, false);
            } else {
                e.fire_cd = 2.2f;
                e.state = 1;
                e.timer = 0.6f;
            }
            if (rng_.chance(30)) e.strafe = -e.strafe;
        }
    }

    // --- Морозко: «тепло ли тебе?» -------------------------------------------------------
    //
    // Throws fans of ice, and every few seconds stops to draw breath: a ring
    // widens around him, and whoever is inside it when it closes is frozen to
    // the spot. Standing still is the mistake.
    else if (e.species == kMorozko) {
        const float reach = 3.4f;
        if (e.state == 0) {
            walk(steer(e), 0.8f);
            if (sees && e.fire_cd <= 0) {
                e.fire_cd = 1.4f;
                fan(5, 0.8f, 7.0f, std::max(1, e.damage - 2), false);
            }
            if (e.timer <= 0) {
                e.state = 1;
                e.timer = 1.2f;
            }
        } else {
            walk({0, 0}, 0);
            warn(TelegraphKind::Ring, e.pos.x, e.pos.y, reach, 0, 1.2f);
            if (e.timer <= 0) {
                if (dist(hero_.pos, e.pos) < reach) {
                    hurt_hero(6);
                    hero_.frozen = e.phase >= 2 ? 1.4f : 1.0f;
                    emit(EventKind::Frozen, hero_.pos, 0, e.id);
                }
                e.state = 0;
                e.timer = e.phase >= 2 ? 2.5f : 3.5f;
            }
        }
    }

    // --- Кощей Бессмертный ------------------------------------------------------------
    //
    // A line from him to the hero, then he runs along it. Knocked down, he
    // rises at three-fifths of his health — as long as his needle is whole.
    // The needle shows itself the first time he rises; breaking it is the
    // only way the fight ends. (The rising itself is in World::kill_enemy.)
    else if (e.species == kKoschei) {
        if (e.state == 0) {
            walk(steer(e), 0.9f);
            if (e.timer <= 0) {
                e.state = 1;
                e.timer = 0.7f;
                e.target = hero_.pos;
            }
        } else if (e.state == 1) {
            walk({0, 0}, 0);
            warn(TelegraphKind::Line, e.pos.x, e.pos.y, e.target.x, e.target.y, 0.7f);
            if (e.timer <= 0) {
                e.state = 2;
                e.timer = 0.55f;
            }
        } else {
            const V before = e.pos;
            move_circle(e.pos, normalized(e.target - e.pos) * (9.0f * kDt), e.radius);
            if (e.timer <= 0 || length(e.pos - before) < 1e-3f || dist(e.pos, e.target) < 0.3f) {
                e.state = 0;
                e.timer = e.phase >= 2 ? 2.2f : 3.0f;
            }
        }
    }

    // --- Полоз ------------------------------------------------------------------------
    //
    // Scorches what he touches. Every few seconds he goes into the stone and
    // comes after the hero from underneath; then a circle is marked where he
    // will break out, and he does, throwing a ring of rock. Distance stops
    // being safety — keeping on the move is.
    else if (e.species == kPolozh) {
        if (e.state == 0) {
            e.hidden = false;
            walk(steer(e), 1.0f);
            if (e.timer <= 0) {
                e.state = 1;
                e.timer = 1.0f;
                e.hidden = true;
                emit(EventKind::Burrowed, e.pos, 0, e.id);
            }
        } else if (e.state == 1) {
            // Underground the rock is no obstacle.
            e.pos = e.pos + normalized(hero_.pos - e.pos) * (5.0f * kDt);
            if (e.timer <= 0) {
                e.state = 2;
                e.timer = 0.8f;
                e.target = hero_.pos;
            }
        } else {
            warn(TelegraphKind::Spot, e.target.x, e.target.y, 1.4f, 0, 0.8f);
            if (e.timer <= 0) {
                e.pos = e.target;
                move_circle(e.pos, {0, 0}, e.radius);
                e.hidden = false;
                emit(EventKind::Emerged, e.pos, 0, e.id);
                if (dist(hero_.pos, e.target) < 1.4f) hurt_hero(9);
                ring(e.pos, e.phase >= 2 ? 12 : 8, 5.0f, std::max(1, e.damage / 2));
                e.state = 0;
                e.timer = e.phase >= 2 ? 2.8f : 4.0f;
            }
        }
        if (!e.hidden && d < hero_.radius + e.radius + 0.1f) hero_.burning = std::max(hero_.burning, 3.0f);
    }

    // --- Змей Горыныч ------------------------------------------------------------------
    //
    // A cone is marked where he means to breathe, then he breathes: one stream
    // of fire per head still on his shoulders. Fewer heads, faster fire; from
    // his second phase the fire that hits the walls keeps burning on the
    // ground; on his last head he charges as well.
    else if (e.species == kGorynych) {
        const int heads = std::max(1, 4 - e.phase);
        switch (e.state) {
            case 0:
                walk(steer(e), 0.8f);
                if (e.timer <= 0) {
                    e.state = 1;
                    e.timer = 0.9f;
                    const V to = hero_.pos - e.pos;
                    e.angle = std::atan2(to.y, to.x);   // locked: the player can step out of it
                }
                break;
            case 1:
                walk({0, 0}, 0);
                warn(TelegraphKind::Cone, e.pos.x, e.pos.y, e.angle,
                     0.25f + 0.2f * static_cast<float>(heads - 1), 0.9f);
                if (e.timer <= 0) {
                    e.state = 2;
                    e.timer = 1.2f;
                    e.timer2 = 0;
                }
                break;
            case 2:
                walk({0, 0}, 0);
                if (e.timer2 <= 0) {
                    e.timer2 = heads == 1 ? 0.05f : 0.08f;
                    for (int h = 0; h < heads; ++h) {
                        const float spread = (static_cast<float>(h) - static_cast<float>(heads - 1) * 0.5f) * 0.35f;
                        const float jitter = (static_cast<float>(rng_.unit()) - 0.5f) * 0.12f;
                        const float a = e.angle + spread + jitter;
                        shoot(e.pos, V{std::cos(a), std::sin(a)}, 7.0f, std::max(1, e.damage / 2), 0.18f);
                        bullets_.back().ember = e.phase >= 2;
                    }
                }
                if (e.timer <= 0) {
                    if (e.phase >= 3) {
                        e.state = 3;
                        e.timer = 0.6f;
                        e.target = hero_.pos;
                    } else {
                        e.state = 0;
                        e.timer = e.phase >= 2 ? 2.2f : 2.8f;
                    }
                }
                break;
            case 3:
                walk({0, 0}, 0);
                warn(TelegraphKind::Line, e.pos.x, e.pos.y, e.target.x, e.target.y, 0.6f);
                if (e.timer <= 0) {
                    e.state = 4;
                    e.timer = 0.5f;
                }
                break;
            default: {
                const V before = e.pos;
                move_circle(e.pos, normalized(e.target - e.pos) * (10.0f * kDt), e.radius);
                if (e.timer <= 0 || length(e.pos - before) < 1e-3f || dist(e.pos, e.target) < 0.3f) {
                    e.state = 0;
                    e.timer = 1.8f;
                }
                break;
            }
        }
    } else {
        // A guardian with no clock-mechanic of its own still fights.
        walk(steer(e), 0.9f);
    }

    // Touching any guardian hurts.
    if (!e.hidden && d < hero_.radius + e.radius + 0.08f && e.attack_cd <= 0) {
        e.attack_cd = 1.0f;
        hurt_hero(e.damage);
    }
}

}  // namespace rt
}  // namespace nav
