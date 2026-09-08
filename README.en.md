<div align="center">

# Nav

**A turn-based roguelike drawn from Slavic folklore.**
The game core is written in C++ and builds for both a terminal and the browser, via WebAssembly.

[![CI](https://github.com/TheToshix/nav-roguelike/actions/workflows/ci.yml/badge.svg)](https://github.com/TheToshix/nav-roguelike/actions/workflows/ci.yml)
[![Pages](https://github.com/TheToshix/nav-roguelike/actions/workflows/pages.yml/badge.svg)](https://github.com/TheToshix/nav-roguelike/actions/workflows/pages.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)](CMakeLists.txt)
[![Tests](https://img.shields.io/badge/tests-285-4c9a5a)](tests/)
[![Coverage](https://img.shields.io/badge/coverage-94%25-4c9a5a)](docs/TEST_PLAN.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

### ▶ [Play in your browser](https://thetoshix.github.io/nav-roguelike/)

[Русский](README.md) · [Architecture](docs/ARCHITECTURE.md) · [Test plan](docs/TEST_PLAN.md) · [Test cases](docs/TEST_CASES.md) · [Bug reports](docs/BUG_REPORTS.md)

<img src="docs/media/gameplay.gif" alt="Gameplay" width="800">

</div>

---

## What it is

Twelve floors down into Nav, the underworld of Slavic folk tales. Death is final: a save is a
pause, not a spare life.

Every dungeon is generated from a seed. The same seed always produces the same floor, the same
creatures and the same loot, so any find — and any bug — can be reproduced from a single string.

### Three belts, three guardians

The descent is cut into three parts of four floors. Each has its own generator, palette and
hazards, and its own master at the bottom — so twelve floors read as a journey rather than as
repetition.

| Belt | Depth | What is there | Guardian |
|---|---|---|---|
| **The Boneyard** | 1–4 | Dry crypts and corridors, doors, the odd pool | **Viy** |
| **The Black Mire** | 5–8 | Caves instead of rooms, water nearly everywhere, no doors at all | **Baba Yaga** |
| **Koschei's Kingdom** | 9–12 | Cold halls with chasms underfoot | **Koschei the Deathless** |

<img src="docs/media/screenshot.png" alt="The Boneyard" width="880">

<table>
<tr>
<td><img src="docs/media/belt-mire.png" alt="The Black Mire"></td>
<td><img src="docs/media/belt-ice.png" alt="Koschei's Kingdom"></td>
</tr>
<tr>
<td align="center"><sub><b>The Black Mire</b> — cellular-automaton caves</sub></td>
<td align="center"><sub><b>Koschei's Kingdom</b> — halls and chasms</sub></td>
</tr>
</table>

### The bosses fight by their own rules

None of the three is a bigger pile of health. Each carries a mechanic out of the story it comes
from, and each mechanic has a counter.

**Viy** stands three turns with his eyelids down, and takes half again as much damage while
they are. On the fourth he lifts them, and anything caught in that gaze is struck hard and left
blind. The warning arrives a turn early. The answer is the one from the story: get out of sight.

**Baba Yaga** does not come alone — her huts on hen's legs stand with her. While any of them is
up, blows against her barely land, and she backs off and calls for help. The huts come first.

**Koschei** does not die. Beaten to nothing, he simply rises again, because his death is on a
needle's point and the needle lies somewhere on that same floor. After the first resurrection
the whole floor is revealed: a mechanic nobody can guess at is not a puzzle, it is a trap.

### The artwork is 16x16, and it is written as text

You can play it in glyphs or in pixels: the **ASCII** button in the top bar (or `G`)
switches renderers mid-run. Both are fed the same frame — the engine reports a glyph
*and* a sprite key for every cell.

<img src="docs/media/sprites.png" alt="Every sprite" width="740">

Forty-one sprites: nine tiles, eight item kinds, six heroes and eighteen creatures. There
is not a single PNG in the repository. All of it lives in `tools/sprites/pixels.py` as
text — sixteen rows per sprite, `.` for transparent, every other character an index into
that sprite's own palette:

```python
beast('volkolak', {'1': '#4a3520', '2': '#6f4f2d', '3': '#8f6b40', 'w': '#e8e0d0',
                   'r': '#a8433c'}, [
    '.oo..........oo.',
    'o33o........o33o',
    'o333oo....oo333o',
    '.o3333oooo3333o.',
    '.o33e333333e33o.',
    ...
```

That buys the art something a PNG never has: a changed pixel shows up in a diff as a
changed character rather than as "binary files differ". `tools/sprites/build.py` turns it
into `frontend/web/sprites.js` (a palette plus 256 index characters per sprite — 14 KB for
the lot) and into the contact sheet above. A CI job regenerates the file and fails if what
is committed has fallen behind its source.

**The ground and whoever stands on it are separate layers.** The engine reports `terrain`
and `entity` as different fields, so an upyr on a staircase no longer erases the staircase.

**One set of stones for three belts.** Terrain is drawn in neutral grey and tinted at
runtime with the colour of the belt. The alternative is three copies of every tile and
three chances to forget one of them.

The `Sprites.*` tests walk every tile, species, item kind and hero class and demand a
drawing for each — and, in the other direction, catch a drawing nothing ever asks for.
They read the generated artwork itself rather than a second list kept beside the engine: a
list would agree with the engine and disagree with the pictures.

## What is interesting about it, engineering-wise

**One core, two builds.** All the rules live in `engine/` and know nothing about input or output:
no `printf`, no key reading. Frontends submit an `Action` and read the state back. The terminal
build and the browser build are two ~600-line files wrapped around the same code. That separation
is exactly what lets the rules be unit-tested without a screen.

**Determinism as a requirement, not a happy accident.** The project ships its own generator
(xoshiro256\*\* seeded through splitmix64): `std::uniform_int_distribution` produces different
results on libstdc++, libc++ and MSVC, which would mean one seed building different dungeons on
different platforms. The generator state is part of the save file, so a loaded game resumes the
same sequence. `Integration.TheSameSeedAndActionsProduceTheSameRun` compares two runs byte for byte.

**Level connectivity is proven, not hoped for.** The generator partitions the level with BSP,
connects rooms along the tree, then runs a separate pass that finds stranded regions and tunnels to
them. The invariant "every walkable cell is reachable from the entrance" is checked across 300 seeds
and all twelve depths — including the worst case, a floor riddled with chasms.

**Symmetric field of view.** The first implementation used classic recursive shadowcasting. It is
fast and looks right, but it is not symmetric: there were cells from which a monster could see the
hero while the hero could not see back. In a game with ranged attackers that reads as cheating. The
symmetry test caught it immediately, and the algorithm was replaced with Albert Ford's symmetric
formulation.

**A flow field instead of A\* per monster.** One Dijkstra map built from the hero each turn serves
every monster at once: walking downhill is pursuit, walking uphill is flight. A\* remains the
fallback for when the field is blocked in a narrow corridor.

**Saves survive hostile input.** The format is textual and versioned, with tile grids run-length
encoded (a 72×34 floor costs about 2 KB, a whole game a few kilobytes — which matters for
`localStorage`). Loading is strict: any malformed record is rejected whole and never leaves the
engine half-built. One test truncates a save at dozens of different lengths and asserts that none
of them is accepted.

## Mechanics

| | |
|---|---|
| **Classes** | Six, and three of them carry a mechanic rather than a stat spread (see below) |
| **Bestiary** | 15 species with distinct behaviour: pursuit, ranged attacks, fleeing when wounded, summoning, erratic movement |
| **Bosses** | Viy (depth 4), Baba Yaga (depth 8), Koschei the Deathless (depth 12) — each with its own mechanic |
| **Items** | 18 pieces of gear, 8 potions, 8 scrolls; consumables are unlabelled until you try them |
| **Magic** | 6 spells, aimed along the line of sight |
| **Effects** | Poison, burning, freezing, confusion, blindness, haste, slow, regeneration, might, ward |
| **Also** | Hunger, water and chasms, doors, shrines that bless your gear, a speed-based turn scheduler, saved games |

### Six heroes

| Class | Plays on | Trait |
|---|---|---|
| **Vityaz** | Health and armour | — |
| **Vedun** | Spells at range | — |
| **Tat** | Speed, evasion, critical hits | — |
| **Znahar** | Potions | Knows every potion from the first turn, and they work half again as hard |
| **Kuznets** | Gear | Every item counts one grade better; shrines charge him half |
| **Bogatyr** | Melee | Every blow sweeps everything adjacent — paid for in speed |

Both languages, Russian and English, live inside the engine: every string is stored in both
variants, so the language can be switched at any moment — including for messages already in the log.

<img src="docs/media/title.png" alt="Title screen" width="700">

## Running it

### Browser

Nothing to build — [play online](https://thetoshix.github.io/nav-roguelike/).

### Terminal

```bash
git clone https://github.com/TheToshix/nav-roguelike.git
cd nav-roguelike
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/nav
```

Needs a C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+) and CMake 3.16+.
Built and tested on Linux, macOS and Windows.

### Tests

```bash
ctest --test-dir build --output-on-failure    # 285 tests
./build/nav --demo 20                         # 20 complete games, headless
```

### Web build

```bash
source /path/to/emsdk/emsdk_env.sh
./tools/build_web.sh          # writes index.html and nav.js into dist/
python3 -m http.server -d dist 8080
```

## Controls

| Keys | Action |
|---|---|
| `hjkl` `yubn`, arrows, numeric keypad | move; step into a creature to attack |
| `.` or `5` | wait a turn |
| `g` | pick up |
| `i` | pack: use, equip, remove |
| `d` | drop an item |
| `z` | cast a spell |
| `>` `<` | descend / climb |
| `p` | make an offering at a shrine |
| `S` `L` | save / load |
| `T` | switch language |
| `G` | sprites / ASCII |
| `?` | help |

The browser build adds mouse control and an on-screen pad on phones.

<img src="docs/media/terminal.png" alt="Terminal build" width="760">

## Testing

The project was written with tests from the first day rather than "covered" afterwards. The details
are in the [test plan](docs/TEST_PLAN.md), the [test cases](docs/TEST_CASES.md) and the
[bug reports](docs/BUG_REPORTS.md), which record nine defects found during development along with
reproduction steps and fixes.

| | |
|---|---|
| Unit and integration tests | **285** across 43 suites |
| Engine line coverage | **94%** |
| Platforms in CI | Linux (GCC, Clang), macOS, Windows (MSVC) |
| Also in CI | ASan + UBSan, `-Werror`, a coverage report, 20 headless games, the WebAssembly build |

Two approaches paid off more than anything else:

**Properties instead of examples.** Rather than "seed 42 must produce this exact floor", the suite
asserts "on any seed, every cell is reachable". Tests like that do not break on harmless changes to
the generator, and they catch precisely what they are meant to.

**Invariants after every turn.** `Integration.InvariantsHoldThroughoutLongRandomisedRuns` plays
random 1500-turn games and, after each turn, checks about fifteen conditions: health within bounds,
the hero not inside a wall, no two monsters on one cell, equipment indices pointing at real items of
the right kind. That is how states no hand-written scenario would ever build get found.

**Probabilistic mechanics get measured, not guessed at.** "Viy is softer with his eyes shut" and
"the huts protect Baba Yaga" are claims about a distribution, not about one swing. Those tests
average dozens of runs and demand "less than half", not merely "less" — otherwise they would flake.

## Layout

```
engine/          the core: game rules, no I/O
  include/nav/   public headers
  src/           map generation, field of view, pathfinding, combat, AI, saves
frontend/
  terminal/      ANSI rendering and raw keyboard input
  web/           the C binding layer for WebAssembly, the game page and the sprites
tests/           285 GoogleTest cases
docs/            architecture, test plan, test cases, bug reports
tools/           the web build script
  sprites/       the pixel art as text, and the generator that reads it
```

## License

[MIT](LICENSE).

---

<div align="center">
<sub>Timofey Iotmalik · <a href="https://github.com/TheToshix">github.com/TheToshix</a> · <a href="https://t.me/TheToshix">@TheToshix</a></sub>
</div>
