// SPDX-License-Identifier: MIT
//
// All static game content lives here: the bestiary, the loot tables, the hero
// classes and the spell list. Keeping it in one translation unit means the
// balance of the game can be reviewed in a single diff, and it keeps the rules
// code free of magic numbers.
#include "nav/data.hpp"

#include <cstring>
#include <utility>

namespace nav {

// ---------------------------------------------------------------------------
// Bestiary
// ---------------------------------------------------------------------------

const std::vector<Species>& bestiary() {
    static const std::vector<Species> table = {
        // key            name (ru / en)                          gl  colour     hp atk def spd sig  xp  d0  d1  w  ai
        {"anchutka",  Text{"Анчутка", "Anchutka"},                'a', "#8a9a5b",  6,  3,  0, 110, 6,   4,  1,  4, 10,
         AiMelee | AiErratic, Effect::Poison, 0, 0,
         Text{"Мелкий бес. Суетлив и труслив, но кусается.", "A petty imp. Skittish, but it bites."}},

        {"upyr",      Text{"Упырь", "Upyr"},                      'u', "#b05353", 12,  5,  1, 100, 7,   8,  1,  6, 12,
         AiMelee, Effect::Poison, 0, 0,
         Text{"Неупокойник. Идёт на живое тепло.", "The restless dead. It walks towards warmth."}},

        {"kikimora",  Text{"Кикимора", "Kikimora"},               'k', "#6f9f8f",  8,  4,  0, 100, 8,   7,  1,  5,  8,
         AiRanged | AiCoward, Effect::Poison, 0, 0,
         Text{"Швыряет тем, что подвернётся, и прячется.", "Throws whatever is at hand, then hides."}},

        {"bolotnik",  Text{"Болотник", "Bolotnik"},               'b', "#4e7a4e", 18,  6,  3,  80, 6,  14,  2,  6,  7,
         AiMelee, Effect::Poison, 40, 6,
         Text{"Тяжёл и ядовит, но нетороплив.", "Heavy and venomous, but slow."}},

        {"mavka",     Text{"Мавка", "Mavka"},                     'm', "#c58fc5", 14,  6,  1, 110, 8,  12,  2,  7,  9,
         AiMelee, Effect::Slow, 35, 4,
         Text{"Тянет силы прикосновением.", "Its touch drains the strength from your limbs."}},

        {"poludnitsa",Text{"Полудница", "Poludnitsa"},            'p', "#d9c17a", 16,  7,  1, 100, 9,  18,  3,  8,  7,
         AiRanged, Effect::Confusion, 40, 5,
         Text{"Полуденный морок. Мутит разум на расстоянии.", "A noon wraith. It clouds the mind from afar."}},

        {"volkolak",  Text{"Волколак", "Volkolak"},               'V', "#9b7b4b", 22,  9,  2, 130, 9,  20,  3,  9,  8,
         AiMelee, Effect::Poison, 0, 0,
         Text{"Оборотень. Быстр и бьёт насмерть.", "A werewolf. Fast, and it hits to kill."}},

        {"aspid",     Text{"Аспид", "Aspid"},                     's', "#7ac77a", 20,  8,  2, 100, 9,  24,  4, 11,  8,
         AiRanged, Effect::Poison, 50, 6,
         Text{"Крылатый змей. Плюётся ядом.", "A winged serpent. It spits venom."}},

        {"likho",     Text{"Лихо Одноглазое", "Likho"},           'L', "#8b5a8b", 30, 12,  3, 100, 8,  30,  4, 10,  7,
         AiMelee, Effect::Blind, 25, 5,
         Text{"Само несчастье. Встреча с ним ослепляет.", "Misfortune itself. Meeting its eye blinds you."}},

        {"shishiga",  Text{"Шишига", "Shishiga"},                 'h', "#a8a8c0", 18,  8,  2, 140, 8,  22,  5, 10,  6,
         AiMelee | AiCoward, Effect::Poison, 0, 0,
         Text{"Юркая нежить. Бьёт и отскакивает.", "Nimble undead. It strikes and darts away."}},

        {"kamennaya", Text{"Каменная баба", "Stone Idol"},        'K', "#9a9a9a", 45, 10,  8,  70, 6,  45,  5, 12,  5,
         AiMelee, Effect::Poison, 0, 0,
         Text{"Ожившее капище. Медлительна, но почти неуязвима.", "A shrine come alive. Slow, and nearly impervious."}},

        {"nav",       Text{"Навь", "Nav"},                        'n', "#7f8fd0", 26, 11,  4, 120, 10, 34,  6, 12,  7,
         AiMelee, Effect::Slow, 30, 5,
         Text{"Тень мёртвого мира. Холод идёт впереди неё.", "A shade of the dead world. Cold walks before it."}},

        {"koldun",    Text{"Колдун-отступник", "Renegade Sorcerer"},'C',"#d07acc", 24, 10,  2, 100, 10, 40,  6, 12,  5,
         AiRanged | AiSummoner, Effect::Confusion, 30, 4,
         Text{"Зовёт помощь и бьёт издали.", "Calls for help and strikes from range."}},

        {"zmey",      Text{"Огненный Змей", "Fire Serpent"},      'Z', "#e07b39", 40, 15,  4, 100, 10, 50,  7, 12,  6,
         AiRanged, Effect::Burn, 60, 4,
         Text{"Дышит огнём. Раны от него горят.", "It breathes fire. Its wounds keep burning."}},

        // --- Bosses: weight 0, placed by hand on a fixed floor ---------------
        {"viy",       Text{"Вий", "Viy"},                         'Y', "#e0c060", 70, 12,  4, 100, 12, 200, 4,  4,  0,
         AiMelee | AiBoss, Effect::Blind, 60, 8,
         Text{"Поднимите мне веки. Взгляд его — слепота.", "Lift up my eyelids. His gaze is blindness."}},

        {"babayaga",  Text{"Баба-Яга", "Baba Yaga"},              'B', "#c04ac0", 130, 17, 6, 110, 12, 500, 8,  8,  0,
         AiRanged | AiSummoner | AiBoss, Effect::Confusion, 45, 6,
         Text{"Хозяйка избы на курьих ногах. Морочит и зовёт своих.", "Mistress of the hut on hen's legs. She confuses, and she calls her own."}},

        {"koschei",   Text{"Кощей Бессмертный", "Koschei the Deathless"},'!', "#f0f0f0", 240, 24,  9, 110, 14, 1500, 12, 12, 0,
         AiMelee | AiRanged | AiSummoner | AiBoss, Effect::Freeze, 20, 2,
         Text{"Смерть его на конце иглы. Игла — в этом подземелье.", "His death is on a needle's point. The needle is in this dungeon."}},
    };
    return table;
}

int species_index(const char* key) {
    const auto& table = bestiary();
    for (std::size_t i = 0; i < table.size(); ++i)
        if (std::strcmp(table[i].key, key) == 0) return static_cast<int>(i);
    return -1;
}

int spawn_weight(const Species& s, int depth) {
    if (s.weight <= 0) return 0;                       // bosses never roll
    if (depth < s.min_depth || depth > s.max_depth) return 0;
    // Species are commonest in the middle of their depth window and taper off
    // towards its edges, so floors feel distinct instead of uniformly random.
    const int span = s.max_depth - s.min_depth;
    if (span <= 0) return s.weight;
    const int from_start = depth - s.min_depth;
    const int from_end = s.max_depth - depth;
    const int edge = from_start < from_end ? from_start : from_end;
    return s.weight * (2 + edge) / 2;
}

// ---------------------------------------------------------------------------
// Gear
// ---------------------------------------------------------------------------

const std::vector<GearTemplate>& gear_table() {
    static const std::vector<GearTemplate> table = {
        // --- Weapons ---------------------------------------------------------
        {"nozh",      Text{"Нож", "Knife"},                 ItemKind::Weapon, ')',  2, 1, 10, Text{"Лёгок и быстр.", "Light and quick."}},
        {"mech",      Text{"Меч", "Sword"},                 ItemKind::Weapon, ')',  5, 2,  9, Text{"Надёжная сталь.", "Dependable steel."}},
        {"topor",     Text{"Топор", "Axe"},                 ItemKind::Weapon, ')',  6, 3,  8, Text{"Рубит и доспех.", "Bites through armour."}},
        {"bulava",    Text{"Булава", "Mace"},               ItemKind::Weapon, ')',  7, 4,  7, Text{"Дробит кости.", "Shatters bone."}},
        {"kisten",    Text{"Кистень", "Flail"},             ItemKind::Weapon, ')',  8, 5,  6, Text{"Достаёт из-за щита.", "Reaches past a shield."}},
        {"sekira",    Text{"Секира", "Great Axe"},          ItemKind::Weapon, ')', 11, 7,  5, Text{"Тяжела, но страшна.", "Heavy, and terrible."}},
        {"posokh",    Text{"Посох ведуна", "Sorcerer's Staff"}, ItemKind::Weapon, ')', 3, 1, 6, Text{"+5 к запасу сил.", "+5 to your reserve of power."}},
        {"kladenets", Text{"Меч-кладенец", "Sword Kladenets"}, ItemKind::Weapon, ')', 15, 10, 3, Text{"Сказочный клинок.", "A blade out of legend."}},

        // --- Armour ----------------------------------------------------------
        {"rubaha",    Text{"Рубаха", "Shirt"},              ItemKind::Armor, '[',  1, 1, 10, Text{"Почти ничего.", "Barely anything."}},
        {"kozha",     Text{"Кожаный доспех", "Leather Armour"}, ItemKind::Armor, '[', 3, 2, 9, Text{"Не стесняет движений.", "Does not slow you down."}},
        {"kolchuga",  Text{"Кольчуга", "Chainmail"},        ItemKind::Armor, '[',  5, 4,  7, Text{"Держит удар.", "Holds against a blow."}},
        {"zertsalo",  Text{"Зерцало", "Plate Armour"},      ItemKind::Armor, '[',  8, 7,  5, Text{"Доспех воеводы.", "A warlord's plate."}},
        {"mantiya",   Text{"Мантия ведуна", "Sorcerer's Robe"}, ItemKind::Armor, '[', 2, 1, 6, Text{"+5 к запасу сил.", "+5 to your reserve of power."}},

        // --- Amulets ---------------------------------------------------------
        {"ob_zhizni", Text{"Оберег жизни", "Charm of Life"},   ItemKind::Amulet, '"', 12, 2, 6, Text{"+12 к здоровью.", "+12 maximum health."}},
        {"ob_sily",   Text{"Оберег силы", "Charm of Strength"},ItemKind::Amulet, '"',  3, 3, 6, Text{"+3 к удару.", "+3 attack."}},
        {"ob_zorko",  Text{"Оберег зоркости", "Charm of Sight"},ItemKind::Amulet,'"',  3, 2, 5, Text{"+3 к обзору.", "+3 sight radius."}},
        {"ob_yada",   Text{"Оберег от яда", "Charm of Antivenom"},ItemKind::Amulet,'"',1, 3, 5, Text{"Яд не берёт.", "Poison cannot touch you."}},
        {"ob_skoro",  Text{"Оберег скорости", "Charm of Haste"},ItemKind::Amulet,'"', 20, 5, 4, Text{"+20 к скорости.", "+20 speed."}},
    };
    return table;
}

Slot item_slot(const Item& it) {
    switch (it.kind) {
        case ItemKind::Weapon: return Slot::Weapon;
        case ItemKind::Armor:  return Slot::Armor;
        case ItemKind::Amulet: return Slot::Amulet;
        default: return Slot::None;
    }
}

// ---------------------------------------------------------------------------
// Consumables
// ---------------------------------------------------------------------------

const std::vector<Text>& potion_names() {
    static const std::vector<Text> t = {
        Text{"Зелье исцеления", "Potion of Healing"},
        Text{"Зелье большого исцеления", "Potion of Greater Healing"},
        Text{"Зелье силы духа", "Potion of Mana"},
        Text{"Зелье ярости", "Potion of Might"},
        Text{"Зелье скорости", "Potion of Haste"},
        Text{"Зелье живой воды", "Potion of Regeneration"},
        Text{"Зелье отравы", "Potion of Venom"},
        Text{"Зелье морока", "Potion of Bewilderment"},
    };
    return t;
}

const std::vector<Text>& potion_notes() {
    static const std::vector<Text> t = {
        Text{"Возвращает часть здоровья.", "Restores some health."},
        Text{"Возвращает много здоровья.", "Restores a great deal of health."},
        Text{"Восполняет запас сил.", "Restores your reserve of power."},
        Text{"Ненадолго усиливает удар.", "Briefly strengthens your blows."},
        Text{"Ненадолго ускоряет.", "Briefly quickens you."},
        Text{"Затягивает раны со временем.", "Closes wounds over time."},
        Text{"Травит выпившего.", "Poisons the one who drinks it."},
        Text{"Мутит разум.", "Clouds the mind."},
    };
    return t;
}

const std::vector<Text>& potion_appearances() {
    static const std::vector<Text> t = {
        Text{"мутное зелье", "cloudy potion"},
        Text{"багровое зелье", "crimson potion"},
        Text{"искристое зелье", "sparkling potion"},
        Text{"дымчатое зелье", "smoky potion"},
        Text{"тягучее зелье", "viscous potion"},
        Text{"ледяное зелье", "ice-cold potion"},
        Text{"золотистое зелье", "golden potion"},
        Text{"чёрное зелье", "black potion"},
    };
    return t;
}

const std::vector<Text>& scroll_names() {
    static const std::vector<Text> t = {
        Text{"Свиток огненного шара", "Scroll of Fireball"},
        Text{"Свиток молнии", "Scroll of Lightning"},
        Text{"Свиток стужи", "Scroll of Frost"},
        Text{"Свиток слепоты", "Scroll of Blinding"},
        Text{"Свиток переноса", "Scroll of Teleportation"},
        Text{"Свиток прозрения", "Scroll of Magic Mapping"},
        Text{"Свиток познания", "Scroll of Identify"},
        Text{"Свиток зова", "Scroll of Summoning"},
    };
    return t;
}

const std::vector<Text>& scroll_notes() {
    static const std::vector<Text> t = {
        Text{"Жжёт всех вокруг.", "Burns everything nearby."},
        Text{"Бьёт всех в округе молнией.", "Strikes everything near with lightning."},
        Text{"Сковывает всех вокруг.", "Freezes everything nearby."},
        Text{"Ослепляет всех вокруг.", "Blinds everything nearby."},
        Text{"Переносит в случайное место этажа.", "Moves you elsewhere on this floor."},
        Text{"Открывает карту этажа.", "Reveals the map of this floor."},
        Text{"Опознаёт все вещи в котомке.", "Identifies everything in your pack."},
        Text{"Созывает нечисть. Не читайте это.", "Calls the unclean. Do not read this."},
    };
    return t;
}

const std::vector<Text>& scroll_appearances() {
    static const std::vector<Text> t = {
        Text{"свиток с вязью", "scroll of woven script"},
        Text{"свиток с рунами", "runed scroll"},
        Text{"пятнистый свиток", "stained scroll"},
        Text{"обгорелый свиток", "scorched scroll"},
        Text{"ветхий свиток", "brittle scroll"},
        Text{"свиток, шитый нитью", "thread-bound scroll"},
        Text{"свиток с печатью", "sealed scroll"},
        Text{"безымянный свиток", "nameless scroll"},
    };
    return t;
}

char item_glyph(const Item& it) {
    switch (it.kind) {
        case ItemKind::Potion: return '!';
        case ItemKind::Scroll: return '?';
        case ItemKind::Food:   return '%';
        case ItemKind::Gold:   return '$';
        default: {
            const auto& gear = gear_table();
            const std::size_t i = static_cast<std::size_t>(it.subtype);
            return i < gear.size() ? gear[i].glyph : '*';
        }
    }
}

Text item_name(const Item& it, const Identification& ident) {
    switch (it.kind) {
        case ItemKind::Gold:
            return format(Text{"золото ({})", "gold ({})"}, num(it.count));
        case ItemKind::Food:
            return Text{"краюха хлеба", "crust of bread"};
        case ItemKind::Potion: {
            const std::size_t i = static_cast<std::size_t>(it.subtype);
            if (ident.knows(ItemKind::Potion, it.subtype) || it.identified)
                return i < potion_names().size() ? potion_names()[i] : Text{"зелье", "potion"};
            const std::size_t look = i < ident.potion_look.size()
                                         ? static_cast<std::size_t>(ident.potion_look[i]) : i;
            return look < potion_appearances().size() ? potion_appearances()[look]
                                                      : Text{"зелье", "potion"};
        }
        case ItemKind::Scroll: {
            const std::size_t i = static_cast<std::size_t>(it.subtype);
            if (ident.knows(ItemKind::Scroll, it.subtype) || it.identified)
                return i < scroll_names().size() ? scroll_names()[i] : Text{"свиток", "scroll"};
            const std::size_t look = i < ident.scroll_look.size()
                                         ? static_cast<std::size_t>(ident.scroll_look[i]) : i;
            return look < scroll_appearances().size() ? scroll_appearances()[look]
                                                      : Text{"свиток", "scroll"};
        }
        default: {
            const auto& gear = gear_table();
            const std::size_t i = static_cast<std::size_t>(it.subtype);
            if (i >= gear.size()) return Text{"вещь", "item"};
            if (it.enchant == 0) return gear[i].name;
            const std::string sign = it.enchant > 0 ? "+" : "";
            return format(Text{"{} ({}{})", "{} ({}{})"},
                          gear[i].name, Text(sign), num(it.enchant));
        }
    }
}

Text item_note(const Item& it, const Identification& ident) {
    switch (it.kind) {
        case ItemKind::Food: return Text{"Утоляет голод.", "Staves off hunger."};
        case ItemKind::Gold: return Text{"", ""};
        case ItemKind::Potion: {
            if (!ident.knows(ItemKind::Potion, it.subtype) && !it.identified)
                return Text{"Неизвестно, что это.", "You do not know what this is."};
            const std::size_t i = static_cast<std::size_t>(it.subtype);
            return i < potion_notes().size() ? potion_notes()[i] : Text{"", ""};
        }
        case ItemKind::Scroll: {
            if (!ident.knows(ItemKind::Scroll, it.subtype) && !it.identified)
                return Text{"Неизвестно, что это.", "You do not know what this is."};
            const std::size_t i = static_cast<std::size_t>(it.subtype);
            return i < scroll_notes().size() ? scroll_notes()[i] : Text{"", ""};
        }
        default: {
            const auto& gear = gear_table();
            const std::size_t i = static_cast<std::size_t>(it.subtype);
            return i < gear.size() ? gear[i].note : Text{"", ""};
        }
    }
}

// ---------------------------------------------------------------------------
// Hero classes
// ---------------------------------------------------------------------------

const std::vector<ClassTemplate>& class_table() {
    static const std::vector<ClassTemplate> table = {
        {HeroClass::Vityaz, Text{"Витязь", "Vityaz"},
         Text{"Много здоровья и брони, никакого колдовства. Прямой путь вниз.",
              "Deep reserves of health and armour, no magic at all. The straight road down."},
         34, 0, 6, 2, 100, 8, 7, 0, 5, 5, "topor", "kozha"},

        {HeroClass::Vedun, Text{"Ведун", "Vedun"},
         Text{"Слаб в ближнем бою, но бьёт заклятьями издалека.",
              "Frail up close, but strikes with spells from a distance."},
         20, 20, 4, 0, 100, 9, 4, 5, 5, 8, "posokh", "mantiya"},

        {HeroClass::Tat, Text{"Тать", "Tat"},
         Text{"Быстр, уклончив, бьёт в уязвимое место. Хрупок, если попадут.",
              "Fast, evasive, strikes at the weak point. Fragile once caught."},
         24, 8, 5, 1, 115, 9, 5, 2, 25, 20, "nozh", "rubaha"},
    };
    return table;
}

const ClassTemplate& class_info(HeroClass c) {
    for (const auto& t : class_table())
        if (t.cls == c) return t;
    return class_table().front();
}

// ---------------------------------------------------------------------------
// Spells
// ---------------------------------------------------------------------------

const std::vector<SpellTemplate>& spell_table() {
    static const std::vector<SpellTemplate> table = {
        {Spell::FireArrow, Text{"Огненная стрела", "Fire Arrow"},
         Text{"Урон одной цели, поджигает.", "Damages one target and sets it burning."},  4, 7,  8, true},
        {Spell::IceBind,   Text{"Ледяные оковы", "Ice Bind"},
         Text{"Сковывает цель на несколько ходов.", "Freezes a target for several turns."}, 5, 6,  4, true},
        {Spell::Lightning, Text{"Молния", "Lightning"},
         Text{"Пробивает всех на линии до цели.", "Pierces every creature on the line to the target."}, 7, 8, 12, true},
        {Spell::Heal,      Text{"Исцеление", "Healing"},
         Text{"Затягивает собственные раны.", "Closes your own wounds."},                 6, 0, 12, false},
        {Spell::Morok,     Text{"Морок", "Bewilderment"},
         Text{"Мутит разум всем вокруг.", "Clouds the mind of everything nearby."},        8, 4,  6, false},
        {Spell::Ward,      Text{"Оберег", "Ward"},
         Text{"Временно поднимает защиту.", "Temporarily raises your defence."},           5, 0,  4, false},
    };
    return table;
}

const SpellTemplate& spell_info(Spell s) {
    const auto& table = spell_table();
    const std::size_t i = static_cast<std::size_t>(s);
    return i < table.size() ? table[i] : table.front();
}

const std::vector<std::pair<Spell, int>>& class_spells(HeroClass c) {
    static const std::vector<std::pair<Spell, int>> vityaz = {
        {Spell::Ward, 6},
    };
    static const std::vector<std::pair<Spell, int>> vedun = {
        {Spell::FireArrow, 1}, {Spell::Heal, 2},  {Spell::Ward, 4},
        {Spell::IceBind, 5},   {Spell::Lightning, 7}, {Spell::Morok, 9},
    };
    static const std::vector<std::pair<Spell, int>> tat = {
        {Spell::Morok, 5}, {Spell::IceBind, 8},
    };
    switch (c) {
        case HeroClass::Vedun: return vedun;
        case HeroClass::Tat:   return tat;
        default:               return vityaz;
    }
}

// ---------------------------------------------------------------------------
// Bosses and progression
// ---------------------------------------------------------------------------

const std::vector<BossPlacement>& boss_table() {
    static const std::vector<BossPlacement> table = {
        {4, "viy"},
        {8, "babayaga"},
        {12, "koschei"},
    };
    return table;
}

const char* boss_for_depth(int depth) {
    for (const auto& b : boss_table())
        if (b.depth == depth) return b.species_key;
    return nullptr;
}

int xp_for_level(int level) {
    if (level <= 1) return 0;
    const int n = level - 1;
    return 25 * n * n + 15 * n;
}

}  // namespace nav
