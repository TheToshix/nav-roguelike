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

        {"kamennaya", Text{"Каменная баба", "Stone Idol"},        'K', "#9a9a9a", 45, 10,  8,  70, 6,  45,  5, 16,  5,
         AiMelee, Effect::Poison, 0, 0,
         Text{"Ожившее капище. Медлительна, но почти неуязвима.", "A shrine come alive. Slow, and nearly impervious."}},

        {"nav",       Text{"Навь", "Nav"},                        'n', "#7f8fd0", 26, 11,  4, 120, 10, 34,  6, 16,  7,
         AiMelee, Effect::Slow, 30, 5,
         Text{"Тень мёртвого мира. Холод идёт впереди неё.", "A shade of the dead world. Cold walks before it."}},

        {"koldun",    Text{"Колдун-отступник", "Renegade Sorcerer"},'C',"#d07acc", 24, 10,  2, 100, 10, 40,  6, 16,  5,
         AiRanged | AiSummoner, Effect::Confusion, 30, 4,
         Text{"Зовёт помощь и бьёт издали.", "Calls for help and strikes from range."}},

        {"zmey",      Text{"Огненный Змей", "Fire Serpent"},      'Z', "#e07b39", 40, 15,  4, 100, 10, 50,  7, 16,  6,
         AiRanged, Effect::Burn, 60, 4,
         Text{"Дышит огнём. Раны от него горят.", "It breathes fire. Its wounds keep burning."}},

        // --- Bosses: weight 0, placed by hand on a fixed floor ---------------
        {"viy",       Text{"Вий", "Viy"},                         'Y', "#e0c060", 70, 12,  4, 100, 12, 200, 4,  4,  0,
         AiMelee | AiBoss, Effect::Blind, 60, 8,
         Text{"Поднимите мне веки. Взгляд его — слепота.", "Lift up my eyelids. His gaze is blindness."}, /*phases=*/3},

        {"babayaga",  Text{"Баба-Яга", "Baba Yaga"},              'B', "#c04ac0", 130, 17, 6, 110, 12, 500, 8,  8,  0,
         AiRanged | AiSummoner | AiBoss, Effect::Confusion, 45, 6,
         Text{"Хозяйка избы на курьих ногах. Морочит и зовёт своих.", "Mistress of the hut on hen's legs. She confuses, and she calls her own."}, /*phases=*/3},

        {"koschei",   Text{"Кощей Бессмертный", "Koschei the Deathless"},'!', "#f0f0f0", 240, 24,  9, 110, 14, 1500, 12, 12, 0,
         AiMelee | AiRanged | AiSummoner | AiBoss, Effect::Freeze, 20, 2,
         Text{"Смерть его на конце иглы. Игла — в этом подземелье.", "His death is on a needle's point. The needle is in this dungeon."}, /*phases=*/3},

        // --- Пекло: the fourth belt's own -----------------------------------

        {"chert",     Text{"Чёрт", "Chert"},                      'c', "#c05a2a", 34, 14,  3, 130, 9,  46, 13, 16,  9,
         AiMelee | AiErratic, Effect::Burn, 45, 4,
         Text{"Быстр, нахален и горяч на руку.", "Quick, insolent, and hot to the touch."}},

        {"chugaister",Text{"Чугайстер", "Chugaister"},            'g', "#8a6a3a", 60, 16,  6,  90, 8,  70, 13, 16,  6,
         AiMelee, Effect::Confusion, 40, 5,
         Text{"Лесной великан. Затанцует до смерти.", "A forest giant. He will dance you to death."}},

        {"ognevik",   Text{"Огневик", "Cinderling"},              'v', "#f26a2a", 22,  9,  2, 135, 8,  40, 13, 16,  8,
         AiMelee | AiErratic, Effect::Burn, 30, 3,
         Text{"Мелкая огненная нечисть. Где прошла — там и горит, ещё несколько ходов.",
              "A small fiery vermin. Where it passes the floor keeps burning for a few turns."}},

        // --- Мини-стражи: один на середину каждого пояса ---------------------
        //
        // Weaker than a belt's master and shorter of phase, but built the same
        // way: a mechanic first, a health bar second.

        {"mara",      Text{"Мара", "Mara"},                       'M', "#9a7ac0", 38, 6,  2, 105, 7,  130, 3,  3,  0,
         AiRanged | AiBoss | AiMiniBoss, Effect::Confusion, 55, 6,
         Text{"Дух морока. Шепчет, и мир перестаёт слушаться.",
              "A spirit of delusion. She whispers, and the world stops obeying."}, /*phases=*/2},

        {"vodyanoy",  Text{"Водяной", "Vodyanoy"},                'W', "#3f9a9a", 90, 14,  5,  95, 9,  260, 7,  7,  0,
         AiMelee | AiSummoner | AiBoss | AiMiniBoss, Effect::Slow, 50, 5,
         Text{"Хозяин омута. В воде его не взять.",
              "Master of the deep pool. In the water he cannot be taken."}, /*phases=*/2},

        {"morozko",   Text{"Морозко", "Morozko"},                 'F', "#a8d8f0", 120, 18,  7, 100, 11, 420, 11, 11, 0,
         AiRanged | AiBoss | AiMiniBoss, Effect::Freeze, 45, 2,
         Text{"«Тепло ли тебе?» Отвечать надо быстро.",
              "\"Are you warm?\" You had better answer quickly."}, /*phases=*/2},

        {"polozh",    Text{"Огненный Полоз", "The Fiery Poloz"},  'P', "#f08030", 150, 21,  8, 110, 11, 600, 15, 15, 0,
         AiMelee | AiBoss | AiMiniBoss, Effect::Burn, 60, 5,
         Text{"Змей, что ходит сквозь камень. Там, где прошёл, камень плавится.",
              "A serpent that walks through stone. Where he passes, the stone melts."}, /*phases=*/2},

        // --- Змей Горыныч: три головы, три фазы ------------------------------
        //
        // The one boss whose phases are literal: a head falls at each threshold
        // and the survivors stop pacing themselves.

        {"gorynych",  Text{"Змей Горыныч", "Zmey Gorynych"},      'G', "#e0621c", 320, 26, 10, 105, 13, 2400, 16, 16, 0,
         AiMelee | AiRanged | AiBoss, Effect::Burn, 55, 6,
         Text{"Три головы, и каждая дышит огнём. Отрубишь одну — оставшиеся звереют.",
              "Three heads, and every one of them breathes fire. Take one and the rest go wild."},
         /*phases=*/3},

        // Баба-Яга's huts. Stationary, placed with her, and she is all but
        // invulnerable while any of them still stands.
        {"izbushka",  Text{"Избушка на курьих ножках", "Hut on Hen's Legs"}, 'A', "#b06a3a", 55, 7, 6, 100, 4, 60, 8, 8, 0,
         AiStationary | AiMelee, Effect::Poison, 0, 0,
         Text{"Пока стоит изба — хозяйку не взять.", "While the hut stands, its mistress cannot be touched."}},

        // --- Не всегда враг ------------------------------------------------
        //
        // The first creature in the game that is not automatically an enemy.
        // It never strikes first; pass it by without crowding or hitting it and
        // it leaves you a blessing. Hit it and it is a plain brute for the rest
        // of its short life.
        {"domovoy",   Text{"Домовой", "Domovoy"},                 'd', "#a8895f", 26,  9,  4,  90, 7,  16,  2, 12,  0,
         AiNeutral, Effect::Poison, 0, 0,
         Text{"Хозяин дома. Не тронешь — не тронет, а за уважение и отблагодарит.",
              "The keeper of the house. Leave it be and it leaves you be — and a little courtesy it repays."}},

        // --- Необязательный мини-босс ------------------------------------
        //
        // Weight 0: never rolled, placed by hand in a hidden room off a
        // mid-belt floor, guarding a charm. His song is the one attack in the
        // game that takes the player's turn away outright.
        {"kot_bayun", Text{"Кот Баюн", "Bayun the Cat"},          'f', "#9a8fb0", 120, 13,  5, 100, 11, 220,  6, 12,  0,
         AiRanged | AiBoss | AiMiniBoss, Effect::Sleep, 20, 2,
         Text{"Говорящий кот на железном столбе. Заводит песню — и слушающий засыпает.",
              "A speaking cat on an iron post. It begins a song, and the one who hears it sleeps."}, /*phases=*/2},

        // --- Не бой ------------------------------------------------------
        //
        // Never fights, always flees, bolts the instant it is touched. Corner
        // it or outrun it and it leaves a feather behind.
        {"zharptica", Text{"Жар-птица", "Firebird"},              'r', "#ffb020", 30,  1,  4, 130,  9,  25,  3, 14,  0,
         AiSkittish, Effect::Poison, 0, 0,
         Text{"Птица из огня и света. В руки не даётся — но кто изловчится, тому перо.",
              "A bird of fire and light. It will not be held — but a feather to whoever is quick enough."}},

        // --- Распутье: разовая встреча, не на этаже ---------------------
        //
        // Weight 0, placed by hand on the crossroads. He does not chase — he
        // sits on his oak and whistles, and the whistle stuns the hero and
        // hurls them back down the road. Kill him or slip past; either way it
        // happens once, at the very start.
        {"solovey",   Text{"Соловей-Разбойник", "Solovei the Brigand"}, 'w', "#5a6b3a", 60, 6, 4, 100, 12, 40, 0, 0, 0,
         AiStationary, Effect::Sleep, 0, 0,
         Text{"Сидит на девяти дубах и свищет. От свиста лес клонится, а человек — с ног.",
              "He sits in nine oaks and whistles. The forest bows to it, and a man is knocked flat."}},
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
    // The Домовой, the Жар-птица and Кот Баюн are all weight 0 above and never
    // roll here — they are placed by hand off their own random stream, so the
    // main dungeon sequence (and every floor below) is untouched by their
    // existence. This block is left as a guard in case one ever gains a weight.
    if ((s.ai & (AiNeutral | AiSkittish)) && boss_for_depth(depth) != nullptr) return 0;
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
// Zones
// ---------------------------------------------------------------------------

Zone zone_for_depth(int depth) {
    if (depth <= 0) return Zone::Rasputye;
    if (depth <= 4) return Zone::Pogost;
    if (depth <= 8) return Zone::Chernotop;
    if (depth <= 12) return Zone::Koshchei;
    return Zone::Peklo;
}

bool is_zone_entrance(int depth) {
    return depth == 1 || depth == 5 || depth == 9 || depth == 13;
}

const std::vector<Zone>& descending_belts() {
    static const std::vector<Zone> belts = {Zone::Pogost, Zone::Chernotop, Zone::Koshchei,
                                            Zone::Peklo};
    return belts;
}

int belt_last_depth(Zone zone) {
    int last = 0;
    for (int d = 1; d <= kMaxDepth; ++d)
        if (zone_for_depth(d) == zone) last = d;
    return last;
}

EventKind belt_event(Zone zone) {
    switch (zone) {
        case Zone::Chernotop: return EventKind::Flood;
        case Zone::Koshchei:  return EventKind::Blizzard;
        case Zone::Peklo:     return EventKind::Firestorm;
        default:              return EventKind::None;
    }
}

Text event_name(EventKind kind) {
    switch (kind) {
        case EventKind::Flood:     return Text{"Половодье", "The Flood"};
        case EventKind::Blizzard:  return Text{"Метель", "The Blizzard"};
        case EventKind::Firestorm: return Text{"Пожар", "The Firestorm"};
        case EventKind::None:      break;
    }
    return Text{"", ""};
}

Text event_note(EventKind kind) {
    switch (kind) {
        case EventKind::Flood:
            return Text{"Вода прибывает. Не задерживайся на низком месте.",
                        "The water is rising. Do not linger on low ground."};
        case EventKind::Blizzard:
            return Text{"Снег слепит — дальше вытянутой руки не видно.",
                        "The snow blinds you — nothing is clear past arm's reach."};
        case EventKind::Firestorm:
            return Text{"Огонь расходится. Он не станет ждать, пока ты решишься.",
                        "The fire is spreading. It will not wait for you to decide."};
        case EventKind::None:
            break;
    }
    return Text{"", ""};
}

const ZoneTheme& zone_theme(Zone zone) {
    static const ZoneTheme themes[] = {
        {Zone::Pogost,
         Text{"Погост", "The Boneyard"},
         Text{"Сухая земля и тёсаный камень. Здесь ещё пахнет ладаном.",
              "Dry earth and dressed stone. There is still incense in the air here."},
         "#7d6c58", "#453d34", "#4d7fa8",
         Text{"вода", "water"},
         /*caves=*/false, /*water=*/35, /*chasm=*/10, /*door=*/55, /*extra=*/0},

        {Zone::Chernotop,
         Text{"Чернотопь", "The Black Mire"},
         Text{"Стены кончились. Дальше — топь, коряги и вода по колено.",
              "The walls give out. Beyond is mire, deadwood and water to the knee."},
         "#6b8055", "#3d4a33", "#4a7d60",
         Text{"трясина", "mire"},
         /*caves=*/true, /*water=*/95, /*chasm=*/15, /*door=*/0, /*extra=*/2},

        {Zone::Koshchei,
         Text{"Кощеево царство", "Koschei's Kingdom"},
         Text{"Холод берёт за горло. Стены здесь белые, и это не камень.",
              "The cold takes you by the throat. The walls here are white, and they are not stone."},
         "#8e9aa8", "#333b45", "#5f7f9a",
         Text{"полынья", "black ice"},
         /*caves=*/false, /*water=*/15, /*chasm=*/55, /*door=*/40, /*extra=*/1},

        {Zone::Peklo,
         Text{"Пекло", "The Scorch"},
         Text{"Камень под ногой горячий. Где-то внизу дышит что-то очень большое.",
              "The stone underfoot is hot. Somewhere below, something very large is breathing."},
         "#a8593a", "#4a2c22", "#e0742a",
         Text{"огненная река", "the burning river"},
         /*caves=*/false, /*water=*/45, /*chasm=*/45, /*door=*/25, /*extra=*/3},

        // The crossroads is listed last because nothing generates it: its
        // colours exist only so the renderer has something to tint the
        // handmade room with.
        {Zone::Rasputye,
         Text{"Перекрёсток", "The Crossroads"},
         Text{"Три дороги, и все вниз. Собирайся — назад отсюда не ходят.",
              "Three roads, and all of them lead down. Make ready — nobody comes back this way."},
         "#7a6a4e", "#3c352a", "#4d7fa8",
         Text{"вода", "water"},
         /*caves=*/false, /*water=*/0, /*chasm=*/0, /*door=*/0, /*extra=*/0},
    };
    for (const auto& theme : themes)
        if (theme.zone == zone) return theme;
    return themes[0];
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
        {"volchiy_klyk", Text{"Волчий клык", "Wolf's Fang"},  ItemKind::Weapon, ')', 6, 6, 0, Text{"Половина «Волчьей снасти».", "Half of the Wolf's Rig."}},

        // --- Armour ----------------------------------------------------------
        {"rubaha",    Text{"Рубаха", "Shirt"},              ItemKind::Armor, '[',  1, 1, 10, Text{"Почти ничего.", "Barely anything."}},
        {"kozha",     Text{"Кожаный доспех", "Leather Armour"}, ItemKind::Armor, '[', 3, 2, 9, Text{"Не стесняет движений.", "Does not slow you down."}},
        {"kolchuga",  Text{"Кольчуга", "Chainmail"},        ItemKind::Armor, '[',  5, 4,  7, Text{"Держит удар.", "Holds against a blow."}},
        {"zertsalo",  Text{"Зерцало", "Plate Armour"},      ItemKind::Armor, '[',  8, 7,  5, Text{"Доспех воеводы.", "A warlord's plate."}},
        {"mantiya",   Text{"Мантия ведуна", "Sorcerer's Robe"}, ItemKind::Armor, '[', 2, 1, 6, Text{"+5 к запасу сил.", "+5 to your reserve of power."}},
        {"volchya_shkura", Text{"Волчья шкура", "Wolfskin"},    ItemKind::Armor, '[', 4, 6, 0, Text{"Половина «Волчьей снасти».", "Half of the Wolf's Rig."}},

        // --- Amulets ---------------------------------------------------------
        {"ob_zhizni", Text{"Оберег жизни", "Charm of Life"},   ItemKind::Amulet, '"', 12, 2, 6, Text{"+12 к здоровью.", "+12 maximum health."}},
        {"ob_sily",   Text{"Оберег силы", "Charm of Strength"},ItemKind::Amulet, '"',  3, 3, 6, Text{"+3 к удару.", "+3 attack."}},
        {"ob_zorko",  Text{"Оберег зоркости", "Charm of Sight"},ItemKind::Amulet,'"',  3, 2, 5, Text{"+3 к обзору.", "+3 sight radius."}},
        {"ob_yada",   Text{"Оберег от яда", "Charm of Antivenom"},ItemKind::Amulet,'"',1, 3, 5, Text{"Яд не берёт.", "Poison cannot touch you."}},
        {"ob_skoro",  Text{"Оберег скорости", "Charm of Haste"},ItemKind::Amulet,'"', 20, 5, 4, Text{"+20 к скорости.", "+20 speed."}},

        // Кот Баюн's collar, taken from around his neck. Weight 0: it is never
        // generated, only left behind by the cat in his hidden room.
        {"koshkin_glaz", Text{"Кошачий глаз", "The Cat's Eye"}, ItemKind::Amulet, '"', 4, 6, 0,
         Text{"Сон больше не берёт, и видно дальше.", "Sleep can no longer take you, and you see further."},
         GpNoSleep | GpSight},

        // --- Наборы ----------------------------------------------------------
        //
        // Four matched sets, each exactly one weapon, one armour and one amulet.
        // Three slots and three pieces is not an accident: completing a set
        // costs the hero every slot they have, so it is a decision about the
        // whole run rather than about one hand. Each piece is also worth
        // wearing alone — a set that is worthless until finished is a trap, not
        // a choice.

        // Обережный круг — против нечисти.
        {"rogatina",  Text{"Рогатина", "Boar Spear"},        ItemKind::Weapon, ')',  9, 4,  5,
         Text{"Против крупного бьёт вполтора раза.", "Half again as hard against the great ones."},
         GpVsBoss, GearSet::Oberezhny},
        {"sorochka",  Text{"Сорочка-неуязвимка", "Warding Shirt"}, ItemKind::Armor, '[', 4, 4, 5,
         Text{"Один удар за этаж уходит мимо.", "One blow a floor goes wide."},
         GpWard, GearSet::Oberezhny},
        {"nauzy",     Text{"Наузы", "Knot Charm"},           ItemKind::Amulet, '"',  2, 3,  5,
         Text{"Ни яд, ни огонь не берут.", "Neither venom nor fire takes hold."},
         GpNoPoison | GpNoBurn, GearSet::Oberezhny},

        // Ратный сбор — прямой бой.
        {"palitsa",   Text{"Палица", "War Club"},            ItemKind::Weapon, ')', 10, 5,  5,
         Text{"Иной удар сшибает с ног.", "Now and then a blow knocks them off their feet."},
         GpStun, GearSet::Ratny},
        {"bahterets", Text{"Бахтерец", "Scale Cuirass"},     ItemKind::Armor, '[',  7, 6,  5,
         Text{"Часть удара возвращается бьющему.", "Part of the blow goes back to whoever struck it."},
         GpThorns, GearSet::Ratny},
        {"grivna",    Text{"Гривна", "Torc"},                ItemKind::Amulet, '"',  4, 4,  5,
         Text{"+4 к удару, и золото само липнет к рукам.", "+4 attack, and gold sticks to your hands."},
         GpRichGold, GearSet::Ratny},

        // Навий сговор — за счёт чужой смерти.
        {"naviy_nozh",Text{"Навий нож", "Nav Knife"},        ItemKind::Weapon, ')',  7, 5,  5,
         Text{"Убитый отдаёт часть своего.", "The slain give up a little of what they had."},
         GpLifesteal, GearSet::Naviy},
        {"savan",     Text{"Саван", "Shroud"},               ItemKind::Armor, '[',  5, 5,  5,
         Text{"Раны затягиваются сами.", "Wounds close on their own."},
         GpRegen, GearSet::Naviy},
        {"zerkaltse", Text{"Зеркальце", "Little Mirror"},    ItemKind::Amulet, '"',  2, 5,  5,
         Text{"Заклятья обходятся дешевле.", "Spells come cheaper."},
         GpCheapSpell, GearSet::Naviy},

        // Ходовой припас — про ноги и глаза.
        {"klyuka",    Text{"Клюка", "Crook Staff"},          ItemKind::Weapon, ')',  5, 2,  6,
         Text{"Опора в дороге: +15 к скорости.", "Something to lean on: +15 speed."},
         GpQuick, GearSet::Hodovoy},
        {"lapti",     Text{"Лапти-скороходы", "Swift Bast Shoes"}, ItemKind::Armor, '[', 2, 2, 6,
         Text{"Лёгкие ноги: +15 к скорости.", "Light on your feet: +15 speed."},
         GpQuick, GearSet::Hodovoy},
        {"svecha",    Text{"Неугасимая свеча", "Unfailing Candle"}, ItemKind::Amulet, '"', 3, 2, 6,
         Text{"Светит и там, где света нет: +3 к обзору.", "It burns where there is no light: +3 sight."},
         GpSight, GearSet::Hodovoy},
    };
    return table;
}

const std::vector<GearSetInfo>& gear_set_table() {
    static const std::vector<GearSetInfo> table = {
        {GearSet::None, Text{"", ""}, Text{"", ""}},
        {GearSet::Oberezhny, Text{"Обережный круг", "The Warding Circle"},
         Text{"Ни морок, ни слепота не пристают.", "Neither delusion nor blindness will stick."}},
        {GearSet::Ratny, Text{"Ратный сбор", "The War Gathering"},
         Text{"Каждый удар достаёт всех, кто рядом.", "Every blow reaches everyone standing close."}},
        {GearSet::Naviy, Text{"Навий сговор", "The Pact with the Dead"},
         Text{"Убитый отдаёт здоровье и силы.", "The slain give up health and power."}},
        {GearSet::Hodovoy, Text{"Ходовой припас", "The Traveller's Kit"},
         Text{"Вода и пропасти больше не держат.", "Water and chasms no longer hold you."}},
    };
    return table;
}

const GearSetInfo& gear_set_info(GearSet set) {
    for (const auto& info : gear_set_table())
        if (info.set == set) return info;
    return gear_set_table()[0];
}

const std::vector<GearPair>& gear_pair_table() {
    static const std::vector<GearPair> table = {
        // Волчья снасть — a hunter's kit. Клык in one hand, шкура on the back;
        // each is a plain, honest piece alone, and the two together let the
        // slain feed you.
        {"volchiy_klyk", "volchya_shkura",
         Text{"Волчья снасть", "The Wolf's Rig"},
         Text{"Клык и шкура вместе: убитый отдаёт немного здоровья.",
              "Fang and hide together: the slain give up a little health."},
         GpLifesteal},
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
        Text{"Свиток снятия проклятья", "Scroll of Remove Curse"},
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
        Text{"Снимает проклятье с надетых вещей.", "Lifts the curse from what you are wearing."},
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
        Text{"свиток в чёрной ленте", "black-ribboned scroll"},
    };
    return t;
}

char item_glyph(const Item& it) {
    switch (it.kind) {
        case ItemKind::Needle: return '/';
        case ItemKind::Feather: return '{';
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

Text effect_name(Effect e) {
    switch (e) {
        case Effect::Poison:    return Text{"яд", "poison"};
        case Effect::Burn:      return Text{"огонь", "burning"};
        case Effect::Freeze:    return Text{"оковы", "frozen"};
        case Effect::Confusion: return Text{"морок", "confusion"};
        case Effect::Blind:     return Text{"слепота", "blindness"};
        case Effect::Haste:     return Text{"спешка", "haste"};
        case Effect::Slow:      return Text{"вязкость", "slowness"};
        case Effect::Regen:     return Text{"живая вода", "mending"};
        case Effect::Might:     return Text{"ярость", "might"};
        case Effect::Shield:    return Text{"оберег", "warding"};
        case Effect::Invisible: return Text{"тень", "unseen"};
        case Effect::Sleep:     return Text{"сон", "sleep"};
        case Effect::Count:     break;
    }
    return Text{"?", "?"};
}

Text item_name(const Item& it, const Identification& ident) {
    switch (it.kind) {
        case ItemKind::Needle:
            return Text{"Игла Кощеева", "Koschei's Needle"};
        case ItemKind::Feather:
            return Text{"Перо Жар-птицы", "Firebird's Feather"};
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
            Text name = gear[i].name;
            if (it.enchant != 0) {
                const std::string sign = it.enchant > 0 ? "+" : "";
                name = format(Text{"{} ({}{})", "{} ({}{})"}, name, Text(sign), num(it.enchant));
            }
            // A curse only shows once it has been felt — that is, once the piece
            // has been worn and refused to come off.
            if (it.cursed && it.identified) name = name + Text{" — проклято", " — cursed"};
            return name;
        }
    }
}

Text item_note(const Item& it, const Identification& ident) {
    switch (it.kind) {
        case ItemKind::Needle:
            return Text{"Сломай её — и Кощей станет смертен.",
                        "Break it, and Koschei becomes mortal."};
        case ItemKind::Feather:
            return Text{"Один раз удержит на этом свете. Само.",
                        "It will hold you in this world once. On its own."};
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
            if (it.cursed && it.identified)
                return Text{"Проклято. Снимается только свитком снятия проклятья.",
                            "Cursed. Only a Scroll of Remove Curse takes it off."};
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
         34, 0, 6, 2, 100, 8, 7, 0, 5, 5, TraitNone, "topor", "kozha"},

        {HeroClass::Vedun, Text{"Ведун", "Vedun"},
         Text{"Слаб в ближнем бою, но бьёт заклятьями издалека.",
              "Frail up close, but strikes with spells from a distance."},
         20, 20, 4, 0, 100, 9, 4, 5, 5, 8, TraitNone, "posokh", "mantiya"},

        {HeroClass::Tat, Text{"Тать", "Tat"},
         Text{"Быстр, уклончив, бьёт в уязвимое место. Хрупок, если попадут.",
              "Fast, evasive, strikes at the weak point. Fragile once caught."},
         24, 8, 5, 1, 115, 9, 5, 2, 25, 20, TraitNone, "nozh", "rubaha"},

        {HeroClass::Znahar, Text{"Знахарь", "Znahar"},
         Text{"Знает все зелья в лицо, и в его руках они крепче. Слаб в драке.",
              "Knows every potion by sight, and in his hands they work harder. Poor in a fight."},
         26, 12, 4, 1, 100, 8, 5, 3, 5, 10, TraitHerbalist, "nozh", "rubaha"},

        {HeroClass::Kuznets, Text{"Кузнец", "Kuznets"},
         Text{"Всякая вещь в его руках на ступень лучше, а капища берут с него вполовину.",
              "Every item is one grade better in his hands, and shrines charge him half."},
         30, 0, 5, 3, 95, 7, 6, 0, 10, 5, TraitSmith, "bulava", "kozha"},

        {HeroClass::Bogatyr, Text{"Богатырь", "Bogatyr"},
         Text{"Медлителен, но одним замахом достаёт всех, кто стоит рядом.",
              "Slow, but a single swing reaches everything standing around him."},
         42, 0, 7, 3, 85, 7, 8, 0, 5, 0, TraitCleave, "topor", "kolchuga"},
    };
    return table;
}

bool class_has(HeroClass c, ClassTrait trait) {
    return (class_info(c).traits & static_cast<std::uint32_t>(trait)) != 0;
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
    static const std::vector<std::pair<Spell, int>> znahar = {
        {Spell::Heal, 1}, {Spell::Ward, 5},
    };
    static const std::vector<std::pair<Spell, int>> none = {};

    switch (c) {
        case HeroClass::Vedun:   return vedun;
        case HeroClass::Tat:     return tat;
        case HeroClass::Znahar:  return znahar;
        case HeroClass::Kuznets: return none;
        case HeroClass::Bogatyr: return vityaz;   // gains Ward at level 6, like the Vityaz
        default:                 return vityaz;
    }
}

// ---------------------------------------------------------------------------
// Bosses and progression
// ---------------------------------------------------------------------------

const std::vector<BossPlacement>& boss_table() {
    static const std::vector<BossPlacement> table = {
        // Every belt holds two: a lesser guardian halfway down and its master
        // at the bottom. Four floors of ordinary monsters and one fight made
        // each belt read as a corridor with a door at the end of it.
        {3, "mara"},      {4, "viy"},
        {7, "vodyanoy"},  {8, "babayaga"},
        {11, "morozko"},  {12, "koschei"},
        {15, "polozh"},   {16, "gorynych"},
    };
    return table;
}

const std::vector<BossPhaseLine>& boss_phase_table() {
    static const std::vector<BossPhaseLine> table = {
        {"mara", 2, Text{"Мара расплывается — теперь их несколько, и все шепчут.",
                         "Mara blurs — there are several of her now, and all of them whisper."}},
        {"vodyanoy", 2, Text{"Водяной уходит под воду. Пол под ногами становится мокрым.",
                             "Vodyanoy sinks under. The floor underfoot turns wet."}},
        {"morozko", 2, Text{"«Тепло ли тебе, девица?» — воздух звенит от холода.",
                            "\"Are you warm, girl?\" — the air rings with cold."}},
        {"polozh", 2, Text{"Полоз уходит в камень. Пол дрожит — он идёт под тобой.",
                           "The Poloz slips into the stone. The floor shakes — he is moving beneath you."}},

        {"kot_bayun", 2, Text{"Кот Баюн мурлычет глубже. Теперь и подойти не даёт — усыпляет вплотную.",
                              "Bayun's purr drops lower. He will not even let you close now — the song reaches at arm's length."}},

        {"viy", 2, Text{"Вий перестаёт ждать. Веки поднимаются чаще.",
                        "Viy stops waiting. The eyelids come up sooner now."}},
        {"viy", 3, Text{"Веки больше не опускаются. Вий смотрит не мигая.",
                        "The eyelids do not come down again. Viy stares without blinking."}},

        {"babayaga", 2, Text{"Баба-Яга садится в ступу. Теперь она быстрее тебя.",
                             "Baba Yaga climbs into her mortar. She is faster than you now."}},
        {"babayaga", 3, Text{"«Изба, встань!» — из земли поднимается новая изба.",
                             "\"Hut, stand up!\" — a fresh hut rises out of the ground."}},

        {"koschei", 2, Text{"Кощей перестаёт бить и начинает тянуть: его раны затягиваются твоими.",
                            "Koschei stops striking and starts drawing: his wounds close with yours."}},
        {"koschei", 3, Text{"Кощей зовёт своих. Из стен выходит навь.",
                            "Koschei calls his own. The dead come out of the walls."}},

        {"gorynych", 2, Text{"Одна голова падает. Две оставшиеся заходятся рёвом.",
                             "One head falls. The other two go into a rage."}},
        {"gorynych", 3, Text{"Вторая голова падает. Последняя больше не бережёт огонь.",
                             "The second head falls. The last one stops saving its fire."}},
    };
    return table;
}

Text boss_phase_line(const char* key, int phase) {
    for (const auto& line : boss_phase_table())
        if (std::strcmp(line.species_key, key) == 0 && line.phase == phase) return line.line;
    return Text{"", ""};
}

const char* boss_for_depth(int depth) {
    for (const auto& b : boss_table())
        if (b.depth == depth) return b.species_key;
    return nullptr;
}

int boss_depth(const char* species_key) {
    for (const auto& b : boss_table())
        if (std::strcmp(b.species_key, species_key) == 0) return b.depth;
    return -1;
}

int xp_for_level(int level) {
    if (level <= 1) return 0;
    const int n = level - 1;
    return 25 * n * n + 15 * n;
}

}  // namespace nav
