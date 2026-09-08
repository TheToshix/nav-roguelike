<div align="center">

# Nav

**A turn-based roguelike drawn from Slavic folklore.**
The game core is written in C++ and builds for both a terminal and the browser, via WebAssembly.

[![CI](https://github.com/TheToshix/nav-roguelike/actions/workflows/ci.yml/badge.svg)](https://github.com/TheToshix/nav-roguelike/actions/workflows/ci.yml)
[![Pages](https://github.com/TheToshix/nav-roguelike/actions/workflows/pages.yml/badge.svg)](https://github.com/TheToshix/nav-roguelike/actions/workflows/pages.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)](CMakeLists.txt)
[![Tests](https://img.shields.io/badge/tests-234-4c9a5a)](tests/)
[![Coverage](https://img.shields.io/badge/coverage-94%25-4c9a5a)](docs/TEST_PLAN.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

### ▶ [Play in your browser](https://thetoshix.github.io/nav-roguelike/)

[Русский](README.md) · [Architecture](docs/ARCHITECTURE.md) · [Test plan](docs/TEST_PLAN.md) · [Test cases](docs/TEST_CASES.md) · [Bug reports](docs/BUG_REPORTS.md)

<img src="docs/media/gameplay.gif" alt="Gameplay" width="800">

</div>

---

## What it is

Twelve floors down into Nav, the underworld of Slavic folk tales. Viy waits on the fourth floor,
Baba Yaga on the eighth, Koschei the Deathless on the twelfth. Death is final: a save is a pause,
not a spare life.

Every dungeon is generated from a seed. The same seed always produces the same floor, the same
creatures and the same loot, so any find — and any bug — can be reproduced from a single string.

<img src="docs/media/screenshot.png" alt="Screenshot" width="880">

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
| **Classes** | Vityaz (armour and health), Vedun (magic), Tat (speed, evasion, critical hits) |
| **Bestiary** | 14 species with distinct behaviour: pursuit, ranged attacks, fleeing when wounded, summoning, erratic movement |
| **Bosses** | Viy (depth 4), Baba Yaga (depth 8), Koschei the Deathless (depth 12) |
| **Items** | 18 pieces of gear, 8 potions, 8 scrolls; consumables are unlabelled until you try them |
| **Magic** | 6 spells, aimed along the line of sight |
| **Effects** | Poison, burning, freezing, confusion, blindness, haste, slow, regeneration, might, ward |
| **Also** | Hunger, water and chasms, doors, shrines that bless your gear, a speed-based turn scheduler, saved games |

Both languages, Russian and English, live inside the engine: every string is stored in both
variants, so the language can be switched at any moment — including for messages already in the log.

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
ctest --test-dir build --output-on-failure    # 234 tests
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
| `?` | help |

The browser build adds mouse control and an on-screen pad on phones.

<img src="docs/media/terminal.png" alt="Terminal build" width="760">

## Testing

The project was written with tests from the first day rather than "covered" afterwards. The details
are in the [test plan](docs/TEST_PLAN.md), the [test cases](docs/TEST_CASES.md) and the
[bug reports](docs/BUG_REPORTS.md), which record eight defects found during development along with
reproduction steps and fixes.

| | |
|---|---|
| Unit and integration tests | **234** across 32 suites |
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

## Layout

```
engine/          the core: game rules, no I/O
  include/nav/   public headers
  src/           map generation, field of view, pathfinding, combat, AI, saves
frontend/
  terminal/      ANSI rendering and raw keyboard input
  web/           the C binding layer for WebAssembly, and the game page
tests/           234 GoogleTest cases
docs/            architecture, test plan, test cases, bug reports
tools/           the web build script
```

## License

[MIT](LICENSE).

---

<div align="center">
<sub>Timofey Iotmalik · <a href="https://github.com/TheToshix">github.com/TheToshix</a> · <a href="https://t.me/TheToshix">@TheToshix</a></sub>
</div>
