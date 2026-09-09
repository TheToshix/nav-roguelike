<div align="center">

# Nav

**A turn-based roguelike drawn from Slavic folklore.**
The game core is written in C++ and builds for both a terminal and the browser, via WebAssembly.

[![CI](https://github.com/TheToshix/nav-roguelike/actions/workflows/ci.yml/badge.svg)](https://github.com/TheToshix/nav-roguelike/actions/workflows/ci.yml)
[![Pages](https://github.com/TheToshix/nav-roguelike/actions/workflows/pages.yml/badge.svg)](https://github.com/TheToshix/nav-roguelike/actions/workflows/pages.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)](CMakeLists.txt)
[![Tests](https://img.shields.io/badge/tests-406-4c9a5a)](tests/)
[![Coverage](https://img.shields.io/badge/coverage-94%25-4c9a5a)](docs/TEST_PLAN.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

### ▶ [Play in your browser](https://thetoshix.github.io/nav-roguelike/)

[Русский](README.md) · [How to run it](docs/RUNNING.md) · [Architecture](docs/ARCHITECTURE.md) · [Test plan](docs/TEST_PLAN.md) · [Test cases](docs/TEST_CASES.md) · [Bug reports](docs/BUG_REPORTS.md)

<img src="docs/media/gameplay.gif" alt="Gameplay" width="800">

</div>

---

## What it is

Sixteen floors down into Nav, the underworld of Slavic folk tales. Death is final: a save is a
pause, not a spare life.

Every dungeon is generated from a seed. The same seed always produces the same floor, the same
creatures and the same loot, so any find — and any bug — can be reproduced from a single string.

### Four belts, eight guardians

The descent is cut into three parts of four floors. Each has its own generator, palette and
hazards, and its own master at the bottom — so twelve floors read as a journey rather than as
repetition.

| Belt | Depth | What is there | Halfway | At the bottom |
|---|---|---|---|---|
| **The Boneyard** | 1–4 | Dry crypts and corridors, doors, the odd pool | Mara | **Viy** |
| **The Black Mire** | 5–8 | Caves instead of rooms, water nearly everywhere, no doors at all | Vodyanoy | **Baba Yaga** |
| **Koschei's Kingdom** | 9–12 | Cold halls with chasms underfoot | Morozko | **Koschei the Deathless** |
| **The Scorch** | 13–16 | Hot stone, burning rivers, chasms | The Fiery Poloz | **Zmey Gorynych** |

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

### A guardian has a hall, and the doors close behind you

A boss that can be pulled into a corridor and fought one square at a time is not a boss but a
monster with a large health bar: everything that makes a guardian interesting — Baba Yaga's
huts, Vodyanoy's flooded floor, fire that walks a line — needs room to happen. So each master
of a belt (Viy, Baba Yaga, Koschei, Gorynych) has a hall, and the way down is inside it.

The threshold is labelled in advance: standing at the door you are told that a guardian waits
and that stepping in closes it behind you. The decision stays with the player, which is the
only thing separating difficulty from a dirty trick. The doors close when you step in yourself,
and open when the guardian falls. They hold both sides — the hero cannot leave and neither can
the guardian — because it is one rule for both. Two rules would drift apart, and the way you'd
find out is a boss strolling through a wall.

Koschei is the exception, and for a reason: his death is on a needle lying somewhere else on
the floor. A sealed hall would be a room you cannot win in.

The hall is carved into a floor that is already finished — a ring of walls and one door — and
then the floor is checked for having been cut in two. If it was, the carve is rolled back whole
and another spot is tried; when the spot that works is not where the stairs were, the stairs
move to meet it. A floor without a hall is a small loss; a floor with an unreachable half is a
broken game.

The same carve hides a **secret room** partway down the mire and Koschei's kingdom: a small
chamber cut into solid rock behind an ordinary closed door — and a door blocks sight, so from
a corridor the room reads as blank wall. Inside is **Bayun the Cat**, an optional mini-boss:
from a distance he sings, and the song takes the hero's whole turn away (its own effect,
Sleep — not confusion, not the freeze). He is guarding the Cat's Eye, a charm the song can no
longer touch once it is worn.

Now and then a floor holds someone you are not meant to fight. The **Domovoy** never strikes
first: pass it by without giving offence and it leaves a brief blessing and is gone; hit it and
it is an ordinary brute. The **Firebird** will not be held — on sight it backs away a cell
every turn. Corner it, or outpace it (haste, the swift charms), and it drops a feather that
will hold you in this world once, on its own.

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

Fifty-five sprites: ten tiles and effects, nine item kinds, six heroes and thirty creatures. There
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

**One set of stones for four belts.** Terrain is drawn in neutral grey and tinted at
runtime with the colour of the belt. The alternative is three copies of every tile and
three chances to forget one of them.

The `Sprites.*` tests walk every tile, species, item kind and hero class and demand a
drawing for each — and, in the other direction, catch a drawing nothing ever asks for.
They read the generated artwork itself rather than a second list kept beside the engine: a
list would agree with the engine and disagree with the pictures.

### Four belts, eight guardians, and a room above them

Sixteen floors in four belts. Each belt has its own generator, palette and hazards, a master
at the bottom and a lesser guardian halfway down.

A run begins on the **crossroads** — depth zero, the one floor nothing generates. A shrine, a
stair down and three pieces of gear on pedestals. Exactly one leaves with the hero; the rule
lives in one branch of `act_pick_up` rather than in a flag on the item, because three things
and one pair of hands is the whole idea of the room.

By the road down sits **Solovei the Brigand** — not a guardian (no hall, no music, not in the
boss table) but a one-time encounter. He never leaves his spot, but every third turn he
whistles, and the whistle shoves the hero sideways off the path, costs a turn and stings a
little. Kill him or slip past; either way it happens exactly once, at the very start.

Every guardian fights in **two or three phases**, turning at even fractions of its health. A
boss whose only change is a smaller number is a wall, not a fight — so what changes is
behaviour. Viy's eyelid cycle shortens from four turns to three to two. Baba Yaga leaves her
huts for the mortar and then calls a fresh hut back. Koschei stops striking and starts drawing
the life out of you. Zmey Gorynych loses a head at each threshold and the survivors stop
saving their fire. The phase only ever rises: healing a boss must not hand back a pattern the
player has already beaten.

Guardians are also drawn at double size — a 16x16 sprite rendered across two cells, standing
on its own and overhanging the ones beside it. It still occupies exactly one cell: a real
multi-tile creature would touch movement, sight, pathfinding and saves all at once, which is
the classic source of quiet bugs in this genre. Here the picture carries the warning the rules
do not, and changes nothing.

### Twelve pieces of gear that do something, in four sets

Numbers alone make gear that is only ever "the bigger one". These twelve carry a mechanic —
a club that knocks down, a boar spear meant for the big ones, a shirt that turns one blow
aside per floor, a cuirass that sends part of the blow back, a knife that takes something from
the slain.

They also form four sets of exactly one weapon, one armour and one amulet. Three pieces and
three slots is deliberate: completing a set costs the hero everything they have, which makes
it a decision about the run rather than about a hand. Every piece is worth wearing alone — a
set that is worthless until finished is a trap, and a test forbids it.

### Music with no audio files in the repository

Each of the eight guardians has a theme, and it changes with the phase. There is not one mp3
here: `frontend/web/music.js` is a small Web Audio synth that builds a drone, a pulse and a
motif out of oscillators and filtered noise, in modal minor scales, opening up as the fight
gets worse.

It is not checked by ear. Each theme is rendered through an `OfflineAudioContext` and measured:
there is signal, the third phase is measurably louder than the first, and no phase change
leaves a discontinuity big enough to be heard as a click.

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
| **Bestiary** | 20 species with distinct behaviour: pursuit, ranged attacks, fleeing when wounded, summoning, erratic movement, trailing fire across the floor; one that is not always an enemy |
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

### Walking one square at a time is dull, and that is fixable

A movement key can be **held**: the hero keeps walking until it is released, and `Shift`
sprints. The game stays turn-based throughout — the page is only asking the engine for the
same single step again, so monsters move between steps, hunger ticks, and every invariant
holds; the engine cannot tell a held key from a patient finger. A tap of `Shift` and a
direction still runs to the next junction in one action, `o` walks to the nearest place the
hero has not been, and `m` shows the whole explored floor with the way down marked.

The on-screen pad on phones works the same way: hold it. Tapping the same arrow thirty times
is exactly what makes people put a game down.

The interesting part is not the running but the brakes. Movement stops when something comes
into view, when the hero is hurt, when a new effect lands, and when there is an item, a
staircase or a shrine underfoot; a run additionally stops where the corridor offers a choice,
and auto-explore refuses to set off at all while anything is in sight.

There is one stop rule, not four: `Game::situation()` and `situation_changed()` in the engine.
The run reads it, auto-explore reads it, a held key in the terminal reads it, and a held key in
the browser reads it too (the engine ships its own snapshot of the situation with every frame,
and the page only compares two of them). Four lookalike rules in four places would drift apart,
and would drift apart quietly — a held key that carries the hero into a monster is the worst
thing this feature can do.

The terminal reaches the same place from the other side: keypresses that piled up in the
buffer while the game was thinking are thrown away the moment the situation changes. Otherwise
the player sees a monster, lets go — and the hero plays half a second of queued keys into its
face.

### Combat you can follow

A blow now reports what actually got through and what is left: "Anchutka hits you for 7 —
12/34 left". It used to report the dice roll, and stayed quiet when the warding shirt ate the
blow entirely. Effects name themselves and their duration instead of "something foul takes
hold of you".

In the pack, every wearable says what it would change: `+2 atk, -1 def`, green or red. The
engine works that out by putting the item on, asking the character sheet, and taking it off
again, so the preview cannot disagree with what actually happens. The test checks that
equality rather than the numbers themselves.

### Death explains itself

Death here is final, so it owes the player a reason. The ending screen shows the last five
blows — turn, source, damage, health left — and, separately, everything still unused in the
pack. That list is usually the answer to "what did I do wrong": the healing draught was in
there the whole time.

### The score table

Death here is final, and the only thing a run leaves behind is a line in the table. It keeps the
ten best descents: score, depth, class, and the seed the run can be repeated from. Ordering is by
score, ties broken by depth, and equal depths by who wasted fewer turns. The engine scores and
orders; the frontends only display, so the two builds show the same table and never get to
disagree about what a run was worth. The terminal keeps it in `~/.nav_scores`, the browser in its
own storage.

Both languages, Russian and English, live inside the engine: every string is stored in both
variants, so the language can be switched at any moment — including for messages already in the log.

<img src="docs/media/title.png" alt="Title screen" width="700">

## Running it

The short version is below. The long one — installing everything from scratch, and what to
do when it does not build — is in [docs/RUNNING.md](docs/RUNNING.md) (in Russian).

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
ctest --test-dir build --output-on-failure    # 406 tests
./build/nav --demo 20                         # 20 complete games, headless
./build/nav --sweep 4                         # the sweeper: reach the bottom, kill every guardian
```

### Web build

```bash
source /path/to/emsdk/emsdk_env.sh
./tools/build_web.sh          # writes index.html and nav.js into dist/
python3 -m http.server -d dist 8080
```

## Settings, and the phone

Everything switchable switches in play: control scheme, map view, sprites or ASCII, boss music,
language. The settings screen opens from the title and from inside a run, and each setting is a
labelled row of choices with the current one lit.

This did not come from a love of settings. The control scheme used to live on a button labelled
`HJKL` — a button labelled with its current state rather than with what it does. A player who
wanted WASD read it as a heading and drew the only available conclusion: WASD is broken (see
[NAV-016](docs/BUG_REPORTS.md)).

The map now shows **the whole floor** by default whenever a cell still comes out big enough to
read, and only falls back to a window following the hero on a small screen. The window used to
be all there was, which on a large monitor reads as part of the game being off-screen.

On a phone the game is meant to be played in landscape: the map takes the full height, the
panel sits beside it as on a desktop, and the pad and the actions float at the corners the
thumbs already rest on. Portrait works too and has been rewritten as well. None of this is
checked by eye: the page is opened at three phone sizes and the rectangles of every button,
meter and table are compared pairwise — any overlap larger than two pixels fails the check.

## Controls

There are two schemes, switchable in play: `k` on the title screen, the `Esc` menu during a
run, or the button up top in the browser. The choice is remembered between runs.

| | Classic | WASD |
|---|---|---|
| Walk (hold to keep walking) | `hjkl` `yubn` | `wasd` `qezc` |
| Sprint (hold) / run to the junction (tap) | `Shift` + the same | `Shift` + the same |
| Drop an item | `d` | `r` |
| Cast | `z` | `f` |
| Quit | `q` | from the menu |
| Save / load | `S` `R` | from the menu |

Everything else is the same in both: arrows and the numeric keypad also walk, `.` or `5`
waits, `g` picks up, `i` opens the pack, `o` walks to what has not been seen, `m` shows the
whole floor, `>` `<` take the stairs, `p` makes an offering, `T` switches language, `?` is
help, `Esc` is the menu. The browser build adds `G` for sprites/ASCII and `M` for music.

The help screen is not hand-written: it and the keymap come out of one table in the engine
(`engine/src/keys.cpp`), so a scheme cannot gain a key the help forgets to mention. There is
a test for that.

The browser build adds mouse control and an on-screen pad on phones.

<img src="docs/media/terminal.png" alt="Terminal build" width="760">

## Testing

The project was written with tests from the first day rather than "covered" afterwards. The details
are in the [test plan](docs/TEST_PLAN.md), the [test cases](docs/TEST_CASES.md) and the
[bug reports](docs/BUG_REPORTS.md), which record seventeen defects found during development along with
reproduction steps and fixes.

| | |
|---|---|
| Unit and integration tests | **406** across 50 suites |
| Engine line coverage | **94%** |
| Platforms in CI | Linux (GCC, Clang), macOS, Windows (MSVC) |
| Also in CI | ASan + UBSan, `-Werror`, a coverage report, 20 headless games, a sweep to floor 16, the WebAssembly build |

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

**A bot that reaches the end.** Two bots check the game, because there are two questions.
`--demo N` plays fair, with the stats the game hands out, and shows the difficulty curve: runs
currently end on floors 3-5. `--sweep N` takes the same bot deliberately over-levelled and answers
a different question — whether the second half of the game is reachable at all; a run only counts
if the hero got to floor 16 and put down all eight guardians.

The second bot earned its keep immediately: before it, no run had ever gone past floor 5, which
means eleven floors and six guardians existed only inside arena tests. The very first sweep found
four defects that made the second half unreachable or broken — from Koschei's needle being placed
on the wrong floor to victory firing four floors early. Both bots now run in CI.

## Layout

```
engine/          the core: game rules, no I/O
  include/nav/   public headers
  src/           map generation, field of view, pathfinding, combat, AI, saves
frontend/
  terminal/      ANSI rendering, raw keyboard input and the run bot
  web/           the C binding layer for WebAssembly, the game page and the sprites
tests/           406 GoogleTest cases
docs/            how to run it, architecture, test plan, test cases, bug reports
tools/           the web build script
  sprites/       the pixel art as text, and the generator that reads it
```

## License

[MIT](LICENSE).

---

<div align="center">
<sub>Timofey Iotmalik · <a href="https://github.com/TheToshix">github.com/TheToshix</a> · <a href="https://t.me/TheToshix">@TheToshix</a></sub>
</div>
