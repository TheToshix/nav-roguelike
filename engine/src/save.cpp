// SPDX-License-Identifier: MIT
//
// Save format.
//
// The state is written as whitespace-separated tokens with a version header,
// which keeps a save file diffable and makes a corrupt one easy to inspect by
// hand. Tile and explored grids are run-length encoded, which is what keeps a
// twelve-floor save inside a browser's localStorage quota.
//
// Loading is strict: any malformed token aborts the load and leaves the caller
// with an untouched Game, so a truncated or tampered save can never put the
// engine into a half-initialised state.
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "nav/game.hpp"

namespace nav {
namespace {

constexpr const char* kMagic = "NAV";
// Version 2 added the zones, the three extra hero classes, the boss state
// (Вий's eyelids, Кощей's revivals) and the broken-needle flag. Version 4 adds
// the guardians' halls: a floor now remembers where its arena is and whether
// its doors have closed, and a save without that would reopen a sealed fight.
// Version 5 adds the burning cells the Огневик trails behind it: a floor now
// remembers where it is on fire and for how long, so a save taken mid-fight
// resumes with the same hazard on the ground. Version 6 adds the codex: which
// bestiary rows the hero has unlocked by sight, so the screen's progress
// survives a reload.
// There is no migration: an older save is refused rather than loaded as
// something it is not — see docs/TEST_CASES.md, "what stayed unchecked".
constexpr int kFormatVersion = 6;

/// Escapes a string into a single whitespace-free token.
std::string encode_string(const std::string& s) {
    if (s.empty()) return "~";
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        if (c == '\\' || c == '~' || c <= ' ') {
            static const char* hex = "0123456789abcdef";
            out += '\\';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

bool decode_string(const std::string& token, std::string& out) {
    out.clear();
    if (token == "~") return true;
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (token[i] != '\\') { out += token[i]; continue; }
        if (i + 2 >= token.size()) return false;
        const auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        const int hi = digit(token[i + 1]), lo = digit(token[i + 2]);
        if (hi < 0 || lo < 0) return false;
        out += static_cast<char>(hi * 16 + lo);
        i += 2;
    }
    return true;
}

class Writer {
public:
    template <typename T>
    Writer& operator<<(const T& v) { os_ << v << ' '; return *this; }
    Writer& str(const std::string& s) { os_ << encode_string(s) << ' '; return *this; }
    /// Run-length encodes a byte-sized sequence as "count:value" pairs.
    template <typename T>
    Writer& rle(const std::vector<T>& data) {
        os_ << data.size() << ' ';
        std::size_t i = 0;
        while (i < data.size()) {
            std::size_t run = 1;
            while (i + run < data.size() && data[i + run] == data[i]) ++run;
            os_ << run << ':' << static_cast<int>(data[i]) << ' ';
            i += run;
        }
        os_ << "| ";
        return *this;
    }
    std::string take() { return os_.str(); }

private:
    std::ostringstream os_;
};

class Reader {
public:
    explicit Reader(const std::string& blob) : is_(blob) {}

    bool ok() const { return ok_; }

    template <typename T>
    Reader& operator>>(T& v) {
        if (ok_ && !(is_ >> v)) ok_ = false;
        return *this;
    }

    Reader& str(std::string& out) {
        std::string token;
        if (!ok_) return *this;
        if (!(is_ >> token) || !decode_string(token, out)) ok_ = false;
        return *this;
    }

    template <typename T>
    Reader& rle(std::vector<T>& out, std::size_t max_elements) {
        if (!ok_) return *this;
        std::size_t total = 0;
        if (!(is_ >> total) || total > max_elements) { ok_ = false; return *this; }
        out.clear();
        out.reserve(total);
        std::string token;
        while (out.size() < total) {
            if (!(is_ >> token)) { ok_ = false; return *this; }
            const std::size_t colon = token.find(':');
            if (colon == std::string::npos) { ok_ = false; return *this; }
            long long run = 0, value = 0;
            try {
                run = std::stoll(token.substr(0, colon));
                value = std::stoll(token.substr(colon + 1));
            } catch (...) { ok_ = false; return *this; }
            if (run <= 0 || out.size() + static_cast<std::size_t>(run) > total) {
                ok_ = false;
                return *this;
            }
            out.insert(out.end(), static_cast<std::size_t>(run), static_cast<T>(value));
        }
        if (!(is_ >> token) || token != "|") ok_ = false;  // terminator
        return *this;
    }

    /// Reads a bounded count, guarding against a hostile save claiming a
    /// billion monsters and exhausting memory before anything is validated.
    bool count(std::size_t& n, std::size_t max_allowed) {
        if (!ok_) return false;
        if (!(is_ >> n) || n > max_allowed) { ok_ = false; return false; }
        return true;
    }

private:
    std::istringstream is_;
    bool ok_{true};
};

void write_actor(Writer& w, const Actor& a) {
    w << a.pos.x << a.pos.y << a.hp << a.max_hp << a.attack << a.defence << a.speed
      << a.energy << (a.alive ? 1 : 0) << a.effects.size();
    for (const auto& e : a.effects) w << static_cast<int>(e.kind) << e.turns << e.power;
}

bool read_actor(Reader& r, Actor& a) {
    int alive = 0;
    std::size_t n = 0;
    r >> a.pos.x >> a.pos.y >> a.hp >> a.max_hp >> a.attack >> a.defence >> a.speed >>
        a.energy >> alive;
    if (!r.ok() || !r.count(n, 64)) return false;
    a.alive = alive != 0;
    a.effects.clear();
    for (std::size_t i = 0; i < n; ++i) {
        int kind = 0, turns = 0, power = 0;
        r >> kind >> turns >> power;
        if (!r.ok() || kind < 0 || kind >= static_cast<int>(Effect::Count)) return false;
        a.effects.push_back(ActiveEffect{static_cast<Effect>(kind), turns, power});
    }
    return r.ok();
}

void write_item(Writer& w, const Item& it) {
    w << static_cast<int>(it.kind) << it.subtype << it.power << it.enchant << it.count
      << (it.identified ? 1 : 0) << it.pos.x << it.pos.y;
}

bool read_item(Reader& r, Item& it) {
    int kind = 0, ident = 0;
    r >> kind >> it.subtype >> it.power >> it.enchant >> it.count >> ident >> it.pos.x >> it.pos.y;
    if (!r.ok() || kind < 0 || kind > static_cast<int>(ItemKind::Feather)) return false;
    if (it.count < 0 || it.count > 1000000) return false;
    it.kind = static_cast<ItemKind>(kind);
    it.identified = ident != 0;
    return true;
}

}  // namespace

std::string Game::save() const {
    Writer w;
    w << kMagic << kFormatVersion;

    // --- Run configuration -------------------------------------------------
    w << cfg_.seed;
    w.str(cfg_.seed_text);
    w << static_cast<int>(cfg_.hero_class) << cfg_.map_width << cfg_.map_height;

    // The generator state is part of the save: resuming must continue the same
    // random sequence, or a replayed run diverges from the reported one.
    const std::uint64_t* rs = rng_.state();
    w << rs[0] << rs[1] << rs[2] << rs[3];

    w << depth_ << turn_ << static_cast<int>(state_) << (needle_broken_ ? 1 : 0);

    // --- Hero --------------------------------------------------------------
    write_actor(w, hero_.a);
    w << static_cast<int>(hero_.cls) << hero_.mana << hero_.max_mana << hero_.level
      << hero_.xp << hero_.gold << hero_.sight << hero_.nutrition << hero_.kills
      << hero_.deepest << hero_.ward_ready;

    w << hero_.spells.size();
    for (std::uint8_t s : hero_.spells) w << static_cast<int>(s);

    w << hero_.inv.items.size();
    for (const auto& it : hero_.inv.items) write_item(w, it);
    w << hero_.inv.weapon << hero_.inv.armor << hero_.inv.amulet;

    // --- Item identification ----------------------------------------------
    w << ident_.potion_look.size();
    for (int v : ident_.potion_look) w << v;
    w << ident_.scroll_look.size();
    for (int v : ident_.scroll_look) w << v;
    w.rle(ident_.potion_known);
    w.rle(ident_.scroll_known);

    // --- Codex -----------------------------------------------------------
    w.rle(codex_seen_);

    // --- Levels ------------------------------------------------------------
    w << levels_.size();
    for (const auto& lvl : levels_) {
        w << (lvl.generated ? 1 : 0);
        if (!lvl.generated) continue;

        w << lvl.map.width() << lvl.map.height() << lvl.entrance.x << lvl.entrance.y
          << lvl.exit.x << lvl.exit.y << (lvl.boss_slain ? 1 : 0);

        const Arena& ar = lvl.arena;
        w << (ar.exists ? 1 : 0);
        if (ar.exists)
            w << ar.min.x << ar.min.y << ar.max.x << ar.max.y << ar.door.x << ar.door.y
              << (ar.sealed ? 1 : 0) << (ar.warned ? 1 : 0) << (ar.seals ? 1 : 0);

        std::vector<std::uint8_t> tiles;
        tiles.reserve(lvl.map.raw_tiles().size());
        for (Tile t : lvl.map.raw_tiles()) tiles.push_back(static_cast<std::uint8_t>(t));
        w.rle(tiles);
        w.rle(lvl.map.raw_explored());

        w << lvl.monsters.size();
        for (const auto& m : lvl.monsters) {
            write_actor(w, m.a);
            w << m.species << (m.awake ? 1 : 0) << m.last_seen.x << m.last_seen.y
              << m.search_turns << m.summon_cooldown << m.charge << m.revives << m.phase;
        }

        w << lvl.items.size();
        for (const auto& it : lvl.items) write_item(w, it);

        w << lvl.embers.size();
        for (const auto& e : lvl.embers) w << e.pos.x << e.pos.y << e.turns;
    }

    return w.take();
}

bool Game::load(const std::string& blob) {
    Reader r(blob);

    std::string magic;
    int version = 0;
    r >> magic >> version;
    if (!r.ok() || magic != kMagic || version != kFormatVersion) return false;

    // Everything is decoded into a scratch Game first, so a failure part-way
    // through leaves the live object untouched.
    Game g;

    r >> g.cfg_.seed;
    r.str(g.cfg_.seed_text);
    int hero_class = 0;
    r >> hero_class >> g.cfg_.map_width >> g.cfg_.map_height;
    if (!r.ok() || hero_class < 0 || hero_class >= static_cast<int>(HeroClass::Count)) return false;
    if (g.cfg_.map_width <= 0 || g.cfg_.map_width > 512) return false;
    if (g.cfg_.map_height <= 0 || g.cfg_.map_height > 512) return false;
    g.cfg_.hero_class = static_cast<HeroClass>(hero_class);

    std::uint64_t rs[4]{};
    r >> rs[0] >> rs[1] >> rs[2] >> rs[3];
    if (!r.ok()) return false;
    g.rng_.set_state(rs);

    int run_state = 0, needle = 0;
    r >> g.depth_ >> g.turn_ >> run_state >> needle;
    if (!r.ok() || run_state < 0 || run_state > static_cast<int>(RunState::Ascended)) return false;
    g.needle_broken_ = needle != 0;
    if (g.depth_ < kLobbyDepth || g.depth_ > kMaxDepth) return false;
    g.state_ = static_cast<RunState>(run_state);

    if (!read_actor(r, g.hero_.a)) return false;
    int cls = 0;
    r >> cls >> g.hero_.mana >> g.hero_.max_mana >> g.hero_.level >> g.hero_.xp >>
        g.hero_.gold >> g.hero_.sight >> g.hero_.nutrition >> g.hero_.kills >> g.hero_.deepest >>
        g.hero_.ward_ready;
    if (!r.ok() || cls < 0 || cls >= static_cast<int>(HeroClass::Count)) return false;
    g.hero_.cls = static_cast<HeroClass>(cls);

    std::size_t n = 0;
    if (!r.count(n, 64)) return false;
    g.hero_.spells.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        int v = 0;
        r >> v;
        g.hero_.spells[i] = v ? 1 : 0;
    }

    if (!r.ok() || !r.count(n, Inventory::kCapacity)) return false;
    g.hero_.inv.items.resize(n);
    for (auto& it : g.hero_.inv.items)
        if (!read_item(r, it)) return false;
    r >> g.hero_.inv.weapon >> g.hero_.inv.armor >> g.hero_.inv.amulet;
    if (!r.ok()) return false;
    const int slots = static_cast<int>(g.hero_.inv.items.size());
    for (int* slot : {&g.hero_.inv.weapon, &g.hero_.inv.armor, &g.hero_.inv.amulet})
        if (*slot < -1 || *slot >= slots) return false;

    auto read_look = [&](std::vector<int>& look, std::size_t limit) {
        std::size_t count = 0;
        if (!r.count(count, limit)) return false;
        look.assign(count, 0);
        for (auto& v : look) r >> v;
        return r.ok();
    };
    if (!read_look(g.ident_.potion_look, 64)) return false;
    if (!read_look(g.ident_.scroll_look, 64)) return false;
    r.rle(g.ident_.potion_known, 64);
    r.rle(g.ident_.scroll_known, 64);
    if (!r.ok()) return false;

    r.rle(g.codex_seen_, bestiary().size());
    if (!r.ok() || g.codex_seen_.size() != bestiary().size()) return false;

    if (!r.count(n, static_cast<std::size_t>(kMaxDepth) + 1)) return false;
    g.levels_.assign(n, Level{});
    for (auto& lvl : g.levels_) {
        int generated = 0;
        r >> generated;
        if (!r.ok()) return false;
        if (!generated) continue;
        lvl.generated = true;

        int w = 0, h = 0, boss = 0;
        r >> w >> h >> lvl.entrance.x >> lvl.entrance.y >> lvl.exit.x >> lvl.exit.y >> boss;
        if (!r.ok() || w <= 0 || h <= 0 || w > 512 || h > 512) return false;
        lvl.boss_slain = boss != 0;

        int has_arena = 0;
        r >> has_arena;
        if (!r.ok()) return false;
        if (has_arena) {
            Arena ar;
            int sealed = 0, warned = 0, seals = 0;
            r >> ar.min.x >> ar.min.y >> ar.max.x >> ar.max.y >> ar.door.x >> ar.door.y >> sealed >>
                warned >> seals;
            if (!r.ok()) return false;
            // A hall whose corners are the wrong way round, or that reaches
            // outside the floor, is a hall that would trap the hero in a
            // rectangle with no way out. Refuse the save instead.
            if (ar.min.x < 0 || ar.min.y < 0 || ar.max.x < ar.min.x || ar.max.y < ar.min.y)
                return false;
            if (ar.max.x >= w || ar.max.y >= h) return false;
            if (ar.door.x < 0 || ar.door.y < 0 || ar.door.x >= w || ar.door.y >= h) return false;
            ar.exists = true;
            ar.sealed = sealed != 0;
            ar.warned = warned != 0;
            ar.seals = seals != 0;
            lvl.arena = ar;
        }

        lvl.map.resize(w, h);
        const std::size_t cells = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);

        std::vector<std::uint8_t> tiles;
        r.rle(tiles, cells);
        if (!r.ok() || tiles.size() != cells) return false;
        for (std::size_t i = 0; i < cells; ++i) {
            if (tiles[i] > static_cast<std::uint8_t>(Tile::Altar)) return false;
            lvl.map.raw_tiles()[i] = static_cast<Tile>(tiles[i]);
        }
        r.rle(lvl.map.raw_explored(), cells);
        if (!r.ok() || lvl.map.raw_explored().size() != cells) return false;

        if (!r.count(n, 4096)) return false;
        lvl.monsters.resize(n);
        for (auto& m : lvl.monsters) {
            if (!read_actor(r, m.a)) return false;
            int awake = 0;
            r >> m.species >> awake >> m.last_seen.x >> m.last_seen.y >> m.search_turns >>
                m.summon_cooldown >> m.charge >> m.revives >> m.phase;
            if (!r.ok() || m.species < 0 || m.species >= static_cast<int>(bestiary().size()))
                return false;
            // A phase outside the species' range would let a doctored save put
            // a boss into a pattern that has no code behind it.
            const int phases = bestiary()[static_cast<std::size_t>(m.species)].phases;
            if (m.phase < 1 || m.phase > std::max(1, phases)) return false;
            m.awake = awake != 0;
        }

        if (!r.count(n, 4096)) return false;
        lvl.items.resize(n);
        for (auto& it : lvl.items)
            if (!read_item(r, it)) return false;

        if (!r.count(n, 4096)) return false;
        lvl.embers.resize(n);
        for (auto& e : lvl.embers) {
            r >> e.pos.x >> e.pos.y >> e.turns;
            if (!r.ok() || e.turns <= 0) return false;
            if (e.pos.x < 0 || e.pos.y < 0 || e.pos.x >= w || e.pos.y >= h) return false;
        }
    }
    if (!r.ok()) return false;
    if (g.levels_.size() <= static_cast<std::size_t>(g.depth_)) return false;
    if (!g.levels_[static_cast<std::size_t>(g.depth_)].generated) return false;

    *this = std::move(g);
    recompute_fov();
    needs_flow_rebuild_ = true;
    message(Text{"Игра загружена.", "Game loaded."}, Severity::System);
    return true;
}

}  // namespace nav
