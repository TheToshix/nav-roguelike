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
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "nav/game.hpp"

#ifdef __EMSCRIPTEN__
#  include <emscripten/emscripten.h>
#else
#  define EMSCRIPTEN_KEEPALIVE
#endif

namespace {

nav::Game g_game;
nav::Lang g_lang = nav::Lang::Ru;

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
            colors += palette.index_of(cell.color);
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
    // The belt's three colours. The sprite renderer tints one neutral set of
    // stones with these, which is why the crypts, the mire and the frozen
    // kingdom do not need three copies of every tile.
    {
        const ZoneTheme& theme = zone_theme_for_depth(g.depth());
        append_field(out, "tintWall", std::string(theme.wall_color), first);
        append_field(out, "tintFloor", std::string(theme.floor_color), first);
        append_field(out, "tintLiquid", std::string(theme.liquid_color), first);
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
    if (type < 0 || type > static_cast<int>(nav::ActionType::Pray)) return 0;
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
    for (nav::Zone z : {nav::Zone::Pogost, nav::Zone::Chernotop, nav::Zone::Koshchei}) {
        const auto& theme = nav::zone_theme(z);
        if (!zone_first) out += ',';
        zone_first = false;
        out += "{\"name\":";
        append_json_string(out, theme.name.get(g_lang));
        out += ",\"blurb\":";
        append_json_string(out, theme.arrival.get(g_lang));
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

EMSCRIPTEN_KEEPALIVE void nav_free_cstr(char* p) { std::free(p); }

EMSCRIPTEN_KEEPALIVE const char* nav_version() { return "1.0.0"; }

}  // extern "C"

#ifndef __EMSCRIPTEN__
int main() { return 0; }  // lets the bindings compile natively for a sanity check
#endif
