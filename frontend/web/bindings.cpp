// SPDX-License-Identifier: MIT
//
// WebAssembly bindings.
//
// The browser build uses exactly the same nav::Game as the terminal build; this
// file is only a translation layer. JavaScript sends actions as integers and
// reads the state back as one JSON document per turn.
//
// The map is serialised as three parallel strings rather than an array of cell
// objects. A 72x34 floor is 2448 cells, and an object per cell turned a turn
// into roughly 300 KB of JSON — enough to be felt on a phone. Three strings
// plus a small colour palette bring the same information down to a few
// kilobytes.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "nav/achievements.hpp"
#include "nav/fov.hpp"
#include "nav/game.hpp"
#include "nav/keys.hpp"
#include "nav/rt.hpp"
#include "nav/score.hpp"

#ifdef __EMSCRIPTEN__
#  include <emscripten/emscripten.h>
#else
#  define EMSCRIPTEN_KEEPALIVE
#endif

namespace {

nav::Game g_game;
nav::Lang g_lang = nav::Lang::Ru;
nav::Palette g_palette = nav::Palette::Default;

/// Escapes a UTF-8 string for embedding in JSON. Multi-byte sequences pass
/// through untouched — JSON is defined over Unicode, so Cyrillic needs no
/// escaping, only the structural characters do.
void append_json_string(std::string& out, const std::string& value) {
    out += '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void append_field(std::string& out, const char* key, long long value, bool& first) {
    if (!first) out += ',';
    first = false;
    out += '"';
    out += key;
    out += "\":";
    out += std::to_string(value);
}

void append_field(std::string& out, const char* key, const std::string& value, bool& first) {
    if (!first) out += ',';
    first = false;
    out += '"';
    out += key;
    out += "\":";
    append_json_string(out, value);
}

/// Collects the distinct strings a frame refers to — colours, terrain sprite
/// keys, entity sprite keys — so the per-cell maps can name each with a single
/// character and the string itself is sent once.
///
/// Three of these per frame is what keeps a 2448-cell floor at a few kilobytes
/// instead of a few hundred: the payload is three flat strings plus three short
/// lists, not 2448 objects.
class StringTable {
public:
    /// ' ' means "nothing here"; a real entry starts at 'A'.
    char index_of(const char* s) {
        if (s == nullptr) return ' ';
        for (std::size_t i = 0; i < values_.size(); ++i)
            if (values_[i] == s) return static_cast<char>('A' + static_cast<int>(i));
        values_.emplace_back(s);
        return static_cast<char>('A' + static_cast<int>(values_.size()) - 1);
    }

    std::string to_json() const {
        std::string out = "[";
        for (std::size_t i = 0; i < values_.size(); ++i) {
            if (i) out += ',';
            append_json_string(out, values_[i]);
        }
        out += ']';
        return out;
    }

private:
    std::vector<std::string> values_;
};

/// The achievement list as JSON, each row marked unlocked or not against
/// `unlocked`, and `fresh` for the ones just earned.
std::string achievements_json(const std::vector<std::string>& unlocked,
                              const std::vector<std::string>& fresh) {
    using namespace nav;
    const auto has = [](const std::vector<std::string>& v, const char* k) {
        for (const std::string& s : v) if (s == k) return true;
        return false;
    };
    std::string out = "[";
    bool first = true;
    for (const AchievementInfo& a : achievement_table()) {
        if (!first) out += ',';
        first = false;
        out += "{\"key\":";
        append_json_string(out, a.key);
        out += ",\"name\":";
        append_json_string(out, a.name.get(g_lang));
        out += ",\"how\":";
        append_json_string(out, a.how.get(g_lang));
        out += ",\"unlocked\":" + std::string(has(unlocked, a.key) ? "true" : "false");
        out += ",\"fresh\":" + std::string(has(fresh, a.key) ? "true" : "false");
        out += "}";
    }
    out += "]";
    return out;
}

const char* severity_name(nav::Severity s) {
    switch (s) {
        case nav::Severity::Good:     return "good";
        case nav::Severity::Bad:      return "bad";
        case nav::Severity::Critical: return "crit";
        case nav::Severity::System:   return "sys";
        default:                      return "info";
    }
}

const char* effect_key(nav::Effect e) {
    switch (e) {
        case nav::Effect::Poison:    return "poison";
        case nav::Effect::Burn:      return "burn";
        case nav::Effect::Freeze:    return "freeze";
        case nav::Effect::Confusion: return "confusion";
        case nav::Effect::Blind:     return "blind";
        case nav::Effect::Haste:     return "haste";
        case nav::Effect::Slow:      return "slow";
        case nav::Effect::Regen:     return "regen";
        case nav::Effect::Might:     return "might";
        case nav::Effect::Shield:    return "shield";
        case nav::Effect::Sleep:     return "sleep";
        default:                     return "other";
    }
}

/// Copies a std::string onto the heap for JavaScript to read and then free.
char* to_c_string(const std::string& s) {
    char* buffer = static_cast<char*>(std::malloc(s.size() + 1));
    if (!buffer) return nullptr;
    std::memcpy(buffer, s.c_str(), s.size() + 1);
    return buffer;
}

std::string build_state_json() {
    using namespace nav;
    const Game& g = g_game;
    const Hero& h = g.hero();
    const Map& map = g.map();

    StringTable palette, terrain_keys, entity_keys;
    std::string glyphs, colors, visibility, terrain, entities;
    const std::size_t cells =
        static_cast<std::size_t>(map.width()) * static_cast<std::size_t>(map.height());
    glyphs.reserve(cells);
    colors.reserve(cells);
    visibility.reserve(cells);
    terrain.reserve(cells);
    entities.reserve(cells);

    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            const RenderCell cell = g.render_at({x, y});
            glyphs += cell.explored ? cell.glyph : ' ';
            colors += palette.index_of(display_color(cell.color, g_palette).c_str());
            visibility += cell.visible ? '2' : (cell.explored ? '1' : '0');
            // Sprite keys travel next to the glyphs rather than instead of
            // them: the page can switch between the two renderers without
            // asking the engine for a different frame.
            terrain += cell.explored ? terrain_keys.index_of(cell.terrain) : ' ';
            entities += cell.explored ? entity_keys.index_of(cell.entity) : ' ';
        }
    }

    std::string out = "{";
    bool first = true;

    append_field(out, "w", map.width(), first);
    append_field(out, "h", map.height(), first);
    append_field(out, "depth", g.depth(), first);
    append_field(out, "maxDepth", kMaxDepth, first);
    append_field(out, "turn", g.turn(), first);
    append_field(out, "score", g.score(), first);
    append_field(out, "state", static_cast<int>(g.state()), first);
    append_field(out, "seedText", g.config().seed_text, first);
    // The belt the hero is in — the frontends show its name beside the depth.
    append_field(out, "zone", zone_theme_for_depth(g.depth()).name.get(g_lang), first);
    append_field(out, "needleIntact", g.needle_intact() ? 1 : 0, first);
    // The floor's belt event, if one is running: name and note for the banner.
    if (g.level_event() != EventKind::None) {
        append_field(out, "eventName", event_name(g.level_event()).get(g_lang), first);
        append_field(out, "eventNote", event_note(g.level_event()).get(g_lang), first);
    }
    // The codex: one character per bestiary row, '1' once the hero has laid
    // eyes on that species. Same order as nav_static_json's "bestiary".
    {
        std::string seen;
        seen.reserve(g.codex_seen().size());
        for (std::uint8_t v : g.codex_seen()) seen += v ? '1' : '0';
        append_field(out, "codex", seen, first);
    }
    // The belt's three colours. The sprite renderer tints one neutral set of
    // stones with these, which is why the crypts, the mire and the frozen
    // kingdom do not need three copies of every tile.
    {
        const ZoneTheme& theme = zone_theme_for_depth(g.depth());
        append_field(out, "tintWall", display_color(theme.wall_color, g_palette), first);
        append_field(out, "tintFloor", display_color(theme.floor_color, g_palette), first);
        append_field(out, "tintLiquid", display_color(theme.liquid_color, g_palette), first);
    }

    // The floor's guardian, while it lives. The page uses it to name the fight
    // and to pick the music, and the phase is what makes the music change when
    // the fight does.
    out += ",\"boss\":";
    if (const Monster* boss = g.active_boss()) {
        const Species& sp = bestiary()[static_cast<std::size_t>(boss->species)];
        out += "{\"key\":";
        append_json_string(out, sp.key);
        out += ",\"name\":";
        append_json_string(out, sp.name.get(g_lang));
        out += ",\"phase\":" + std::to_string(boss->phase);
        out += ",\"phases\":" + std::to_string(sp.phases);
        out += ",\"hp\":" + std::to_string(boss->a.hp);
        out += ",\"maxHp\":" + std::to_string(boss->a.max_hp);
        out += ",\"mini\":" + std::string((sp.ai & nav::AiMiniBoss) ? "true" : "false");
        out += "}";
    } else {
        out += "null";
    }

    out += ",\"map\":{";
    out += "\"glyphs\":";
    append_json_string(out, glyphs);
    out += ",\"colors\":";
    append_json_string(out, colors);
    out += ",\"vis\":";
    append_json_string(out, visibility);
    out += ",\"palette\":" + palette.to_json();
    out += ",\"terrain\":";
    append_json_string(out, terrain);
    out += ",\"terrainKeys\":" + terrain_keys.to_json();
    out += ",\"entities\":";
    append_json_string(out, entities);
    out += ",\"entityKeys\":" + entity_keys.to_json();
    out += "}";

    // --- Hero -------------------------------------------------------------
    out += ",\"hero\":{";
    bool hero_first = true;
    append_field(out, "x", h.a.pos.x, hero_first);
    append_field(out, "y", h.a.pos.y, hero_first);
    append_field(out, "hp", h.a.hp, hero_first);
    append_field(out, "maxHp", h.a.max_hp, hero_first);
    append_field(out, "mana", h.mana, hero_first);
    append_field(out, "maxMana", h.max_mana, hero_first);
    append_field(out, "level", h.level, hero_first);
    append_field(out, "xp", h.xp, hero_first);
    append_field(out, "xpNext", xp_for_level(h.level + 1), hero_first);
    append_field(out, "gold", h.gold, hero_first);
    append_field(out, "kills", h.kills, hero_first);
    append_field(out, "nutrition", h.nutrition, hero_first);
    append_field(out, "attack", g.hero_attack(), hero_first);
    append_field(out, "defence", g.hero_defence(), hero_first);
    append_field(out, "className", class_info(h.cls).name.get(g_lang), hero_first);
    // The completed set, if any. Named rather than numbered because the panel
    // shows it to the player, and because a set the player cannot name is a
    // set they will never deliberately assemble.
    if (g.hero_set() != GearSet::None) {
        const GearSetInfo& info = gear_set_info(g.hero_set());
        append_field(out, "setName", info.name.get(g_lang), hero_first);
        append_field(out, "setNote", info.note.get(g_lang), hero_first);
    }
    if (const GearPair* pair = g.hero_pair()) {
        append_field(out, "pairName", pair->name.get(g_lang), hero_first);
        append_field(out, "pairNote", pair->note.get(g_lang), hero_first);
    }
    out += ",\"effects\":[";
    bool effect_first = true;
    for (const auto& e : h.a.effects) {
        if (e.turns <= 0) continue;
        if (!effect_first) out += ',';
        effect_first = false;
        out += "{\"kind\":";
        append_json_string(out, effect_key(e.kind));
        out += ",\"turns\":" + std::to_string(e.turns) + "}";
    }
    out += "]}";

    // --- Visible monsters --------------------------------------------------
    out += ",\"monsters\":[";
    bool monster_first = true;
    for (const auto& m : g.monsters()) {
        if (!map.visible(m.a.pos)) continue;
        const auto& sp = bestiary()[static_cast<std::size_t>(m.species)];
        if (!monster_first) out += ',';
        monster_first = false;
        out += "{\"x\":" + std::to_string(m.a.pos.x);
        out += ",\"y\":" + std::to_string(m.a.pos.y);
        out += ",\"key\":";           // sprite key, so the panel can show the picture
        append_json_string(out, sp.key);
        out += ",\"hp\":" + std::to_string(m.a.hp);
        out += ",\"maxHp\":" + std::to_string(m.a.max_hp);
        out += ",\"name\":";
        append_json_string(out, sp.name.get(g_lang));
        out += ",\"desc\":";
        append_json_string(out, sp.description.get(g_lang));
        out += ",\"color\":";
        append_json_string(out, sp.color);
        out += ",\"glyph\":";
        append_json_string(out, std::string(1, sp.glyph));
        out += "}";
    }
    out += "]";

    // --- Inventory ---------------------------------------------------------
    out += ",\"inventory\":[";
    for (std::size_t i = 0; i < h.inv.items.size(); ++i) {
        const Item& it = h.inv.items[i];
        if (i) out += ',';
        out += "{\"i\":" + std::to_string(i);
        out += ",\"name\":";
        append_json_string(out, item_name(it, g.identification()).get(g_lang));
        out += ",\"note\":";
        append_json_string(out, item_note(it, g.identification()).get(g_lang));
        out += ",\"glyph\":";
        append_json_string(out, std::string(1, item_glyph(it)));
        out += ",\"count\":" + std::to_string(it.count);
        out += ",\"equipped\":" + std::string(h.inv.is_equipped(static_cast<int>(i)) ? "true" : "false");
        out += ",\"wearable\":" + std::string(item_slot(it) != Slot::None ? "true" : "false");
        out += ",\"cursed\":" + std::string((it.cursed && it.identified) ? "true" : "false");
        // What wearing it would change. The engine works it out by trying the
        // item on and asking the character sheet, so the page never has to
        // reimplement — or mispredict — the rules it is previewing.
        const EquipPreview pv = g_game.equip_preview(static_cast<int>(i));
        if (pv.valid && !pv.taking_off && !pv.changes_nothing()) {
            out += ",\"delta\":{\"atk\":" + std::to_string(pv.attack) +
                   ",\"def\":" + std::to_string(pv.defence) +
                   ",\"hp\":" + std::to_string(pv.max_hp) +
                   ",\"spd\":" + std::to_string(pv.speed) +
                   ",\"sight\":" + std::to_string(pv.sight) + "}";
        }
        out += "}";
    }
    out += "]";

    // --- Spells ------------------------------------------------------------
    out += ",\"spells\":[";
    bool spell_first = true;
    for (const auto& t : spell_table()) {
        if (!h.knows(t.spell)) continue;
        if (!spell_first) out += ',';
        spell_first = false;
        out += "{\"id\":" + std::to_string(static_cast<int>(t.spell));
        out += ",\"name\":";
        append_json_string(out, t.name.get(g_lang));
        out += ",\"note\":";
        append_json_string(out, t.note.get(g_lang));
        out += ",\"cost\":" + std::to_string(t.cost);
        out += ",\"castable\":" + std::string(h.mana >= t.cost ? "true" : "false");
        out += ",\"needsTarget\":" + std::string(t.needs_target ? "true" : "false");
        out += ",\"targets\":[";
        if (t.needs_target && h.mana >= t.cost) {
            const auto targets = g.spell_targets(t.spell);
            for (std::size_t k = 0; k < targets.size(); ++k) {
                if (k) out += ',';
                out += "{\"x\":" + std::to_string(targets[k].x) +
                       ",\"y\":" + std::to_string(targets[k].y) + "}";
            }
        }
        out += "]}";
    }
    out += "]";

    // --- The situation, as the engine judges it ----------------------------
    //
    // A key held down in the browser has to stop for the same reasons the
    // engine's own run does. Rather than let the page grow a second opinion
    // about what "something happened" means, the engine's snapshot is sent
    // across and the page only compares two of them.
    {
        const Game::Situation sit = g.situation();
        out += ",\"situation\":{\"hp\":" + std::to_string(sit.hp);
        out += ",\"depth\":" + std::to_string(sit.depth);
        out += ",\"effects\":" + std::to_string(static_cast<unsigned long long>(sit.effects));
        out += ",\"foes\":" + std::string(sit.foes ? "true" : "false");
        out += ",\"underfoot\":" + std::string(sit.underfoot ? "true" : "false");
        out += ",\"playing\":" +
               std::string(g.state() == RunState::Playing ? "true" : "false") + "}";
    }

    // --- What the hero is standing on --------------------------------------
    out += ",\"here\":{";
    const Tile tile = map.at(h.a.pos);
    out += "\"stairsDown\":" + std::string(tile == Tile::StairsDown ? "true" : "false");
    out += ",\"stairsUp\":" + std::string(tile == Tile::StairsUp ? "true" : "false");
    out += ",\"altar\":" + std::string(tile == Tile::Altar ? "true" : "false");
    const int item_index = g.item_index_at(h.a.pos);
    out += ",\"item\":";
    if (item_index >= 0)
        append_json_string(
            out, item_name(g.floor_items()[static_cast<std::size_t>(item_index)],
                           g.identification()).get(g_lang));
    else
        out += "null";
    out += "}";

    // --- Message log -------------------------------------------------------
    out += ",\"log\":[";
    const auto& log = g.log();
    const std::size_t keep = 40;
    const std::size_t start = log.size() > keep ? log.size() - keep : 0;
    for (std::size_t i = start; i < log.size(); ++i) {
        if (i > start) out += ',';
        out += "{\"t\":";
        append_json_string(out, log[i].text.get(g_lang));
        out += ",\"s\":";
        append_json_string(out, severity_name(log[i].severity));
        out += ",\"turn\":" + std::to_string(log[i].turn) + "}";
    }
    out += "]";

    out += "}";
    return out;
}

}  // namespace

extern "C" {

/// Starts a run. `seed_text` may be empty, in which case `fallback_seed` (a
/// value JavaScript derives from the clock) is used.
EMSCRIPTEN_KEEPALIVE void nav_new_game(const char* seed_text, int hero_class,
                                       double fallback_seed) {
    nav::GameConfig cfg;
    cfg.seed_text = seed_text ? seed_text : "";
    cfg.seed = cfg.seed_text.empty()
                   ? static_cast<std::uint64_t>(fallback_seed) | 1ULL
                   : nav::Rng::hash_seed(cfg.seed_text);
    cfg.hero_class = static_cast<nav::HeroClass>(
        hero_class < 0 || hero_class >= static_cast<int>(nav::HeroClass::Count) ? 0 : hero_class);
    g_game.start(cfg);
}

/// Applies one action. Returns 1 when a turn was consumed.
EMSCRIPTEN_KEEPALIVE int nav_perform(int type, int dx, int dy, int index, int tx, int ty) {
    if (type < 0 || type > static_cast<int>(nav::ActionType::Explore)) return 0;
    nav::Action action;
    action.type = static_cast<nav::ActionType>(type);
    action.dir = {dx, dy};
    action.index = index;
    action.target = {tx, ty};
    return g_game.perform(action) ? 1 : 0;
}

/// Sets the display language: 0 Russian, 1 English.
EMSCRIPTEN_KEEPALIVE void nav_set_language(int lang) {
    g_lang = lang == 1 ? nav::Lang::En : nav::Lang::Ru;
}

/// Sets the colour palette: 0 default, 1 colour-blind (red-green). Affects only
/// how colours are reported to the page — never the run.
EMSCRIPTEN_KEEPALIVE void nav_set_palette(int cb) {
    g_palette = cb == 1 ? nav::Palette::Colorblind : nav::Palette::Default;
}

/// Returns the whole game state as JSON. The caller must free the pointer with
/// nav_free_cstr.
EMSCRIPTEN_KEEPALIVE char* nav_state_json() { return to_c_string(build_state_json()); }

EMSCRIPTEN_KEEPALIVE char* nav_save() { return to_c_string(g_game.save()); }

EMSCRIPTEN_KEEPALIVE int nav_load(const char* blob) {
    return (blob && g_game.load(blob)) ? 1 : 0;
}

/// The static content tables, for the title screen and the bestiary panel.
EMSCRIPTEN_KEEPALIVE char* nav_static_json() {
    std::string out = "{\"classes\":[";
    for (std::size_t i = 0; i < nav::class_table().size(); ++i) {
        const auto& c = nav::class_table()[i];
        if (i) out += ',';
        out += "{\"id\":" + std::to_string(static_cast<int>(c.cls));
        out += ",\"key\":";           // sprite key for the portrait on the card
        append_json_string(out, nav::hero_sprite_key(c.cls));
        out += ",\"name\":";
        append_json_string(out, c.name.get(g_lang));
        out += ",\"blurb\":";
        append_json_string(out, c.blurb.get(g_lang));
        out += ",\"hp\":" + std::to_string(c.hp);
        out += ",\"mana\":" + std::to_string(c.mana);
        out += ",\"attack\":" + std::to_string(c.attack);
        out += ",\"defence\":" + std::to_string(c.defence);
        out += ",\"speed\":" + std::to_string(c.speed);
        out += ",\"trait\":";
        if (c.traits & nav::TraitHerbalist)
            append_json_string(out, g_lang == nav::Lang::Ru
                                        ? "знает все зелья, и они крепче"
                                        : "knows every potion, and they work harder");
        else if (c.traits & nav::TraitSmith)
            append_json_string(out, g_lang == nav::Lang::Ru
                                        ? "снаряжение на ступень лучше, капища вполовину дешевле"
                                        : "gear one grade better, shrines at half price");
        else if (c.traits & nav::TraitCleave)
            append_json_string(out, g_lang == nav::Lang::Ru
                                        ? "удар достаёт всех, кто стоит рядом"
                                        : "every blow sweeps all adjacent enemies");
        else
            out += "null";
        out += "}";
    }
    out += "],\"zones\":[";
    bool zone_first = true;
    // Straight from the engine's own list. This used to be a hand-written
    // {Pogost, Chernotop, Koshchei} — which is why the fourth belt existed in
    // the game for a whole release and nowhere on the title screen.
    for (nav::Zone z : nav::descending_belts()) {
        const auto& theme = nav::zone_theme(z);
        if (!zone_first) out += ',';
        zone_first = false;
        out += "{\"name\":";
        append_json_string(out, theme.name.get(g_lang));
        out += ",\"blurb\":";
        append_json_string(out, theme.arrival.get(g_lang));
        const int last = nav::belt_last_depth(z);
        out += ",\"depths\":";
        append_json_string(out, std::to_string(last - 3) + "\u2013" + std::to_string(last));
        out += ",\"guardian\":";
        if (const char* key = nav::boss_for_depth(last)) {
            const int index = nav::species_index(key);
            append_json_string(out, index >= 0
                ? nav::bestiary()[static_cast<std::size_t>(index)].name.get(g_lang)
                : std::string());
        } else {
            append_json_string(out, std::string());
        }
        out += "}";
    }
    out += "],\"bestiary\":[";
    bool first = true;
    for (const auto& s : nav::bestiary()) {
        if (!first) out += ',';
        first = false;
        out += "{\"name\":";
        append_json_string(out, s.name.get(g_lang));
        out += ",\"key\":";           // sprite key, so the bestiary can show the picture
        append_json_string(out, s.key);
        out += ",\"glyph\":";
        append_json_string(out, std::string(1, s.glyph));
        out += ",\"color\":";
        append_json_string(out, s.color);
        out += ",\"desc\":";
        append_json_string(out, s.description.get(g_lang));
        out += ",\"minDepth\":" + std::to_string(s.min_depth);
        out += ",\"maxDepth\":" + std::to_string(s.max_depth);
        out += ",\"boss\":" + std::string((s.ai & nav::AiBoss) ? "true" : "false");
        out += "}";
    }
    out += "]}";
    return to_c_string(out);
}

/// Adds the finished run to `blob` and hands the table back as JSON.
///
/// The page keeps the table in the browser's own storage but never parses or
/// orders it: both frontends go through the same engine code, so a run scored
/// in the terminal and one scored in a browser sit in the same order by the
/// same rules. `place` is where this run landed, or -1.
EMSCRIPTEN_KEEPALIVE char* nav_record_score(const char* blob) {
    using namespace nav;
    std::vector<ScoreEntry> table;
    if (blob) parse_scores(blob, table);
    const int place = insert_score(table, entry_from(g_game));

    std::string out = "{\"place\":" + std::to_string(place);
    out += ",\"blob\":";
    append_json_string(out, serialize_scores(table));
    out += ",\"rows\":[";
    bool first = true;
    for (const ScoreEntry& e : table) {
        if (!first) out += ',';
        first = false;
        out += "{\"score\":" + std::to_string(e.score);
        out += ",\"deepest\":" + std::to_string(e.deepest);
        out += ",\"turns\":" + std::to_string(e.turns);
        out += ",\"level\":" + std::to_string(e.level);
        out += ",\"kills\":" + std::to_string(e.kills);
        out += ",\"won\":" + std::string(e.won ? "true" : "false");
        out += ",\"className\":";
        append_json_string(out, class_info(e.cls).name.get(g_lang));
        out += ",\"daily\":" + std::string(e.daily ? "true" : "false");
        out += ",\"seedText\":";
        append_json_string(out, e.seed_text);
        out += "}";
    }
    out += "]}";
    return to_c_string(out);
}
/// Folds the current run's deeds into `blob` and hands back the merged store
/// plus the full list. Mirrors nav_record_score.
EMSCRIPTEN_KEEPALIVE char* nav_achievements_record(const char* blob) {
    using namespace nav;
    std::vector<std::string> unlocked;
    if (blob) parse_achievements(blob, unlocked);
    const std::vector<std::string> fresh =
        merge_achievements(unlocked, achievements_earned(g_game));

    std::string out = "{\"blob\":";
    append_json_string(out, serialize_achievements(unlocked));
    out += ",\"list\":" + achievements_json(unlocked, fresh);
    out += "}";
    return to_c_string(out);
}

/// Reads a stored achievements blob back for the title screen.
EMSCRIPTEN_KEEPALIVE char* nav_achievements_table(const char* blob) {
    using namespace nav;
    std::vector<std::string> unlocked;
    if (blob) parse_achievements(blob, unlocked);
    return to_c_string(achievements_json(unlocked, {}));
}

/// Reads a stored table back for the title screen, without adding anything.
EMSCRIPTEN_KEEPALIVE char* nav_score_table(const char* blob) {
    using namespace nav;
    std::vector<ScoreEntry> table;
    if (blob) parse_scores(blob, table);
    std::string out = "[";
    bool first = true;
    for (const ScoreEntry& e : table) {
        if (!first) out += ',';
        first = false;
        out += "{\"score\":" + std::to_string(e.score);
        out += ",\"deepest\":" + std::to_string(e.deepest);
        out += ",\"turns\":" + std::to_string(e.turns);
        out += ",\"level\":" + std::to_string(e.level);
        out += ",\"kills\":" + std::to_string(e.kills);
        out += ",\"won\":" + std::string(e.won ? "true" : "false");
        out += ",\"className\":";
        append_json_string(out, class_info(e.cls).name.get(g_lang));
        out += ",\"daily\":" + std::string(e.daily ? "true" : "false");
        out += ",\"seedText\":";
        append_json_string(out, e.seed_text);
        out += "}";
    }
    out += "]";
    return to_c_string(out);
}

/// The keyboard table for one scheme, as JSON.
///
/// The browser resolves keys itself — one lookup per keypress, no call into
/// WebAssembly — but the table it looks them up in is built here, by the same
/// function the terminal calls. That is the point: there is one keymap in this
/// project, and both frontends read it rather than each keeping its own.
EMSCRIPTEN_KEEPALIVE char* nav_keys_json(int scheme_id) {
    const nav::KeyScheme scheme =
        scheme_id == 1 ? nav::KeyScheme::Wasd : nav::KeyScheme::Classic;

    std::string out = "{\"scheme\":";
    append_json_string(out, nav::key_scheme_key(scheme));
    out += ",\"name\":";
    append_json_string(out, nav::key_scheme_name(scheme).get(g_lang));
    out += ",\"keys\":{";

    bool first = true;
    for (int c = 32; c < 127; ++c) {
        const nav::KeyPress k = nav::command_for(scheme, static_cast<char>(c));
        if (k.cmd == nav::Command::None) continue;
        if (!first) out += ',';
        first = false;
        append_json_string(out, std::string(1, static_cast<char>(c)));
        out += ":{\"cmd\":" + std::to_string(static_cast<int>(k.cmd)) +
               ",\"dx\":" + std::to_string(k.dir.x) +
               ",\"dy\":" + std::to_string(k.dir.y) + "}";
    }
    out += "},\"help\":[";
    bool first_row = true;
    for (const nav::KeyHelpRow& row : nav::key_help(scheme)) {
        if (!first_row) out += ',';
        first_row = false;
        out += "{\"cmd\":" + std::to_string(static_cast<int>(row.cmd)) + ",\"keys\":";
        append_json_string(out, row.keys.get(g_lang));
        out += ",\"what\":";
        append_json_string(out, row.what.get(g_lang));
        out += "}";
    }
    out += "]}";
    return to_c_string(out);
}

/// The post-mortem for the ending screen, as JSON.
EMSCRIPTEN_KEEPALIVE char* nav_postmortem_json() {
    const nav::Postmortem pm = g_game.postmortem();
    std::string out = "{\"killedBy\":";
    append_json_string(out, pm.killed_by.get(g_lang));
    out += ",\"blows\":[";
    for (std::size_t i = 0; i < pm.blows.size(); ++i) {
        if (i) out += ',';
        out += "{\"source\":";
        append_json_string(out, pm.blows[i].source.get(g_lang));
        out += ",\"amount\":" + std::to_string(pm.blows[i].amount);
        out += ",\"left\":" + std::to_string(pm.blows[i].hp_left);
        out += ",\"turn\":" + std::to_string(pm.blows[i].turn) + "}";
    }
    out += "],\"unspent\":[";
    for (std::size_t i = 0; i < pm.unspent.size(); ++i) {
        if (i) out += ',';
        append_json_string(out, pm.unspent[i].get(g_lang));
    }
    out += "],\"epilogue\":[";
    {
        const nav::Epilogue ep = g_game.epilogue();
        for (std::size_t i = 0; i < ep.lines.size(); ++i) {
            if (i) out += ',';
            append_json_string(out, ep.lines[i].get(g_lang));
        }
    }
    out += "]}";
    return to_c_string(out);
}

EMSCRIPTEN_KEEPALIVE void nav_free_cstr(char* p) { std::free(p); }

EMSCRIPTEN_KEEPALIVE const char* nav_version() { return "1.0.0"; }

/// The daily-run seed text for day `day_index` (JS computes the index from the
/// clock). Returned so both frontends build the string one way.
EMSCRIPTEN_KEEPALIVE char* nav_daily_seed(double day_index) {
    return to_c_string(nav::daily_seed_text(static_cast<long long>(day_index)));
}

// --- The real-time game ---------------------------------------------------
//
// A second, separate world. The page calls nav_rt_step once per frame with
// however many fixed steps have come due, then reads a frame back. The floor
// itself is sent only when it changes — a door slams, the hero sees further,
// a new floor begins — which is what keeps sixty frames a second down to a
// few hundred bytes each.

}  // extern "C"

namespace {

nav::rt::World g_rt;

/// Short, fixed-precision numbers: a frame is mostly coordinates, and
/// std::to_string would spend six decimals on each one.
std::string fnum(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", static_cast<double>(v));
    return buf;
}

std::string build_rt_map_json() {
    const nav::Map& m = g_rt.map();
    const nav::ZoneTheme& theme = nav::zone_theme_for_depth(g_rt.depth());
    std::string terrain, vis;
    terrain.reserve(static_cast<std::size_t>(m.width() * m.height()));
    vis.reserve(terrain.capacity());
    StringTable keys;
    for (int y = 0; y < m.height(); ++y) {
        for (int x = 0; x < m.width(); ++x) {
            const nav::Vec2 c{x, y};
            terrain += keys.index_of(nav::tile_sprite_key(m.at(c)));
            vis += m.visible(c) ? '2' : (m.explored(c) ? '1' : '0');
        }
    }
    std::string out = "{";
    bool first = true;
    append_field(out, "rev", g_rt.map_revision(), first);
    append_field(out, "depth", g_rt.depth(), first);
    append_field(out, "w", m.width(), first);
    append_field(out, "h", m.height(), first);
    append_field(out, "zone", theme.name.get(g_lang), first);
    append_field(out, "tintWall", std::string(theme.wall_color), first);
    append_field(out, "tintFloor", std::string(theme.floor_color), first);
    append_field(out, "tintLiquid", std::string(theme.liquid_color), first);
    append_field(out, "terrain", terrain, first);
    append_field(out, "vis", vis, first);
    out += ",\"terrainKeys\":" + keys.to_json();
    out += ",\"exit\":[" + std::to_string(g_rt.exit().x) + "," + std::to_string(g_rt.exit().y) + "]}";
    return out;
}

std::string build_rt_frame_json() {
    using namespace nav::rt;
    const nav::Map& m = g_rt.map();
    const Hero& h = g_rt.hero();
    const auto seen = [&m](V p) {
        return m.visible({static_cast<int>(p.x), static_cast<int>(p.y)});
    };

    std::string out = "{\"t\":" + fnum(g_rt.time());
    out += ",\"depth\":" + std::to_string(g_rt.depth());
    out += ",\"rev\":" + std::to_string(g_rt.map_revision());
    out += std::string(",\"dead\":") + (g_rt.dead() ? "true" : "false");
    out += std::string(",\"won\":") + (g_rt.won() ? "true" : "false");
    out += std::string(",\"locked\":") + (g_rt.in_locked_room() ? "true" : "false");

    out += ",\"hero\":{\"x\":" + fnum(h.pos.x) + ",\"y\":" + fnum(h.pos.y);
    out += ",\"fx\":" + fnum(h.facing.x) + ",\"ax\":" + fnum(h.aim.x) + ",\"ay\":" + fnum(h.aim.y);
    out += ",\"hp\":" + std::to_string(h.hp) + ",\"maxHp\":" + std::to_string(h.max_hp);
    out += ",\"sh\":" + std::to_string(h.shield) + ",\"maxSh\":" + std::to_string(h.max_shield);
    out += ",\"coins\":" + std::to_string(h.coins) + ",\"kills\":" + std::to_string(h.kills);
    out += std::string(",\"inv\":") + (h.invulnerable > 0 ? "1" : "0");
    out += std::string(",\"mv\":") + (h.moving ? "1" : "0");
    out += std::string(",\"fire\":") + (h.firing ? "1" : "0");
    out += ",\"en\":" + std::to_string(h.energy) + ",\"maxEn\":" + std::to_string(h.max_energy);
    out += ",\"w\":[" + std::to_string(h.weapons[0]) + "," + std::to_string(h.weapons[1]) + "]";
    out += ",\"hand\":" + std::to_string(h.hand);
    out += ",\"frozen\":" + fnum(h.frozen) + ",\"burning\":" + fnum(h.burning) +
           ",\"blind\":" + fnum(h.blind);
    // The weapon lying underfoot, if any: what "take" would pick up.
    int under = -1;
    for (const Pickup& p : g_rt.pickups())
        if (p.kind == PickupKind::Weapon && length(p.pos - h.pos) < kPickupReach) under = p.amount;
    out += ",\"under\":" + std::to_string(under);
    out += ",\"key\":";
    append_json_string(out, nav::hero_sprite_key(h.cls));
    out += "}";

    // The guardian's bar: shown from the moment it wakes, wherever it is.
    if (const Enemy* b = g_rt.boss(); b && b->awake) {
        const nav::Species& sp = nav::bestiary()[static_cast<std::size_t>(b->species)];
        out += ",\"boss\":{\"id\":" + std::to_string(b->id) + ",\"key\":";
        append_json_string(out, sp.key);
        out += ",\"name\":";
        append_json_string(out, sp.name.get(g_lang));
        out += ",\"hp\":" + std::to_string(b->hp) + ",\"max\":" + std::to_string(b->max_hp);
        out += ",\"phase\":" + std::to_string(b->phase) + ",\"phases\":" + std::to_string(b->phases);
        out += std::string(",\"guarded\":") + (b->armour < 1.0f ? "1" : "0") + "}";
    }

    out += ",\"enemies\":[";
    bool first = true;
    for (const Enemy& e : g_rt.enemies()) {
        if (!seen(e.pos)) continue;
        // A needle not yet found stays unseen; a guardian under the ground
        // is sent, flagged, so the page can draw the earth moving over it.
        if (e.hidden && e.role != Role::Boss) continue;
        const nav::Species& sp = nav::bestiary()[static_cast<std::size_t>(e.species)];
        if (!first) out += ',';
        first = false;
        out += "{\"id\":" + std::to_string(e.id) + ",\"key\":";
        append_json_string(out, sp.key);
        out += ",\"x\":" + fnum(e.pos.x) + ",\"y\":" + fnum(e.pos.y);
        out += ",\"hp\":" + std::to_string(e.hp) + ",\"max\":" + std::to_string(e.max_hp);
        out += std::string(",\"hurt\":") + (e.hurt > 0 ? "1" : "0");
        out += std::string(",\"awake\":") + (e.awake ? "1" : "0");
        out += ",\"fx\":" + fnum(e.facing.x);
        out += ",\"role\":" + std::to_string(static_cast<int>(e.role));
        out += ",\"r\":" + fnum(e.radius);
        if (e.hidden) out += ",\"hid\":1";
        if (e.armour < 1.0f) out += ",\"guarded\":1";
        out += ",\"color\":";
        append_json_string(out, sp.color);
        out += ",\"glyph\":";
        append_json_string(out, std::string(1, sp.glyph));
        out += "}";
    }
    out += "]";

    out += ",\"bullets\":[";
    first = true;
    for (const Bullet& b : g_rt.bullets()) {
        if (!seen(b.pos)) continue;
        if (!first) out += ',';
        first = false;
        out += "[" + fnum(b.pos.x) + "," + fnum(b.pos.y) + "," + (b.hero ? "1" : "0") + "," +
               fnum(b.radius) + "," + (b.ember ? "1" : "0") + "]";
    }
    out += "]";

    out += ",\"telegraphs\":[";
    first = true;
    for (const Telegraph& t : g_rt.telegraphs()) {
        if (!first) out += ',';
        first = false;
        out += "[" + std::to_string(static_cast<int>(t.kind)) + "," + fnum(t.x) + "," + fnum(t.y) + "," +
               fnum(t.a) + "," + fnum(t.b) + "," + fnum(t.progress) + "]";
    }
    out += "]";

    out += ",\"hazards\":[";
    first = true;
    for (const Hazard& z : g_rt.hazards()) {
        if (!seen(z.pos)) continue;
        if (!first) out += ',';
        first = false;
        out += "[" + fnum(z.pos.x) + "," + fnum(z.pos.y) + "," + fnum(z.radius) + "," + fnum(z.life) + "]";
    }
    out += "]";

    out += ",\"pickups\":[";
    first = true;
    for (const Pickup& p : g_rt.pickups()) {
        if (!seen(p.pos)) continue;
        if (!first) out += ',';
        first = false;
        out += "[" + std::to_string(p.id) + "," + std::to_string(static_cast<int>(p.kind)) + "," +
               fnum(p.pos.x) + "," + fnum(p.pos.y) + "," + std::to_string(p.amount) + "]";
    }
    out += "]";

    out += ",\"events\":[";
    first = true;
    for (const Event& e : g_rt.take_events()) {
        if (!first) out += ',';
        first = false;
        out += "[" + std::to_string(static_cast<int>(e.kind)) + "," + fnum(e.x) + "," + fnum(e.y) +
               "," + std::to_string(e.amount) + "," + std::to_string(e.ref) + "," + fnum(e.a) + "," +
               fnum(e.b) + "]";
    }
    out += "]}";
    return out;
}

/// What does not change during a run: the weapons, the heroes to choose
/// from, and the guardians' names for their banners.
std::string build_rt_catalog_json() {
    using namespace nav::rt;
    std::string out = "{\"weapons\":[";
    bool first = true;
    for (const WeaponSpec& w : weapons()) {
        if (!first) out += ',';
        first = false;
        out += "{\"key\":";
        append_json_string(out, w.key);
        out += ",\"name\":";
        append_json_string(out, w.name.get(g_lang));
        out += std::string(",\"melee\":") + (w.melee ? "true" : "false");
        out += ",\"damage\":" + std::to_string(w.damage) + ",\"energy\":" + std::to_string(w.energy);
        out += ",\"pellets\":" + std::to_string(w.pellets);
        out += std::string(",\"pierce\":") + (w.pierce ? "true" : "false");
        out += ",\"interval\":" + fnum(w.interval) + ",\"reach\":" + fnum(w.reach) + "}";
    }
    out += "],\"heroes\":[";
    first = true;
    for (const HeroSpec& h : heroes()) {
        if (!first) out += ',';
        first = false;
        out += "{\"cls\":" + std::to_string(static_cast<int>(h.cls)) + ",\"key\":";
        append_json_string(out, nav::hero_sprite_key(h.cls));
        out += ",\"name\":";
        append_json_string(out, nav::class_info(h.cls).name.get(g_lang));
        out += ",\"hp\":" + std::to_string(h.hp) + ",\"shield\":" + std::to_string(h.shield) +
               ",\"energy\":" + std::to_string(h.energy) + ",\"speed\":" + fnum(h.speed);
        out += ",\"weapon\":" + std::to_string(weapon_index(h.weapon));
        out += ",\"perk\":";
        append_json_string(out, h.perk.get(g_lang));
        out += "}";
    }
    out += "],\"lastDepth\":" + std::to_string(kLastDepth) + "}";
    return out;
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void nav_rt_start(double seed, int hero_class) {
    if (hero_class < 0 || hero_class >= static_cast<int>(nav::HeroClass::Count)) hero_class = 0;
    g_rt.start(static_cast<std::uint64_t>(seed), static_cast<nav::HeroClass>(hero_class));
}

/// Runs `steps` fixed ticks with the same input. The page decides how many
/// have come due; capped here too, so a tab woken after a minute in the
/// background does not try to simulate the whole minute in one frame.
/// `interact` is a press, not a hold: it goes to the first of the steps only.
EMSCRIPTEN_KEEPALIVE void nav_rt_step(float move_x, float move_y, int fire, int interact, int steps) {
    nav::rt::Input in{move_x, move_y, fire != 0, interact != 0};
    if (steps > 8) steps = 8;
    for (int i = 0; i < steps; ++i) {
        g_rt.step(in);
        in.interact = false;
    }
}

/// Straight to a floor, keeping the hero as they are: the page's ?floor=
/// jump, for playtesting deep floors without playing down to them. With
/// `at_guardian`, the hero is set down a few steps from the floor's guardian.
EMSCRIPTEN_KEEPALIVE void nav_rt_floor(int depth, int at_guardian) {
    if (depth < 1) depth = 1;
    if (depth > nav::rt::kLastDepth) depth = nav::rt::kLastDepth;
    g_rt.enter_floor(depth);
    const nav::rt::Enemy* boss = g_rt.boss();
    if (!at_guardian || boss == nullptr) return;
    const nav::Map& m = g_rt.map();
    nav::rt::V best = g_rt.hero().pos;
    float best_miss = 1e9f;
    for (int y = 0; y < m.height(); ++y) {
        for (int x = 0; x < m.width(); ++x) {
            const nav::Tile t = m.at({x, y});
            if (t != nav::Tile::Floor || nav::Vec2{x, y} == g_rt.exit()) continue;
            const nav::rt::V c{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            const float miss = std::fabs(nav::rt::length(c - boss->pos) - 3.5f);
            // In sight of it: three steps away through a wall is another room.
            const nav::Vec2 at{static_cast<int>(boss->pos.x), static_cast<int>(boss->pos.y)};
            if (miss < best_miss && nav::has_line_of_sight(m, {x, y}, at, 8)) {
                best_miss = miss;
                best = c;
            }
        }
    }
    g_rt.mutable_hero().pos = best;
}

EMSCRIPTEN_KEEPALIVE char* nav_rt_catalog_json() { return to_c_string(build_rt_catalog_json()); }

EMSCRIPTEN_KEEPALIVE char* nav_rt_map_json() { return to_c_string(build_rt_map_json()); }

EMSCRIPTEN_KEEPALIVE char* nav_rt_frame_json() { return to_c_string(build_rt_frame_json()); }

}  // extern "C"

#ifndef __EMSCRIPTEN__
int main() { return 0; }  // lets the bindings compile natively for a sanity check
#endif
