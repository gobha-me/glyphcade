# Development history

How each epic and each significant change actually landed, kept because the
*reasoning* is the useful part — several of these record a plausible approach
that was tried and was wrong, and why.

This is an archive. [STATUS.md](../STATUS.md) is the live state and supersedes
anything here that disagrees with it.

⚠ **Issue references written as `term-game#NN` predate the move to GitHub** and
are not linkable — that tracker was self-hosted and is not public. They are
kept because the surrounding prose refers to them by number. Live issues are
written as ordinary `#NN` links.

---

## The preview was a dead-end copy (term-game#55)

Reported by the maintainer playing on real hardware: **the next-up preview does
not update.** Reading it made the report worse rather than better. `m_next` was
filled by `reset()` and then read by nothing except `hold()`, while every spawn
site — `reset()`, `lock_active()`, `clear_full_rows()` — called
`spawn(take_next())` and pulled straight off the bag. The three pieces on the
panel were never going to arrive.

And it was skewed from the first frame. `reset()` drew `kPreview` pieces into
`m_next` and *then* drew one more for the active piece, so the opening panel
advertised draws 1-3 while draw 4 was already falling and draw 5 was next.

⚠ **The root cause is not three wrong expressions.** It is that `spawn(Piece)`
let each call site *choose* its piece, so "the preview is the spawn stream" was
a convention repeated at three sites rather than a fact. Three sites is three
chances to disagree, and they all took it. The fix is
`spawn_next()` — pop `m_next.front()`, `advance_preview()`, `spawn()` — and
after it **`spawn(Piece)` has exactly one caller in the file**. That is the
whole change; the sequences are now the same sequence by construction.

`hold()` keeps its own path, because it must: it peeks at the head to build a
candidate, the fit check can still refuse the whole swap, and only then does the
queue advance. Three touches of one queue straddling a guard — structurally the
shape of the bug just fixed — and it is safe only because `fits()` is `const`
and `spawn_x()` is pure. That is now written at the site rather than inferred.

### Why five tests and a bag check never saw it

`test/27tetris`'s seven-bag case read `[active] + preview()` as one window. That
window was draws **4, 1, 2, 3** — a *permutation* of the first four — and the
case counts a multiset rather than an order. **It could not have failed.** The
seed-determinism case compared two boards to each other, and two frozen queues
are equally frozen. Both were real properties, correctly written, and blind to
this by construction.

The two claims a preview actually makes were asserted by nothing:

1. after a lock, `preview()` has **shifted**;
2. `preview()[0]` **is** the piece that spawns next.

⚠ The second cannot be tested from `preview()` alone — it needs `preview()` read
*before* a lock compared against `active()` read *after* it. That is the reason
a file with thirty-two cases missed it.

⚠ **The bag case now reads the SPAWN stream**, which is what it always meant to
test. It clears the stack with `load()` on each iteration: pieces spawn at rot 0
into columns 3-6 and never complete a row, so an un-cleared board tops out
around the tenth drop and a naive thirteen-drop loop asserts against a dead
board. `load()` deliberately leaves the bag and the preview alone, which is what
makes that safe — now noted on `load()` itself, because two cases depend on it.

### Twelve mutations, twelve killed — and five have exactly one killer

| mutation | killed by |
|---|---|
| `advance_preview()` shifts but does not refill the tail | bag case, the pinned opening, the panel case |
| it shifts by two | six cases |
| it does not shift at all | six cases |
| `spawn_next()` pops `m_next[1]` | four cases |
| `clear_full_rows` reverted to `spawn(take_next())` | **the line-clear case only** |
| `lock_active` reverted to `spawn(take_next())` | window case, bag case, panel case |
| `hold()` never advances | the empty-slot hold case |
| `hold()` advances **above** the fit guard | **the "refused whole" case only** |
| `hold()` advances unconditionally | **the occupied-slot hold case only** |
| `reset()` burns a fourth draw again | pinned opening, bag case |
| the panel draws `preview()[0]` three times | **the panel case only** |
| the panel drops its `y += 3` | **the panel case only** |

The five single-killer rows are why those cases exist. `clear_full_rows` is a
**separate spawn call** — `lock_active` awards and returns while the rows are
still on the board — so a fix applied to one and not the other is invisible
everywhere else. And the two panel-only rows are the answer to "is a UI case
redundant with the model cases": no. The model cases pin `preview()` and never
reach the loop that draws it.

⚠ **Two predictions about which mutation had a unique killer were wrong** — the
missing refill and the restored fourth draw were each caught by more cases than
expected. Predicting the killer set is not the same as measuring it, and only
the measurement is evidence.

⚠ **The pass had to be run twice**, because review moved `advance_preview()` to
`std::shift_left` and rewrote how the panel case slices the screen. A mutation
result is evidence about the code that was mutated; changing either the code or
the tests afterwards retires it. The second run is the one in the table.

### The pty run had a control, and needed one

Three hard drops under `script(1)`, with the capture replayed into a grid and
the NEXT panel snapshotted whenever it settled:

- **before the fix: one panel state.** Frozen across all three drops, exactly as
  reported.
- **after: four states**, each one a single-step shift — every box *k* becoming
  what box *k+1* held before the lock.

⚠ The control is the point. "The panel changes" is only evidence if the same
harness can show it *not* changing, and it was cheap: stash the two source
files, rebuild, capture, pop.

### What is verified, and what is not

Green in all four configurations, `-Werror` throughout, and the symptom the
maintainer reported is confirmed fixed in a pty. ⚠ What is **not** verified is
whether a three-deep preview is the right depth to play with, or whether the
panel reads well at speed — the same feel question every game here has.

⚠ **The seeded piece order changed**, deliberately: a board's active piece is
now draw 1 rather than draw 4. No fixture pinned a literal sequence, so nothing
had to be re-derived — but the opening is now pinned on purpose for seed 1234,
because it is the only assertion in the file that can see `reset()` burning a
draw.

⚠ **`Tetris` calls `entropy()` twice** (`tetris.cpp:82-83`), so the
constructor's board carries a seed that `m_seed` does not describe. Harmless —
`start()` opens the options screen and dismissing it rebuilds the board from
`m_seed`, so the orphan is never played or drawn — and it is a suite-wide
convention, identical in `snake.cpp`, `twenty48.cpp` and `minesweeper.cpp`.
**Deliberately not touched here**: fixing it changes every game's opening board
and does not belong inside a preview fix.

---

## The exported package is now resolved, not just read (term-game#46)

`cmake/project-config.cmake.in` carries the consumer-side termforge floor,
`find_dependency(termforge 0.6.0)`. Until now **nothing in `ctest` executed that
line.** `cmake/check_export.cmake` installs to a scratch prefix and greps the
generated `glyphcadeTargets*.cmake` for rtaudio tokens — as *text*. It never
calls `find_package(glyphcade)`, so the generated `glyphcadeConfig.cmake` was
written, inspected, and never run.

That made a wrong floor invisible from both sides at once: the in-tree build
takes the FetchContent path and never reads that file, and CI only builds
in-tree. It broke exclusively in a stranger's tree — the failure
`cmake/deps/termforge.cmake` names in its own words, a package that "resolves on
the developer's machine and nowhere else". The floor has moved four times
(0.1 → 0.1.10 → 0.1.15 → 0.2.2 → 0.6.0), and every time correctness rested on
somebody remembering to edit a second file.

**`cmake/check_consumer.cmake`**, ctest name **`consumer-resolves`**, installs to
its own scratch prefix, generates a throwaway consumer into the build tree and
*configures* it against the prefix. Configure only — `find_package` is the whole
assertion, nothing is compiled. One `cmake --install` is enough for both
packages because `termforge_INSTALL` follows `${PROJECT_NAME}_INSTALL`, so it
needs no network and no second install step. It runs in **1.3 s**.

### The two lines that are the entire point

Red-verified by putting a stale `0.2.2` floor in `project-config.cmake.in` and
reconfiguring. Both tests were run, and both results matter:

```
6: -- export is rtaudio-free: CLEAN
1/2 Test #6: audio-export-clean ...............   Passed    0.76 sec
7:   Could not find a configuration file for package "termforge" that is
1/1 Test #7: consumer-resolves ................***Failed   14.30 sec
```

A package **no consumer on earth can resolve**, reported `CLEAN` by the check
that was supposed to be watching the export. The new one goes red and carries
the consumer's own diagnosis into the failure message.

### The generated consumer is generated because a committed one gets eaten

The obvious shape — a three-line `CMakeLists.txt` under `test/` — does not work.
`test/CMakeLists.txt` globs `test/*` and `add_subdirectory()`s any directory
holding a `CMakeLists.txt`, so a committed `test/consumer/` would be pulled into
**our** build and run `find_package(glyphcade)` at our own configure time. It is
written with a bracket argument (`[==[ … ]==]`, no substitution at all) and the
project name arrives as `-DCHECK_PROJECT=` on the consumer's command line, so
the text needs no `\$` escaping — where one dropped backslash writes an *empty*
marker rather than a missing one, which is precisely the vacuous pass this check
must be incapable of.

### Resolution is not enough on its own, and the extra assertions are not padding

Three of the four extra checks were red-verified individually:

| arm | what was broken | what fired |
|---|---|---|
| R1 | floor `0.2.2` vs a 0.6.0 install | resolution — consumer exits 1 |
| R2 | floor stripped to `find_dependency(termforge)` | **the regex branch** — consumer configures *fine* |
| R3 | consumer pointed at an empty prefix | resolution, proving `RESULT_VARIABLE` is checked |
| R4 | a real package installed at a *second* prefix | the `_DIR`-inside-scratch-prefix guard |

**R4 is the one that matters most in the long run.** Nothing stops somebody
running `cmake --install build` into `/usr/local` once; from then on the
consumer would resolve *that* forever, and the test would pass no matter what
the source tree said. Asserting `glyphcade_DIR` starts with the scratch prefix —
and that the reported version equals this build's — is what distinguishes
"resolved the package we just built" from "resolved a package". `EXPECT_VERSION`
also catches the shallow-clone case where `git describe` degrades to `0.0.0` and
the package stays perfectly resolvable.

⚠ **R2 is the only thing the version comparison independently buys, and the
comment says so.** termforge's version file is `SameMinorVersion`, so a
*successful* `find_dependency(termforge X.Y.Z)` already implies the resolved
major.minor is `X.Y` — the comparison cannot fire while resolution passes. Both
half-bump directions are caught by resolution alone. What it uniquely catches is
the line being deleted or de-versioned, which resolution can *never* notice
because it then always succeeds. A text-vs-text check against the pin in
`cmake/deps/termforge.cmake` would therefore add nothing, and was deliberately
not written.

### ⚠ Two traps in re-running the red arms

**Editing the `.in` is not enough.** `configure_package_config_file()` runs at
*configure* time and `check_consumer.cmake` only *installs*, so editing
`project-config.cmake.in` and going straight to `ctest` leaves
`build/glyphcadeConfig.cmake` at the old value and the arm is a **false green**.
Reconfigure with `cmake -B build`, then `grep find_dependency
build/glyphcadeConfig.cmake` to confirm the edit actually arrived, before
believing any result.

**A leftover consumer cache is the other false green.** `consumer-check-bin`
holds `glyphcade_DIR` and `termforge_DIR` as resolved cache entries; the script
wipes all three scratch directories on entry for that reason. On *failure* it
leaves them behind on purpose — same stance as `check_export.cmake`.

### The suite's first RESOURCE_LOCK

`audio-export-clean` and `consumer-resolves` both run `cmake --install` on the
same build tree, and `cmake --install` finishes by writing one
`install_manifest.txt` into it. Their scratch prefixes do not collide, but that
manifest is a shared write, and two concurrent installs race on it under
`ctest -j`. It would rarely fail outright — which is worse: the failure mode is
one unreproducible CI red per quarter. `RESOURCE_LOCK install-tree` serialises
the two. The label names nothing real; it is plain mutual exclusion.

### What is verified, and what is not

Six configurations green — GCC, GCC without audio, Clang, ASan, UBSan, TSan —
**31 suites** each, up from 30, `-Werror` throughout. The green arm resolves
`glyphcade 0.16.0` with `termforge 0.6.0` against the scratch prefix. No C++
changed; the whole issue is CMake.

⚠ **Nothing compiles or links against the installed package** — the consumer
configures only, which is what the issue asked for and is enough to execute the
floor. A broken `INTERFACE` include path or a missing archive would still
survive. That is the honest remaining gap in `cmake/install.cmake`'s
"three acquisition modes" claim.

⚠ **`consumer-resolves` is conditionally registered**, on `${PROJECT_NAME}_INSTALL`,
exactly like `audio-export-clean`. In a configuration without install rules it
is not there at all — **absent means skipped, not passing**, and the configure
log says which. `ctest -N` is the check.

⚠ **`termforge_DIR` is forwarded to the consumer, and only when real.** On a
machine where `find_package(termforge 0.6.0 QUIET CONFIG)` *succeeds*, the
FetchContent branch in `cmake/deps/termforge.cmake` never runs, `termforge_INSTALL`
is never set, and nothing puts `lib/cmake/termforge/` into the scratch prefix.
Here it is legitimately `termforge_DIR-NOTFOUND` and the script ignores that
spelling. Without the forwarding, that machine would see a red test with a
diagnosis pointing at the floor, which is the wrong answer.

---

## What the geometry block is (term-game#42 + term-game#15)

### The rule, written down for the first time

**Rules extent never scales; presentation may, and has a ceiling as well as a
floor.** It is now a hard rule in AGENTS.md, next to "every game is playable at
the bottom tier", because it is the same kind of statement.

Board size **is** the game — Minesweeper's board size is its difficulty, 2048 is
4x4, Tetris is 10x20 — so a bigger terminal must never buy a bigger board. What
may scale is the *cell*.

⚠ **For Snake this is correctness rather than taste.** `score_key(Level, Walls)`
does not include the field size, so growing the field would silently make every
stored record incomparable with every new one — the exact failure the
wrap-vs-solid split exists to prevent, arriving through geometry instead.

⚠ **No per-game scaling mechanism was built**, deliberately. Nothing on the
roster wants one, and a `cell_scale` field nothing reads is the speculative seam
this repo has twice refused (`context.hpp`'s "no reserved seams left",
`game_meta.hpp`'s "empty is the cheap case on purpose"). The policy is written;
the mechanism is built when a game asks. Which is also why **Sokoban's click
hit-test needed no change** — #42 warns that any change to how a view scales has
to be made there too, and the answer here is that no view scales.

### The floor has a kind — and the kind is a REASON, not a promise

`GameMeta::geometry` is a `GameGeometry{cols, rows, SizeFloor}`, declared **last**
(designated initialisers must follow declaration order — the note that used to
sit on `options` moved down to it, with a corrected blast radius: all five games
set `.geometry`, four set `.options`, only two set `.keyboard`).

| game | floor | kind | detail pane reads |
|---|---|---|---|
| Minesweeper | 21x13 | `Drawable` | `size: 21x13 needed` |
| 2048 | 29x19 | `Drawable` | `size: 29x19 needed` |
| Snake | 58x20 | `Drawable` | `size: 58x20 needed` |
| Tetris | 35x24 | `Drawable` | `size: 35x24 needed` |
| Sokoban | 34x12 | **`Playable`** | `size: 34x12 needed to play well` |

⚠ **THE FIRST DRAFT PRINTED "recommended" FOR SOKOBAN AND "minimum" FOR THE
REST, AND THAT WAS WRONG.** It read as "you may go below this one" — and Sokoban
refuses below 34x12 exactly as hard as the other four: its `compute_layout` sets
`fits = cols >= kNeedCols && rows >= kNeedRows` and its `draw()` falls through to
`draw_too_small()`. So the menu made a promise the game does not keep, one
keystroke apart, which is **the exact defect term-game#42 was filed about**,
reintroduced by the change meant to fix it. Reproduced on a 30x10 pty: soft
advice, then a hard refusal with no board on it and arrow keys still incrementing
the move counter.

What the kind actually records is whether the NUMBER is derivable or chosen —
21x13 falls out of the board and cannot move, 34x12 is an argument about seeing
enough of a room and could be argued down. Both say **"needed"**; only the
`Playable` one adds **why**. The reason differs, the force does not.

⚠ **Minesweeper declares EASY's 21x13, not Hard's 63x20** — the contract is *the
smallest terminal at which the game is playable at all*, not at every setting.
And it is derived from `kMinesweeperOptions[0].default_index` via a new
`minesweeper::default_preset()`, **not** from a typed `Level::Easy`: which level
a fresh game starts on had three copies, and change the default to Medium and the
menu would keep advertising 21x13 while the game that starts needs 35x20. Every
test would have stayed green, because they all re-derived from the same hardcoded
Easy.

⚠ **Every other number is derived too**, from the constants in each game's own
`layout.hpp` — the same ones its `compute_layout` compares against.

⚠ **The selector warns; it never refuses.** Minesweeper's in-game screen naming
what the chosen board needs and keeping the level keys live stays. #15 was always
about finding out one screen earlier.

⚠ **Declared is enforced separately from well-formed.** `{0,0}` is deliberately
legal in the schema, so `geometry_is_well_formed` cannot catch a game that simply
forgot one — a sixth game would compile clean in all four configurations and
silently never warn. `all_geometry_is_declared()` is a second `static_assert` for
exactly that, which is what makes AGENTS.md's "that is a static_assert, not a
convention" true of this rule as well as its neighbours.

### Two channels split by width, so neither can swallow the other

The detail pane names the size. The pane is **dropped below 48 columns**, which
is very nearly the band in which games stop fitting — so below that width, and
only below it, the warning takes the footer row instead. Above it the pane is
already saying so and the row stays the degradation notice's.

⚠ **Both orderings of a SHARED row were wrong, and the tests found both.**

- *Notice wins.* `m_notice` is **sticky** — it holds the most recent `ErrorEvent`
  until the next game entry clears it — and `FallbackDriver` reports no colour
  during setup, so on a bare terminal the footer is never free. The warning was
  unreachable for a whole session at the bottom tier, the tier this repo promises
  always works. `test/11selector` failed on the first run with the footer reading
  `no colour capability: ASCII bo`.
- *Warning wins.* That swallowed a **fresh** degradation: leaving Tetris on a
  terminal with no kitty protocol raises the report one frame before the selector
  redraws, and if Tetris also did not fit the warning took the row. The notice is
  one-shot in practice, so that is a lost event — against "degradation is an
  event, never a silent downgrade".

Splitting by width dissolves it rather than picking a loser. The evidence that it
is the better design and not merely a third option: `test/11selector`'s
pre-existing keyboard case had to be moved off the probe's default 60x20 to keep
passing under *warning-wins*, and the split let it move **back**.

### The footer is a cascade, because one string cut a number in half

`hud::pick_that_fits` is a new runtime sibling of `pick_for_width`, next to it in
core. A `Tier` holds a `string_view` into a literal and a hand-written floor,
which is what makes a table `static_assert`able; a sentence built from
`std::to_string` has neither, so it is measured rather than declared.

⚠ **The one-string version shipped an argument backed by a measurement one column
above the cliff.** It read: the wanted size comes first, so only the tail is lost
— "measured on a 30-column pty". The Shell draws the selector from **20**
columns. On a real 22-column pty the footer read

```
Minesweeper needs 21x1
```

a truncated number that reads as a complete and wrong one. At 20-28 columns the
old Playable arm lost its number entirely. Now three forms, widest first, and the
narrowest is 11 characters:

```
40 cols:  Sokoban needs 34x12; you have 40x10
30 cols:  Sokoban needs 34x12
20 cols:  needs 21x13            (Minesweeper — its title alone is too wide)
```

⚠ **Sweeping ONE game could not see this.** The first sweep used Sokoban, whose
title is short enough that even the widest form clips *after* the size — so
collapsing the cascade to its widest entry survived mutation testing. The failure
needs the longest title on the roster. The sweep now covers every game at every
width from `kMinCols` to `kDetailPaneMinCols`.

### The ceiling: 120 columns, columns only, and ALL of the chrome

`Shell::kSelectorMaxCols = 120`. Past it the body stops widening and is
**centred** — what a game does. Measured on a real 240x40 pty: the body spans
columns **60..179**. At 120x40 it spans **0..119**, byte-identical to before.

⚠ **The title, footer and hint rows are offset too**, and a first draft left them
at x=0 arguing they were "chrome, not a measure". At 240 columns that stranded
them sixty columns from the panes they describe — the same screen disagreeing
with itself, which is one better than the two-screens version #42 is about and no
more defensible. Mutation testing confirmed the gap was invisible: the span check
reads row 1, the frames' top border, so moving the title and hint row back to x=0
survived the entire suite.

⚠ **Columns only.** Rows past a floor are *capacity* — more roster visible, and
for the game #43 is about, more of a pile. There is no `kSelectorMaxRows`.

⚠ Two `static_assert`s now tie the ceiling to the two floors it silently owns:
below `kDetailPaneMinCols` the pane vanishes at *every* width, and in the 48..59
band `list_w` clamps everywhere so "120 splits as 48 + 72" stops being true.

Also named while the line was being touched: `kListPaneMinCols = 24`, a bare
literal for six releases that bites from 48 to 60 columns (`48 * 2 / 5 == 19`).

### A render path that allocates per frame is measurable from two suites away

The sharpest thing this change taught, and it arrived as a red test in a suite
that has nothing to do with geometry.

`test/26snake-ui`'s "the board does not move while the options screen is up"
asserted `ticks() == 120` after calling `tick()` 120 times by hand. That is only
true if the two `app.step()` calls above it delivered **zero** ticks — and
`test_run_frames` deliberately does not reset the tick clock, so real wall time
spent drawing selector frames becomes ticks the accumulator hands over.

The first draft rebuilt the footer warning **every frame**: three `std::string`s
constructed and concatenated for an answer that changes only when the selection or
the terminal does. Measured, under three CPU hogs on the TSan build: **main
passed 6/6, the branch failed 5/6** (121, 121, 123, 121, 123 against 120).

Both halves were wrong and both are fixed. The warning is cached on
`(index, cols, rows)`, and the assertion is now a **delta** from a baseline taken
before the loop — which is what the case always meant ("the ticks ARRIVED"). Same
load, after: **6/6**.

⚠ The lesson generalises past this change: an absolute tick count in a
`test_run_frames` suite is a latent trap that fires for whoever next makes a
frame slower, and points at the wrong file when it does.

### Sixteen mutations, sixteen killed — after four survivors sent me back

| mutation | result |
|---|---|
| ceiling removed (`min` → `max`) | killed — `11selector` |
| centring dropped (`body_x` → 0) | killed — `11selector` |
| title left unoffset | killed — `11selector` |
| hint row left unoffset | killed — `11selector` |
| `Playable` suffix dropped | killed — `11selector` |
| `Playable` suffix applied to all | killed — `11selector` |
| footer never warns | killed — `11selector` |
| warning also fires above the pane width | killed — `11selector` |
| cascade collapsed to its widest form | killed — `11selector` |
| `pick_that_fits` off by one (+4 slack) | killed — `11selector`, `26snake-ui`, `28tetris-ui` |
| `meets_floor` off by one (`>=` → `>`) | killed — `11selector`, `34geometry` |
| `meets_floor` ignores rows | killed — `11selector`, `26snake-ui`, `34geometry` |
| Snake's floor overstated by one column | killed — `11selector`, `34geometry` |
| Sokoban's kind flipped to `Drawable` | killed — `11selector`, `34geometry` |
| Minesweeper's floor pinned to Easy by hand | killed — `11selector`, `34geometry` |
| the both-or-neither schema clause relaxed | killed — **compile error** |
| the list's rect desynced from its painted frame | killed — `11selector` |

⚠ **Four of these SURVIVED the first run, and every one was a weak test rather
than weak code** — the title and hint offsets, the cascade collapse, and the
`pick_that_fits` slack. Three of the four were invisible for the same reason: the
assertion was pointed at the wrong row, or at a game whose title was too short to
cut. Mutation testing was the only thing that said so.

⚠ **A "BUILD FAILED" line is a kill when the negatives are `static_assert`s.**
`test/34geometry`'s malformed-schema cases fail the *build*, the shape
`test/33options` uses. Both mirror arms fired, which is the evidence that writing
the second one was not symmetry: with only the cols-set case, rewriting the clause
as `g.rows == 0` still rejects it and the file stays green.

⚠ The usual trap did not bite but is worth restating: removing a clamp normally
leaves an unused local, `-Werror` kills the build, and that reads as a kill with
no assertion having run. Keep the value live (`* 0` rather than deletion).

### A wide pty is now reachable, and it was not before

`script(1)` sizes its pty from stdin, which is a pipe in an agent's hands, so
every capture in this file before now was **80x24** — none of them could have seen
a ceiling that bites past 120 columns, or a truncation that bites below 30. A
~50-line `pty.fork()` + `ioctl(TIOCSWINSZ)` harness fixes that and is how the
240x40, 30x10, 22x10 and 20x8 numbers above were measured. Worth rebuilding
rather than working around the next time a size-dependent claim needs checking.

The bare-tier session at 30x10 is **zero bytes ≥ 0x80** over the whole capture —
the whole-session analogue of `all_seven_bit()`, covering the new footer strings.

### What test/34geometry is for, and what it is not

The tautology it refuses to be: `CHECK(Snake::kMeta.geometry.cols ==
snake::kNeedCols)` asserts that a line of code says what it says. What it asserts
instead is that **the declared floor is the actual boundary** — at that size the
game's own `compute_layout` reports `fits`, and one column narrower or one row
shorter it does not.

⚠ **Both directions, or it proves half of nothing.** "It fits at the declared
size" alone is satisfied by any floor at or above the truth, so a game claiming
200x200 would sail through; the narrower-does-not-fit half pins it from the other
side. That is the mutation "Snake's floor overstated by one column" catches.

⚠ The Shell comparison (`no game asks for less than the Shell itself needs`)
lives in the test and **not** in `game_meta.hpp`, because `GameMeta` is in
`glyphcade_core`, which sits below the Shell in the link chain precisely so a
game cannot reach it. A test is above both.

**32 suites, up from 31.** Four configurations green — GCC with audio
auto-detected, GCC with audio OFF, Clang, TSan — with `-Werror` throughout,
`artifact-check` printing `CLEAN`, and `pty-restore`, `audio-export-clean` and
`consumer-resolves` registered in all four.

⚠ **And `34geometry` itself was ABSENT from the TSan build for one run**, because
that build directory had been reconfigured while the new test dir was stashed, so
the `file(GLOB)` never saw it. It reported `32/32` on the other three and `31/31`
under TSan, which looks like a pass. AGENTS.md's "absent means skipped, not
passed" is about the three conditional tests; it applies to a brand-new suite
just as hard, and the way to catch it is to diff `ctest -N` between builds rather
than to read the totals.

### One thing deliberately not fixed here

The detail pane is a `TextBox`, a chat-scrollback widget that **auto-scrolls to
the bottom**, so once its content overflows, the game's description scrolls off
the top and the first visible line starts mid-word. That is pre-existing — the
"detail pane's scrollbar is 7-bit too" case drives rows 8..12 precisely because
the description already overflowed — but the new `size:` line is one more row, so
it becomes reachable one terminal size earlier (at 80x16 rather than 80x15).
Filed as [#13](https://github.com/gobha-me/glyphcade/issues/13) rather than
bundled: fixing the scroll position needs its own cases, and a pane that pins to
the bottom of a description is a different defect from a pane that lacks a size
line.

---

## What the options screen is (term-game#38)

**The shape overrules the issue's own recommendation, which invited that.** #38
offered a shared core helper *or* a declarative `GameMeta` + Shell renderer. What
landed is the schema from the second and the renderer from the first, because
the issue names **two** defects and shape A only fixes one — the detail pane, the
one screen you see *before* pressing Enter, never mentioned the settings existed.
The argument is a comment on the issue.

- `GameMeta` grows `std::span<const OptionSpec> options`, and **stays a literal
  type** — the property `game_meta.hpp` says is load-bearing, so the registry
  array is still `constexpr` and the new rules are `static_assert`s.
- **The Shell only READS it**, to advertise settings in the detail pane.
  `Shell::State` is still `{Selector, InGame, Paused}`, `game.hpp` is untouched,
  `enter_selected_game` is untouched.
- **The game draws its own screen**, via `OptionsScreen` in `glyphcade_core`, in
  the same arm where it already draws `draw_too_small()`. "A running game owns
  the whole Screen" stays true.
- One schema, two consumers — which is what stops the menu advertising an option
  the game does not have.

`state()` is `InGame` from the first Enter, so **~110 existing test call sites
needed no edit**. Six `enter_*` helpers gained one line each (six, not the four
the issue predicted: `test/11selector` and `test/13tick` carry their own).

### ⚠ Leaving that helper edit out is a HANG, not a red test

Several `15minesweeper-ui` cases steer with
`while (cursor().row < N) dispatch(Down)` — a loop **bounded by the code under
test**. With the options screen up the arrows move a cycler instead of the
cursor, the predicate never becomes true, and `ctest` span for six minutes before
it was killed. Same family as Epic 7's mutation-harness hang.

### The cycler joins the glyph tier (term-game#45)

The cycler drew a literal `<` and `>` at every tier. termforge v0.6.0 — ours
since #36 — put `arrow_left`/`arrow_right` in `MarkGlyphs`, so it now draws `‹`
and `›` where the terminal can and the same `<`/`>` where it cannot. Four lines
in `draw_cycler`.

⚠ **The old code was correct at the ASCII tier and only wrong above it**, which
is why the pty evidence is two-sided: at the colour tier `›` goes **0 → 1** and
`Easy >` goes **1 → 0**; at the bare tier the capture is unchanged, `Easy >`
still 1 and zero high bytes. A one-sided capture cannot tell "the fix landed"
from "the screen stopped drawing".

**`test/33options` is now the only place in the repo that renders above the
ASCII tier.** Every other suite goes through `test_run_frames`, which is nailed
to a `FallbackDriver` whose `capabilities()` is an all-false literal — that is
[#11](https://github.com/gobha-me/glyphcade/issues/11), and this issue routed around it rather than waiting: `OptionsScreen`
takes a `GameContext*`, `set_border_style` is public plumbing, and the screen is
directly constructible. Before #45, `draw_cycler`'s and `draw_list`'s non-ASCII
branch had **never been executed by a test**.

⚠ **The width arithmetic needed no change, and that is the finding.** `‹ › ▸`
are three bytes and **one column** each (termforge's `kWide` table starts at
U+2E80), so the layout is identical at both tiers and only the byte count moves.
The comment at the `truncate_to_width` call used to justify itself with "may be a
two-column glyph", which is false for U+25B8; it now says multi-byte.

⚠ **`substr(0, cols)` is not caught by asserting whole UTF-8 per cell.** The
obvious guess — that substr leaves half a glyph in a cell — is wrong, because
`write_text` **sanitizes** and the orphaned bytes are silently dropped. What
substr actually costs is **columns**: the Unicode row renders shorter than the
ASCII row on the same screen. Only a case at a width that actually truncates
kills it, and it survived a 4x90 sweep before that was understood.

⚠ **`can_right` was half-tested since #38.** `can_right = true` survived every
case, because no fixture had ever put a value on its **last** choice — the left
end was covered only because "Walls" happens to start there. Older than #45; the
glyph swap is just what made it visible.

Seven mutations, two survivors, both findings, both now killed.

### ⚠ Three minesweeper cases assumed a cursor at (0,0), and one was vacuous

Dismissal goes through `new_game()`, which recentres the cursor exactly as
`1`/`2`/`3` always did. Two cases then walked past their target and failed
loudly. The third — *"chording with no flags is silent"* — **was passing for the
wrong reason**: (4,4) is an unrevealed mine, so `c` was being refused for "the
cell is not revealed" rather than for "the flags do not add up", and the guard
under test was never reached. All four walks are now bidirectional and assert
where they landed.

### `preselect()` is why a default is not always `default_index`

Sokoban's default is "the first level you have not solved" — a function of the
score store, not a constant, so it cannot be `constexpr` and `GameMeta` cannot
hold it. `default_index` therefore means **what the selector advertises** and
`preselect()` means **what the game starts on**. Without the split either the
pane lies or the schema stops being constexpr.

⚠ `Sokoban::options()` is a test seam because `index()` **cannot** witness
`preselect()`: `start()` calls `load(start_at)` before opening the picker, so
the game is already on the right level whether or not the picker agrees.

### The list mode, and why Sokoban shipped in the same change

Twenty choices is past `kInlineChoiceMax`, so Sokoban renders a windowed
vertical list rather than a row of `< value >` cyclers, and the detail pane
prints the **count** instead of joining twenty names into five wrapped rows.
Shipped alongside the other three rather than after them: four cyclers first
would have baked in a cycler-shaped API for Epic 8 to fight — the
"comes out Solitaire-shaped" failure #38 exists to prevent.

### One budgeted status row instead of four, and the numbers that justify it

The four hand-rolled budget loops are now `hud::draw_status_row`. Measured at one
site instead of four:

| mutation | suites red |
|---|---|
| budget effectively removed | **5** — all four games *and* `test/33options` |
| budget off by one (`>` → `>=`) | **1** — `test/33options` only |

The second row is the argument. **No game suite can see the off-by-one** — it
costs one column, and they vary the *screen* while their fields stay the same
length. Killing it needs exact arithmetic (a 14-char field against a 14-column
budget), which before this change would have had to be written four times. That
is why `tetris.cpp` recorded this mutation going green in two consecutive epics.

⚠ **The hint cascades were NOT converted**, deliberately: of five copies only
two are mechanical. Minesweeper has a `finished()` arm above its tiers, Sokoban
has two nested cascades, and Tetris builds a `std::string` for its `HoldSupport`
suffix so it needs a second overload. Filed as its own issue.

### One mutation survives on purpose

`max(0, ...)` on `word_x` in `hud.cpp` is unobservable at every width —
termforge's `write_text` already does `start_x = x < 0 ? 0 : x` — so no test can
kill it and none was contorted into pretending to. It stays because that clamp is
**not in `write_text`'s documented contract** and #36 moves the pin next.
Documented at the site as unkillable-by-design.

### ⚠ A claim of ours was wrong, and mutation testing is what said so

`Tetris::start()` was commented as needing `m_options.open()` **after**
`m_board.reset(..., support)`, on the reasoning that `new_game()` reads
`hold_support()` back off the board and would otherwise carry the constructor's
`Discrete` through, losing DAS on a kitty terminal. Swapping the two lines is
green **even on the Held arm**: `open()` never touches the board, and both calls
are in `start()`, so the reset has always happened before any dismissal can. The
comment now says the order does *not* matter and what would actually break.

That correction was only possible because the Held arm became reachable at all:
`GameContext::set_capabilities` is public, so `test/28tetris-ui` now hands a
`Tetris` a hand-built context with `kitty_keyboard` set instead of going through
the Shell. Under `test_run_frames` that arm had **never once executed**.

### What is verified, and what is not

Six configurations green — GCC, GCC without audio, Clang, TSan, ASan, UBSan, 30
suites each, `-Werror` throughout.

Verified in a pty at both tiers. The decisive check: driving
Enter → Right → Enter into Minesweeper puts **`MEDIUM`** in the status row and
**never `EASY`** — the board that started is the one that was chosen. The
**control** ran too: 2048, which declares nothing, goes straight to a board with
no options screen at any point.

⚠ Two pty greps were vacuous before they were fixed, both worth knowing about.
`Level`/`Medium` matched the **detail pane**, not the options screen — the two
are only separable because the status row shouts `MEDIUM` while the pane says
`Medium`. And `MINES` did not appear at all: the diffing renderer painted it over
`Mines`(weeper), so only the four differing cells were emitted and the stream
contains `INES`. **Adjacency in a stripped `script(1)` capture is not screen
adjacency.**

⚠ **The `KeyboardMode::Enhanced` release path is NOT verified and cannot be
here.** Tetris asks for `Enhanced`, and the Shell sets the tier inside
`enter_selected_game` — before the Enter that entered the game comes back up — so
on a kitty-protocol terminal the *release* of that keystroke reaches the options
screen and, without the guard, dismisses it before a frame is drawn.
`FallbackDriver` reports all-false capabilities and `script(1)` is not a kitty
terminal. Its only coverage is a **synthesised** `KeyAction::Release` unit case.
Needs a real kitty/foot/ghostty terminal, the same position the SFX bank has been
in since Epic 2.


## What the v0.6.0 bump brought (term-game#36)

Six tags, not the four the issue describes — it was written when upstream was at
v0.5.1, and upstream was at **v0.6.0** by the time this was implemented. Third
bump running where the issue's own picture was stale, which is why re-reading
upstream before implementing an issue *about* upstream is a rule here.

**`src/` has no behaviour change**, and that null diff is the finding: six
configurations green across four minor versions of the dependency with nothing
rewritten. The only source edit is a comment.

Reading the tags rather than their titles:

| tag | what it is | reaches us? |
|---|---|---|
| v0.3.0 | #83/#84 — `draw_image(Rect cells, …)`, `preferred_pixel_extent`, `draw_pixels` returns a borrowed `const Image*` and takes an `Extent` | **not today, but it is the point.** Breaking only for a `TerminalDriver` implementor or a `draw_pixels` override, and we are neither. ⚠ It is what **unblocks #8**: at v0.2.2 `draw_image` used an image's *pixel* dims as a *cell* count, so an atlas rendered as one flat colour per cell |
| v0.4.0 | #69 — `Widget::on_tick(dt)`; `ProgressBar`/`Button` animation became **wall-clock** | ⚠ **yes** — see below. The issue says "only if we hold one", and we hold two without naming either |
| v0.5.0 | #122 — `Widget::reset_transient()` at a Dialog showing boundary | ⚠ **yes**, and it is v0.4.0's cure |
| v0.5.1 | #123 — container overloads for `route_mouse`/`tick_widgets`, `route_mouse` skips nulls | no. Our one call passes a braced list of one non-null pointer; the `initializer_list` overload still wins |
| v0.5.2 | #102 — `Screen::fill_rect` clips via `Rect::intersect` instead of `x + w` in `int` | no. Our three call sites pass small in-bounds values, so the arithmetic is identical for every input we produce |
| v0.6.0 | #22 — `TabBar`, and `MarkGlyphs` grew `arrow_left`/`arrow_right` (`all()` 9 → 11) | no. We read `.selector` **by name**, never `all()`, never an aggregate initialiser. ⚠ The options cycler hardcodes `<`/`>`, which is what the new fields are for — correct at the ASCII tier, wrong above it. `term-game#45` |

### ⚠ The one thing that reached us, and the issue is wrong about it twice

Activating a `ConfirmDialog`'s button arms a press flash **and closes the dialog
in the same dispatch**, so the flash never renders in the showing that armed it.
What clears it afterwards is what moved — measured across three pins, not read
off the release notes:

| pin | 1st paint of the next showing | 2nd paint | `test/11selector` |
|---|---|---|---|
| v0.2.2 | **LIT** | clear | next-showing RED, outlive-one-paint GREEN |
| v0.4.0 | **LIT** | **LIT** | both RED |
| v0.6.0 | clear | clear | both GREEN |

⚠ **v0.4.0 did not introduce this — we were shipping it.** At v0.2.2
`Button::draw()` cleared the flag *after* painting with it, so every re-opening
of the pause dialog showed one frame of a wrongly-lit Resume button. v0.4.0
(#69) made the flash a wall-clock countdown in `Widget::on_tick`; `App` keeps no
widget registry and `Shell::on_tick` forwards only to `m_game`, so nothing ticks
`m_pause` and one frame becomes **permanent**. v0.5.0 (#122) clears it at the
showing boundary before anything paints, and cures both.

Confirmed in a pty, old binary against new, identical keystrokes and controls:
the pressed background `48;2;128;64;255` appears **once** on v0.2.2 and **zero**
times on v0.6.0. So this bump is the first that **fixed a live defect** rather
than only staying current.

⚠ **term-game#36's table is wrong on both rows** — it calls v0.4.0 conditional on
holding a `ProgressBar` or `Button` and v0.5.0 unreachable. And the audit it
prescribes, grepping for those two type names, comes back **CLEAN and is
therefore misleading**: the Buttons are inside `ConfirmDialog`, and our source
never names them. The same blind spot shows up in the ABI floor —
`sizeof(Button)` grew, so `sizeof(Shell)` moved through a class our installed
header does not mention, and `Widget`'s two new virtuals were **inserted at
slots 3–4 ahead of six existing ones**, so a mixed-version consumer gets
`hit_test` dispatched into `draw_pixels` rather than a missing symbol.
**Auditing the widget types a header names is not sufficient; transitive members
and vtable layout both count.**

### We do not forward ticks, and only half of that is testable

`tick_widgets(dt, {&m_pause})` is the line this looks like it wants, and it is
not added. To be correct it would sit *above* the pause gate — a paused game's
dialog animating while the simulation does not — in the eight lines of
`Shell::on_tick` that already cost the most to re-derive; and upstream's
`dialogs.hpp` says outright that the three standard dialogs need no ticks. We
pin the **behaviour** rather than that doc comment.

⚠ The test guards the *removal* direction only: it goes red if upstream stops
clearing at the boundary, or if we push an overlay that does not close on
activation. **Adding the line is harmless and stays green**, so only the comment
at the call site guards that half. Said out loud rather than implied, because a
comment claiming a test it does not have is the failure this file exists to
prevent.

### Two red arms, because one could not have told them apart

The reflex from #24 is to build against the previous pin and expect green. That
prediction was **written into `cmake/deps/termforge.cmake` before the arms were
run, and it was wrong** — v0.2.2 came back red. The three columns above need
both arms *and* two test cases: a `REQUIRE` aborts its case, so one case can
only ever report the first claim.

On both old arms `11selector` is the **only** red suite, 29 of 30 green. The
isolation is the evidence, not the failure.

### What is verified, and what is not

Six configurations green — GCC, GCC without audio, Clang, ASan, UBSan, TSan, 30
suites each, `-Werror` throughout. Verified in a pty at **both** tiers, with
controls: the "pressed colour never appears" check was **vacuous on the first
attempt** — grepping the raw capture for `Resume` returns 0 at every pin,
because the renderer positions every cell and screen adjacency is not byte
adjacency. Four controls now run, including that the button's *focused*
background does render, which is what shows the grep can match a Button's own
colour at all.

✅ **`find_dependency(termforge 0.6.0)` used to be exercised by nothing in
`ctest`.** That was 
`term-game#46`, closed by the
`consumer-resolves` check below.

### The pause dialog was the one widget the tier never reached — fixed

`term-game#44`, landed on its
own after #36. `Dialog` owns a `Frame` privately, that `Frame` defaults to
`BorderStyle::Single`, and nothing in `Shell` ever called `set_border_style` —
so the pause dialog painted U+250C/U+2500/U+2502 onto terminals that had just
reported no colour, in every release since the dialog existed.

**Found by a test *capability*, not by a feature.** `Probe::paint_overlay_pass`
exists because `App::frame_step` restores the backdrop before it returns, so
**no test in this repo could read an overlay's cells** until #36 added it. The
moment they could, this became assertable. Measured at both pins, so it is
**pre-existing and not the bump's** — identical glyph sets, counts differing
only by showing count — which is why it was deliberately left for its own issue:
an unrelated red inside a pin bump destroys the "nothing else moved" signal that
is the whole point of doing one alone. Same story as the detail pane's scrollbar
in Epic 6.

**The fix is one line in `open_pause()`**, beside the `set_default(false)` that
is there for the same shape of reason:

```cpp
m_pause.set_border_style(m_ctx.border_style());
```

⚠ **Not in `sync_capabilities()`.** The `rebuild_list()` call there looks like
the precedent and is not — `rebuild_list()` sits at probe time because it
*cannot* be done per frame (`set_items` resets the scroll offset). It is the
exception the repo was forced into. Every *other* tier-derived style is
re-pushed at the moment of use; `draw_selector` does it four times a frame. And
`open_pause()` reads `m_ctx`, never `driver()`, so it is safe at any time —
whereas a one-shot at probe time could never reach an overlay built after the
first frame. The rule this generalises is now in DESIGN.md's "Graphics tiers".

**Two things the issue did not know.**

1. **The title delimiters leak from the same table.** `Frame::draw` takes
   `title_left`/`title_right` out of `border_glyphs(style)` too, so the dialog
   read `┤ Paused ├` at the floor — not just a box-drawn ring.
2. **It was wrong at the colour tier as well**, as a consistency defect.
   `sync_capabilities` only ever answers `Ascii` or `Rounded`, so **`Single` is a
   family this application never chooses** — the dialog drew `┌┐└┘` while every
   other frame drew `╭╮╰╯`. That is also what makes `┌` a *tier-independent*
   discriminator in a pty: its only possible source anywhere in the binary is an
   unstyled `Dialog`.

**The audit** (the issue's second scope bullet) — `set_border_style` is a
complete fix, and complete for the whole `Dialog` family, not just `Confirm`:

| surface | tier-sensitive? |
|---|---|
| `Dialog`'s frame ring | **yes** — the bug |
| `Dialog`'s title chrome | **yes** — `┤ Paused ├`, not in the issue |
| interior fill, body text | no — colour only; our text is ASCII and `wrap_to_width` inserts nothing |
| `ConfirmDialog`'s two `Button`s | **no** — `fill_rect` + centred label, no `BorderStyle`, no glyph table; only axis is colour |
| button labels | no — ASCII `[ ]` brackets, ASCII labels |
| scrollbar / `MarkGlyphs` | `Dialog` owns neither |

**pty counts**, same capture driven against the pre-fix and fixed binaries — the
paired evidence a single arm cannot produce. Controls first (`Paused` and
`Resume` both present in all four captures, or every count below is vacuous):

| capture | ┌ | ┐ | └ | ┘ | ╭ | ┤ |
|---|---|---|---|---|---|---|
| bare, pre-fix | 1 | 1 | 1 | 1 | 0 | 1 |
| **bare, fixed** | **0** | **0** | **0** | **0** | 0 | **0** |
| colour, pre-fix | 1 | 1 | 1 | 1 | 7 | 8 |
| **colour, fixed** | **0** | **0** | **0** | **0** | **8** | 8 |

At the bare tier the dialog was the *only* source of all four `Single` corners —
the game underneath draws `+-|` — and the whole session is now 7-bit by the
`LC_ALL=C grep -P '[\x80-\xff]'` sweep. At the colour tier **`╭` goes 7 → 8**:
the positive half, showing the dialog did not merely stop drawing but joined the
rounded family. `┤` and `│` correctly do *not* move there, because `Rounded`
shares both with `Single`; only the corners differ.

**Mutations**, three rows:

| mutation | result |
|---|---|
| `set_border_style(Single)` | RED — `test/11selector` |
| `set_border_style(Rounded)` hardcoded | RED — `test/11selector` |
| `set_border_style(Ascii)` hardcoded | **GREEN** — the blind spot, named below |

⚠ **The Rounded arm is pty-only, and the headless cases pin the tier's OUTPUT
rather than the fact that it was derived.** At the ASCII tier
`set_border_style(m_ctx.border_style())` and a hardcoded
`set_border_style(Ascii)` are indistinguishable, and no cheap seam changes that:
`test_wire_headless` is private, hardcodes the `FallbackDriver`, and that
driver's `capabilities()` is an all-false literal, so `TERM=` in a ctest
environment does nothing. The only seam that would work is a private virtual
capability probe on `Shell` — real production code, and out of scope for a
focused fix. Filed as
[#11](https://github.com/gobha-me/glyphcade/issues/11); it would unlock the
`▸` marker, the
`↑↓ select` hint row, the *absence* of the colour notice, and the notice-ordering
contract as well, all of which are pty-only today. Trap 3 in `test/11selector`
records the limit at the site.

### A dependency reached into, and nothing recorded it

Six of our headers call `termforge::detail::display_width` /
`truncate_to_width`, several inside `static_assert`s
(`arcade/game_meta.hpp`, the five `games/*/glyphs.hpp`). `widgets/detail/` is
byte-identical across all six tags so this bump is safe, but a change there
would be a hard compile break in a `constexpr` context, not a warning. Written
down here for the first time.

---

## Upstream had moved four tags past our pin, and it changed nothing then

Checked at the start of Epic 7, because this project has now found the blocking
picture staler than the issue describing it four times. Superseded by the
section above, and kept because the reasoning is still the precedent.

**`MapWidget` and `Image` are byte-identical from v0.2.2 to v0.6.0** — `git diff`
over `map_widget.*`, `image.cpp`, `image_loader.*` and their suites is empty. So
the bump buys Epic 7 nothing, and it was deliberately not bundled into it: the
whole signal of a pin bump is "nothing else moved", and burying it in an epic
destroys that. Same reasoning that made term-game#24 its own issue — and it became
`term-game#36`, which also
carried the `Widget::on_tick` audit the bump needed.

⚠ **The `MapWidget` sprite tier still does not exist at any tag.** Its gates
(#83/#84) landed and `docs/map-widget.md` was updated to record that they had —
but no implementation followed. **Epic 8 (Solitaire) is written against cards as
pixel sprites**, so the first thing that epic must do is decide between waiting
on upstream, driving `Image`/`draw_image` directly from game code, or shipping
the glyph tier first. That is a real planning input and it is not visible from
the issue.

---

## What Epic 7 built (term-game#8)

Sokoban, in six headers and three TUs — Tetris' five-header shape plus a
level-pack header, and one more TU than any other game:

- **[`level.hpp`](include/glyphcade/games/sokoban/level.hpp)** + **`level.cpp`** —
  the standard Sokoban charset (`#` `@` `$` `.` `*` `+` and space) and a `parse()`
  that is total: it returns a `ParseError`, never throws, and refuses seven kinds
  of malformed level. **Its own header and its own TU** because the format is
  separable from the rules — a level is validated once, at load, and nothing
  looks at a character again — and because it is the only part of this game a
  future level-FILE loader would reuse unchanged.
- **[`levels.hpp`](include/glyphcade/games/sokoban/levels.hpp)** — the twenty maps,
  verbatim from the reference. Its own header for the same reason
  `tetris/pieces.hpp` is: it is the part taken verbatim, and that boundary is
  worth seeing in a file list. **The pars are not verbatim** — see below.
- **[`board.hpp`](include/glyphcade/games/sokoban/board.hpp)** + **`board.cpp`** —
  push rules, undo, counters, win detection, deadlock detection. No termforge
  header, so `test/31sokoban` *cannot* reach a `Screen`.
- **[`layout.hpp`](include/glyphcade/games/sokoban/layout.hpp)** — a viewport rect
  for a camera, not a coordinate per cell. **34x12 needed.**
- **[`glyphs.hpp`](include/glyphcade/games/sokoban/glyphs.hpp)** — two tiers, three
  `static_assert`s.
- **[`sokoban.hpp`](include/glyphcade/games/sokoban/sokoban.hpp)** — the `Game`,
  and the only file that knows `Screen`, `Event`, `MapWidget` or `GameContext`
  exist.

### The first game with no clock, and the first with no fixed board size

Snake has one accumulator, 2048 one tween, Tetris five — and every one of them
shipped a bug a test found. Sokoban advances only when a key is pressed, so
there is no `tick()` override at all. What takes the clock's place as the thing
most likely to be wrong is **undo**, and it is a `{Dir, bool pushed}` record
rather than the reference's full board copy per move (`game.js:216-224`): a push
is reversible by construction, so three bytes invert a move exactly. That is the
difference between "unlimited undo" as a promise and as a leak.

It is also the first game whose board size is not a compile-time constant. There
are twenty of them, and a camera covers the difference.

### Nine reference defects, and the pars are the interesting one

The push rules in `sokoban/js/game.js` are correct; the value of this port is
everywhere around them.

| # | Reference | Here |
|---|---|---|
| 1 | **Ragged rows walk the player out of the level.** `isValid` bounds columns with `board[0].length` (`:213`) while `render` walks `board[r].length` (`:124`) — and the published corpus has trailing spaces trimmed, so rows *are* ragged. A short row yields `undefined`, and `undefined !== WALL` passes | padded to a rectangle at parse time, **and** off-grid reads answer `Wall` |
| 2 | **No deadlock detection whatsoever.** Push a crate into a corner and it lets you keep playing a level that cannot be won | a frozen-crate detector, announced in the hint row |
| 3 | **`levelComplete` is set on one path and cleared on another.** Dismissing the celebration overlay by clicking its backdrop (`:304-306`) leaves a frozen board with undo AND reset greyed out — and on level 20 there is no Next to escape with | no such flag; undo and reset stay live after a win |
| 4 | **A pre-solved level can never be won.** `checkWin()` is reachable only from `move()` (`:209`) | evaluated in the constructor too |
| 5 | **The persisted level index is never range-checked.** `loadLevel` returns before its first `render()` for an out-of-range index (`:88`), leaving a blank board and no message — and that level set has been replaced wholesale twice in its own git history | the resume point is **derived** from the score store, so it cannot go stale; `load()` clamps |
| 6 | **Boxes and goals are never counted against each other.** `checkWin` scans only for a remaining `$` (`:251-256`), so more goals than boxes wins with empty goals showing | a parse error |
| 7 | **Two players is a silent choice** — the scan keeps the last match (`:97-104`) | a parse error |
| 8 | **Player-on-goal is invisible.** `.cell.target::after` and `.cell.player::after` decorate the same pseudo-element with equal specificity (`style.css:248`, `:311`), so standing on a goal erases its marker | its own glyph in both tiers, enforced by the distinctness `static_assert` |
| 9 | **"New Best!" fires on a tie** (`:280`), because the record is only overwritten on a strict improvement | monotone `Better::Lower`, and no banner claims otherwise |

### The pars: dead data, and wrong

The reference ships a `par` on every level and its README says they are "derived
from the optimal solution length". Neither half survives contact. `par` is never
read anywhere in `game.js` — twenty numbers ship as dead data — and every one of
the twenty was BFS-solved for the true minimum move count before `levels.hpp` was
written:

| lvl | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| ref | 5 | 8 | 8 | 9 | 12 | 12 | **16** | **14** | **14** | **18** |
| true | 3 | 5 | 5 | 6 | 8 | 11 | **17** | **16** | **15** | **19** |

| lvl | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 |
|---|---|---|---|---|---|---|---|---|---|---|
| ref | **14** | **26** | **26** | 26 | **28** | 30 | 36 | 36 | 36 | 40 |
| true | **16** | **41** | **34** | 26 | **37** | 28 | 35 | 36 | 36 | 38 |

**Three are right** (14, 18, 19), nine are loose, and **eight are below the
mathematical minimum for their own level** — targets no sequence of moves can
reach, in a game where par is the only thing telling a player how they did. The
reference cites its verifier as `/tmp/sokoban_design.py`, a path outside its own
repository, so the claim was never reproducible either.

We ship the measured numbers, and `test/31sokoban` re-derives the tutorial five
with a BFS at test time so the method is checkable rather than asserted. The
four-crate levels take minutes to solve and are deliberately left out of that
case, which is said out loud rather than papered over.

⚠ Second epic running where the reference's **documentation** was the wrong half
— Tetris' README speed table was off by one level from its own code. **Generate
from code, not from prose.**

### What MapWidget's first consumer found

Four pieces of friction, each commented at the site that pays for it. None was a
blocker; all four are worth having before the API freezes, which is what term-game#8
volunteered us for.

1. **`set_map_size()` wipes every layer** while its own comment says it preserves
   the overlapping corner "like `Screen::resize` does". Loading a level must
   therefore size first and populate second, and a second call with identical
   dimensions still throws the map away. Filed as termforge
   [#127](https://github.com/gobha-me/termforge/issues/127).
2. **There is no `tile_at(cell_x, cell_y)`.** Turning a click into a tile means
   re-deriving `camera()`, the tile size and the floored viewport extent out in
   app code — the widget's own private `viewport_tiles()`. That is precisely the
   arithmetic the design doc says an app should not do, in the paragraph
   explaining why the widget owns the camera. Filed as termforge
   [#128](https://github.com/gobha-me/termforge/issues/128), and it is the one
   gap here that cost a workaround rather than a convention.
3. **Tile id 0 means transparent and `kEmptyId` is private**, so the convention
   is one a consumer must know rather than name. Our `Tile` enum starts at 1.
4. **`TileDef::glyph` is documented as one grapheme**, but tile size is declared
   in CELLS and the doc calls non-square the expected case. A `{2,1}` tile with a
   one-grapheme glyph is half glyph and half background fill. Two-column strings
   work — `write_text` lays out text — but the contract does not say so.

⚠ And the sprite tier **still does not exist at any tag**, v0.2.2 through v0.6.0.
Its gates lifted and the doc was updated to say so; no code followed. That is
Epic 8's problem, not this one's.

### A control run corrected AGENTS.md

Sokoban declares `KeyboardMode::Legacy`, which makes it the natural **control**
for the pty recipe Epic 6 added — a real game that asks for nothing. Running that
control is what showed the recipe itself was wrong.

Three captures, side by side:

| run | `ESC[>27u` push | `ESC[=0;1u` restore | `ESC[<u` pop |
|---|---|---|---|
| selector only, no game | 0 | 0 | **1** |
| Sokoban only | 0 | 0 | **1** |
| Tetris only | 1 | 1 | 1 |

AGENTS.md said all **three** counts must be zero in the control. The third never
can be: `ESC[<u` comes from `detail::kLeaveSequence`, a fixed string literal that
`Terminal::leave_screen()` emits unconditionally, so it is 1 in every run
including one that never enters a game. A control that fails by construction is
worse than no control — it invites someone to go "fix" a game that is behaving
correctly. Corrected in AGENTS.md, with the measurement beside it.

⚠ The first two counts are exactly the evidence they were meant to be: Sokoban
is byte-identical to the no-game control, and Tetris is the positive control
proving the measurement can see a push at all.

### 34x12: a floor that is an opinion, not arithmetic

Minesweeper needs 63x20 because Hard is 30 cells wide. Snake needs 58x20, Tetris
35x24. Every one of those is a consequence: the board has one size and the
terminal either has room or does not.

Sokoban has a camera, so **there is no size at which a level cannot be drawn** —
a level larger than the window scrolls. What is left is a floor on being
*playable*: below about sixteen tiles across you cannot see enough of a room to
plan a push. That is a judgement.

`term-game#15` is therefore
deferred a **fifth** time, and for a new reason rather than the same one. A
`GameMeta::min_cols` would sit Minesweeper's 63 — derivable — next to Sokoban's
34 — an opinion — and invite the selector to treat them as the same kind of fact.
The four earlier deferrals said "not yet"; this one says the field is the wrong
shape.

⚠ **Superseded, and the objection above is what shaped the answer.** #15 landed
with #42 in v0.18.0: the kind travels with the number (`SizeFloor::Drawable`
against `Playable`), Sokoban is the roster's one `Playable`, and the selector
prints "recommended" here where it prints "minimum" everywhere else. See "What
the geometry block is" above. The reason this section gives for deferring was
right — it was a specification of the field, not an argument against having one.

### Audio: one new id, the fewest of any game

term-game#8 asks for "step, push, crate-on-goal, level complete" and gets **one**
new `SfxId`. A step is `Click` (the most generic gesture in the suite), a push is
`Slide` (already 2048's slide and Tetris' hard drop), and finishing is `Win`.
Only seating a crate on its goal had nothing that meant it — and it is not
`Merge`, because nothing combines and nothing vanishes.

⚠ There is deliberately no sound for a **blocked** move. A player walking into a
wall is holding a direction, so a rejection tone fires as fast as the key
repeats. That is the metronome argument for the fourth game running.

### Scores: one key family, and a count that is derived

Twenty keys, `best_moves_01` … `best_moves_20`, `Better::Lower`, as a **table**
rather than the twenty-arm switch the other games' three-way enums would suggest.
The property the switch was protecting is kept: the key is the level's ordinal,
never its display name, so renaming "Two Texts" cannot orphan a record.

There is no `solved_count` key. It is **derived** by counting levels that have a
record, which is also what makes "resume at the first unsolved level" impossible
to desynchronise from the records themselves.

### Six of twenty-six mutations went green, and the harness itself was two of the findings

Every epic since v0.6.0 has run a mutation pass; this is the first where the
**harness** produced as much as the code did.

Six survivors on the first pass, every one a real gap, and five of them share a
shape: **a fixture that could only fail one way**.

- **`seated` / `unseated` losing their guard** (`now_on_goal && !was_on_goal` →
  `now_on_goal`). Every push in the suite went between a goal and plain floor, so
  the two spellings agreed everywhere. **Goal-to-goal** is the only arrangement
  that separates them — and it is audible, because `seated` is what plays the
  Seat effect.
- **`blocked_on_axis` losing its far side** (`is_wall(a) || is_wall(b)` →
  `is_wall(a)`). Every corner fixture in the file was the **top-left** corner, so
  each axis was only ever blocked by its negative neighbour and half the
  condition was never evaluated. Needed a bottom-right corner.
- **The freeze recursion's assumption stack returning `false`.** The case named
  "two crates bracing each other" did not exercise it: its left crate was in a
  genuine corner, so the answer resolved by walls alone and the stack was never
  consulted. A true mutual dependency needs **neither crate cornered** — open
  floor at both ends of the pair, where each one's immovability is derived only
  from the other's.
- **Ragged width taken from the first row** — i.e. the reference's own
  `board[0].length` bug, reintroduced. The ragged fixture had its **widest row
  first**, so `max` and `first` agreed.
- **The overlay marking a crate frozen ON its goal.** No case had ever rendered a
  crate that was both frozen and finished. Level 18 ("Four Corners") puts all
  four goals in corners, so it is exactly the level where a player is meant to
  end up there.
- **The click direction test relaxed** (`dy == -1` → `dy <= -1`). Every
  "too far away" click in the suite was **horizontal**, so the vertical
  comparison was never bounded. Each of the four directions needs its own
  out-of-range click.

After closing all six, a second pass ran **26 of 26 red**.

#### The harness found two things about itself

⚠ **`grep -F -c` cannot count a multi-line pattern.** It counts LINES CONTAINING
a pattern, so the "refuse an ambiguous pattern" guard — carried forward from Epic
5, where a `sed` spanning two lines silently never applied — reported six
multi-line mutations as "9 matches" and skipped every one. The guard fired
instead of the edit. That is the safe direction and it is still six mutations
that did not run. Counted in python now.

⚠ **A killed harness leaves the mutation in the working tree.** Epic 6's lesson
was "rebuild after restoring"; this is the half it missed. The restore ran at the
end of a function, so when a mutation **hung** the run and the harness was
killed, `++m_moves` stayed deleted from `board.cpp` — and the next ordinary
`ctest` would have reported it as the tree's. Now a `trap ... EXIT INT TERM`
makes the restore unconditional, and `ctest` runs under a `timeout`.

⚠ **And what hung it was a defect in a test.** `test/32sokoban-ui` bounded a
solve loop with `b->moves() > 200` — the model's own counter, which is exactly
what the mutation deletes. Frozen at zero, the loop ran forever. **A test whose
termination depends on the code under test is not a test**; the bound is a local
counter now. Worth generalising: any loop in a test that reads state from the
system under test to decide when to stop has this failure mode, and a mutation
pass is how you find out.

### What Epic 7 deliberately did not build

- **Loading levels from a file.** The parser takes the standard charset, which is
  what term-game#8's "the corpus loads directly" actually asks for, and `parse()` is
  the one piece a future file loader would reuse unchanged — that is why it is
  its own header and its own TU. But no I/O was added: `scores.cpp` is the only
  file in the library allowed to name a real path, and it earned that with a
  whole issue's worth of design. A level loader is its own change.
- **Levels of our own, large enough to scroll.** Twenty BFS-verified levels
  already exist; authoring good big ones is a design job, not a porting one, and
  a level we could not verify is worse than no level. The consequence is stated
  rather than hidden: **the camera is exercised by the tests and not by play.**
- **Pathfinding on a mouse click.** A click on an adjacent tile is a direction; a
  click across the room would be a mechanic the reference does not have, and
  `snake/layout.hpp` already drew that line for clicks.
- **Smooth scrolling.** `docs/map-widget.md` puts sub-tile scrolling permanently
  out of scope for the glyph tier — a cell grid cannot express half a tile — and
  half-supporting it on one tier is what that doc explicitly refuses.
- **A sprite tier.** It does not exist upstream at any tag. See above.
- **A hint or auto-solve.** The deadlock detector says *you cannot win from
  here*; it deliberately does not say *do this next*. Solving is the game.

## What Epic 6 built (term-game#7)

Tetris, in five headers and two TUs — Snake's shape, plus one:

- **[`pieces.hpp`](include/glyphcade/games/tetris/pieces.hpp)** — the seven
  tetrominoes' four rotations and both SRS kick tables. **Its own header because
  it is the one part of the reference taken verbatim**, and that boundary is
  worth seeing in a file list. Two `static_assert`s: every state has exactly
  four cells, and nothing is set outside a piece's own bounding box (kick
  offsets are relative to that box, so a stray cell moves every kick).
- **[`board.hpp`](include/glyphcade/games/tetris/board.hpp)** — the rules and all
  five clocks. No termforge header, so `test/27tetris` *cannot* reach a Screen.
- **[`layout.hpp`](include/glyphcade/games/tetris/layout.hpp)** — 10x20 cells at
  two columns each, plus a 12-column panel. **35x24 needed.**
- **[`glyphs.hpp`](include/glyphcade/games/tetris/glyphs.hpp)** — two tiers, three
  `static_assert`s.
- **[`tetris.hpp`](include/glyphcade/games/tetris/tetris.hpp)** — the `Game`, and
  the only file that knows `Screen`, `Event` or `GameContext` exist.

### 24 rows: the first game that does not fit in twenty

Minesweeper Hard is 63x20, 2048 is 29x19, Snake is 58x20. A Tetris well is
twenty cells tall *before* any chrome, so this one needs **24** — the classic
80x24 exactly, with nothing to spare, which is also the UI probes' default size.
Any chrome beyond the status and hint rows and it stops fitting the terminal
most people still have.

`term-game#15` (a minimum size
in `GameMeta`) is deferred a **fourth** time, and the answer is the same as the
other three games': a game-owned "does not fit" screen.

### Five accumulators, and none of them is the one AGENTS.md bans

Gravity, lock delay, shift auto-repeat, soft-drop repeat and the line-clear
freeze all turn a fixed 1/60 s `dt` into events at rates 60 Hz cannot express —
gravity alone runs 1000 ms down to 50 ms. Snake established the distinction for
one; this game has five and a `Repeater` type to hold them. The table in "What
Epic 5 built" is still the reference.

⚠ **The lock clock is credited only time from ticks that BEGAN with the piece
grounded.** Otherwise one coarse tick both lands a piece and expires its 500 ms
delay, and the player gets no slide window. In production `dt` is 1/60 s so the
difference is one frame — but a rule that is only true at one `dt` will surprise
someone, and a mutation proved no case could see it until one drove coarsely.

### DAS, and the arm this container cannot reach

`HoldSupport` is a **parameter of the model**, not a guess inside the view.
Without the kitty keyboard protocol no `Release` ever arrives, so "held" and
"pressed again" are the same event and DAS is not reconstructible — auto-
repeating anyway would slide the piece on a key the player let go of.

The degraded arm is **visible in the hint row**, not merely raised as an
`ErrorEvent`: that notice lands on the selector's footer, which nobody is
looking at while playing.

⚠ **Every check in this repo runs on the degraded arm.** `test_run_frames`
installs a `FallbackDriver` whose capabilities are all false, and no terminal
here implements the protocol. The held arm is covered on the model and nowhere
else.

### Nine reference defects, fixed rather than ported

The rotation data is good; the control flow is not.

| Reference | Ours |
|---|---|
| `game.js:66` — `lockPiece` ends with `canHold = false`, nothing ever sets it true, so **hold works once per game** and the box is greyed out afterwards | re-armed on lock |
| `game.js:347` — gravity **assigns** the frame timestamp instead of subtracting the interval. Byte for byte Snake's `game.js:78` | accumulate and subtract |
| `game.js:420-422` — soft drop runs **every frame**: ~60 cells/s and a point per cell per frame | a real repeat rate (40 ms, ours — the reference has none) |
| `game.js:103-123` — **T-spin has no rotation gate**, is evaluated after the piece is written and before rows clear, and its mini branch is dead code | a rotation flag, evaluated before the clear, mini by front corners and kick index |
| `renderer.js:101` — the line-clear animation's progress is written twice, read once, **incremented nowhere**: a 300 ms freeze that draws nothing. The 2048 slide, again | a dt-driven flash over rows still on the board |
| `game.js:18-21` — no spawn buffer, and the lock loop `return`s **mid-write**, leaving the board half-updated | two hidden rows; a lock always completes |
| `state.js` — spawn never resets the drop clock, so a fresh piece can fall on its first frame | reset on spawn |
| `game.js:308-311` — **hold does not validate** the swapped-in piece | the whole swap is refused if it does not fit |
| `README.md` — the speed table is **off by one level** from its own `getDropInterval` (it says level 10 is 196 ms; the code gives 231) | the table was generated from the CODE |

**Stripped**, on the 2048 power-tile and Snake multiplayer precedent: the
particle system and the neon canvas presentation. Both are rendering flourishes
with no glyph analogue, and neither participates in a rule.

### Seven pieces, five glyphs

The reference tells its seven tetrominoes apart **by colour alone**, and
`FallbackDriver` discards colour — the trap Snake recorded one epic earlier for
three cells. But the answer is not seven glyphs: once a piece is locked, *which*
piece left a block carries no information a player can act on. What must be
distinguishable is empty / stack / active / ghost / clearing, and that is what
the tier provides. Colour still varies per piece where it is free.

### One key or two

**Two**: `best_score_start{1,5,10}` and `best_lines_start{1,5,10}`. Snake refused
`best_length` because length and score were affine restatements of each other.
Here a tetris scores four times a single for the same four rows and both drops
pay points, so a patient endurance run and a short high-scoring one genuinely
disagree — 2048's `best_score` + `best_tile` argument. **Level is not
persisted**: it is `lines/10`, which *is* the restatement.

⚠ The key carries the **start level**, for Snake's wrap reason: beginning at 10
hands the player the multiplier immediately.

### Audio: three new ids, not the seven the issue lists

Move and rotate are `Click`, a hard drop is `Slide`, a one-to-three line clear is
`Merge`, a top-out is `Lose`. Only `Lock`, `Tetris` and `LevelUp` had nothing in
the bank that already meant them.

⚠ **Gravity and auto-shift are silent.** A piece falls several times a second
with no input, and DAS fires every 50 ms while a key is held — a sound on either
is a metronome. The argument that kept `Spawn` out of 2048 and `Step` out of
Snake, applying twice over in one game.

⚠ `Tetris` is a **separate spec**, not a pitched-up `Merge`: transposing by a
ratio needs `exp`, the portability trap the synth exists to avoid. All three
fingerprints were **generated by measurement**, and the same pass reproduced all
eleven committed rows byte for byte before these were read off it.

### Four of seventeen mutations went green

| Mutation | Finding |
|---|---|
| Kick **y-sign flipped** | ⚠ green. The case covering it asserted an *x* displacement, and every kick it could reach had `y == 0` — while its own comment claimed only a real kick could see the sign. There is now a case whose successful kick is I's `{1, 2}`, lifting the piece two rows out of a one-wide well |
| **Gravity clock not reset on spawn** | ⚠ green. The case banked 900 ms of a 1000 ms interval and ticked 50 — which does not step *either way*. It banks 990 now |
| **Lock clock credited the landing tick** | ⚠ green. Every lock case drove at 60 Hz, where the difference is one frame. The new case drives 1600 ms in one go |
| **`score_key()` returning `start1` for start level five** | ⚠ green. The keying case used 1 and 10 and never evaluated the middle branch — **exactly Snake's finding one epic ago**. All three keys now |

Two were compile errors by design: making the ghost glyph equal the active one
trips the distinctness `static_assert`, and deleting the status row's width
budget leaves it unused under `-Werror`.

⚠ **The status-row budget mutation was RED on the first try**, after going green
in two consecutive epics. The fix that memory carried forward — sweep widths
*narrower than the game's own minimum*, because the status row is drawn whether
or not the board fits — was applied from the first draft. 24, 28, 30 and 34 are
all below `kNeedCols`.

⚠ The harness itself gained a fix: it **rebuilds after restoring** the mutated
file. It had been leaving `build/` holding the mutated binary, so the next
ordinary `ctest` reported the mutation's failures as though they were the tree's.

### What a fourth game actually turned on

Two deferrals shared one written condition — "revisit at four roster entries" —
and **only one of them was really waiting on it**.

- **The wheel's positive half** and **the list's scrollbar glyphs** genuinely
  needed a roster longer than the pane. They also needed something the condition
  did not mention: interior list rows are `h - 5`, so four entries overflow only
  at **`rows == 8`**, and `test/11selector`'s probe hardcoded 60x20. The fourth
  game *and* a size parameter on that probe are what made them reachable.
- **The detail pane's scrollbar was never 7-bit at all**, and needed no fourth
  game — only a window short enough for a description to overflow. termforge
  v0.2.1 gave the shared scrollbar to `ListWidget`, `TableWidget` **and**
  `TextBox`; `draw_selector` set the style on the list, with a comment about the
  scrollbar, and never set it on the pane. It painted U+2502 and U+2588 onto
  terminals that had just reported no colour, for two releases.

A deferral's stated condition is a hypothesis about *why* something is
untestable. It can be right about the mechanism and wrong about the trigger.

---

## What the keyboard seam is (term-game#32)

Epic 6 wants `KeyboardMode::Enhanced`. Turning it on is not one line, and the
three reasons why are the whole change.

**It cannot be global.** `Enhanced` is not a superset of `Legacy`, it is a
different contract: every key arrives as CSI-u, so `Shift+a` becomes `ch=='A'`
**with `shift` set** where a plain byte carried no modifier, and every key gains
a `Release`. Set once at startup, Snake would turn twice per keypress and the
selector's bindings would double-fire. So `GameMeta` gains a `keyboard` field
defaulting to `Legacy`, and the Shell sets it in `enter_selected_game()` and
restores it in `apply_transitions()` — the single point every game exit already
funnels through, next to the score flush and for the same reason.

⚠ **`GameMeta`, not a `Game` method and not a fifth `GameContext` service.**
`set_keyboard_mode` is on `termforge::App` and the Shell is the only App, so a
game cannot ask directly; the Shell needs the answer *before* the game's first
frame and already holds the meta from the registry table. Keeping it there also
keeps it `constexpr`. `arcade/context.hpp` says a fifth service would be a new
design question — this is not one, because **a game never reads this field**.
What a game reads to learn which arm it got is `ctx.capabilities().kitty_keyboard`;
`App::keyboard_mode()` is a mirror of our own setter and is true everywhere.

**The Shell's own keys had no `KeyAction` check.** Ctrl+C, the selector's Escape,
and `handle_in_game_key`'s Escape and `p` all branched on `key`/`ch` alone. Under
`Legacy` no `Release` is ever delivered, so nothing could fire; the moment a game
asks for `Enhanced`, one press of Escape leaves the game on the way down and
quits the program on the way up.

⚠ **The gate is `!= Release`, not `== Press`, and it sits BELOW the game's
refusal.** A game that asked for `Enhanced` asked to see releases — that is the
only reason to ask — so the Shell must stop acting on them without stopping them
arriving.

**termforge's own degradation event cannot fire for us.** Upstream calls
`detail::keyboard_fallback_event` exactly once, from `App::setup()`
(`src/lib/core/app.cpp:51-54`), against whatever mode it holds *then*. We set the
mode at game entry, long after `setup()` returned and answered about `Legacy`. So
the Shell raises its own `ErrorEvent{Info, "keyboard"}`. Without it, a player on
a terminal with no kitty protocol gets a Tetris whose DAS silently is not DAS.

### Three of ten mutations went green, and two were claims in a comment

| Mutation | Result |
|---|---|
| Gate written `action == Press` | ⚠ **green.** The comment justified the wider predicate by claiming a Press-only gate kills hold-to-scroll. **Wrong** — arrows reach `ListWidget` through `m_ring.handle_key`, and upstream already drops releases there itself (`focus_ring.cpp:50`). For the only keys this gate owns, Press and Repeat are the same. What breaks the menu is `== Press` **together with** hoisting the call above the ring; neither half is visible alone, and the case now asserts the pair |
| Gate hoisted above `m_game->on_event` | ⚠ **green.** The placement is the entire point and nothing asserted it. Now pinned through Minesweeper's cursor, which moves on an arrow whatever the `KeyAction` |
| A *released* arrow moving the selection | ⚠ **unasserted.** Added beside its positive half. It pins **upstream's** contract, so a future pin bump that stopped dropping releases in the ring surfaces here |
| Either `set_keyboard_mode` call deleted; the notice keyed on `keyboard_mode()` | **green, and not a finding** — see below |

⚠ **The mode-switching branch has no consumer, and no test can give it one.**
Every roster entry declares `Legacy` and `all_games()` is a file-local
`constexpr` table with no injection seam, so deleting either `set_keyboard_mode`
call leaves the suite green. It was **red-verified in a pty** instead, by
flipping Snake to `Enhanced` in a scratch tree — with a **control** run proving
the evidence came from the flip:

| | flipped | control |
|---|---|---|
| `ESC[>27u` (push, on entry) | 1 | 0 |
| `ESC[=0;1u` (restore on exit) | 1 | 0 |
| `ESC[<u` (pop, on `leave_screen`) | 1 | 0 |
| fallback notice on the footer | present | absent |
| alternate screen in / out | 1 / 1 | 1 / 1 |

The restore is an **overwrite, not a second push**, exactly as termforge's
`terminal.hpp` claims — so the terminal's keyboard stack stays at most one deep
however many games are entered and left. The flip was not committed. **Epic 6 is
what makes this testable for real.**

⚠ **Nothing here shows that `Enhanced` works.** This container's terminal has no
kitty keyboard protocol and `test_run_frames` installs a `FallbackDriver` whose
capabilities are all false, so every arm exercised anywhere is the degraded one.

---

## What the v0.2.2 bump brought

`term-game#24`. Landed on its own,
before Epic 5, for the reason term-game#22 landed before Epic 4: a dependency bump
carrying a breaking change, bundled with a new game, makes a red CI run ambiguous
between the two.

The issue was written when upstream was at v0.2.0. It was at **v0.2.2** by the
time this was implemented, and the two extra tags were not filler — v0.2.2 is
termforge #60, which this file listed as Epic 6's blocker. That is the third time
this project has found the blocking picture staler than the issue describing it,
after Epic 1's already-shipped tick accumulator and term-game#16's "delete the file"
that turned out to rethrow. **Re-read upstream before implementing an issue about
upstream** is now a rule with three citations.

| tag | what it means here |
|---|---|
| v0.1.16 | `Cell::attrs` (#62). The payoff we have **not** spent — see the follow-up below |
| v0.1.17 | dropdown scroll. We use neither `Select` nor `MenuBar`. Inert |
| v0.1.18 | `Image` sub-rect blit, alpha, sprite slicing (#63). Unblocks Epic 8 |
| v0.1.19 | `MapWidget` v1 (#64). Unblocks Epic 7 |
| v0.2.0 | ⚠ wheel vs arrow-key semantics unified (#35). **The only one that changed our behaviour** |
| v0.2.1 | shared scrollbar for List/Table/TextBox (#21) |
| v0.2.2 | kitty keyboard protocol, `KeyAction::Repeat`/`Release` (#60). Unblocks Epic 6's feel |

### The wheel: a decision, not an inheritance

Before v0.2.0, `ListWidget::on_event` answered a wheel notch with
`set_selected(selected ± 3)`. The selector inherited that and never asked for it —
the comment in `Shell::on_event`'s mouse branch cited it as a reason the MenuMove
edge detection existed. #35 unified the wheel onto a view offset: **the wheel now
scrolls the view, the selection stays put and may scroll out of sight, and arrows
still move the selection.**

We **adopted upstream's convention** rather than rebuilding the old one. Doing
otherwise would have meant intercepting `MouseEvent` before `route_mouse` and
diverging from the framework deliberately — the exact workaround shape term-game#16
and term-game#17 spent two issues deleting, and "there are no workarounds left in this
repo" is a property worth more than a wheel gesture on a two-item menu. (Epic 7
has since added one back, deliberately and with a deletion condition — see the
top of this file. The bar is that each one is counted, not that there are never
any.)

The consequence is that **MenuMove no longer fires on the wheel**, which is
honest: nothing moved. `test/11selector` holds both halves — the selection does
not move, and no sound plays.

⚠ **This is the change that would have landed silently.** All 23 tests passed on
the new pin before either wheel case existed; nothing in the suite could see the
difference. The two cases were **red-verified** against the old pin, and the seam
for doing that is worth remembering:

```bash
cmake -B build-oldpin -DTERMFORGE_TAG=v0.1.15 -DCMAKE_CXX_FLAGS=-Werror
```

`find_package(termforge 0.2.2)` misses and FetchContent takes the override, so
the suite builds against the previous pin. On v0.1.15: 22 of 24 cases pass and
**exactly the two new ones fail** — `selector_index()` comes back 1 instead of 0,
and `MenuMove` fires twice. That isolation is the evidence, not the failure.

⚠ **Deferred, with a condition.** The wheel's positive half — that a notch moves
`ListWidget::scroll_offset()` — is *not* asserted, and cannot be faked. It needs a
roster longer than the list pane, and no legal size produces one: the Shell's
floor is 20x8, which leaves three interior rows for two entries. `all_games()` is
a file-local `constexpr` table with no injection seam. **Revisit at four roster
entries (Epic 6)**, or when a test-only substitute for the `glyphcade_roster`
target earns its keep — `src/lib/CMakeLists.txt` already makes the roster its own
archive, so the seam exists at the link level.

### The scrollbar, and a second reason `set_style` is load-bearing

v0.2.1 gives `ListWidget` a one-column scrollbar when its content overflows. The
selector will paint one the moment the roster outgrows its pane — not yet, at two
entries.

What matters now is the tier. The strip reads its track and thumb from
`scrollbar_glyphs(style)`, keyed off the **same** `BorderStyle` enum as the
selection marker: `|`/`#` under `Ascii`, `│`/`█` under every other family. So
`m_list.set_style(style)` in `draw_selector` now guards the 7-bit floor by two
independent routes, and the second one is **invisible to the test suite** — two
entries never overflow, so no scrollbar is drawn at any size the 7-bit case can
reach. Do not read "the 7-bit test passes without that line" as evidence. Whoever
registers the fourth game inherits the coverage.

Geometry did **not** move, which is the risk that turned out not to be one: the
right-hand column the scrollbar claims was already reserved at v0.1.15
(`max_w = r.w - gutter - 1`). Item text still starts at x=3 with 19 columns at
60x20.

### The payoff we did not spend

v0.1.16's `Cell::attrs` is the one tag with a concrete win: Minesweeper's cursor
is a **pair of brackets** costing a column per cell, so Hard needs 63 columns
where reverse video would need ~33. It is deliberately **not** in this bump — it
rewrites `layout.hpp`'s `kCellCols`, six geometry cases in `test/14minesweeper`
and two cursor cases in `test/15minesweeper-ui`, and it interacts with 
`term-game#15` (`GameMeta` minimum
size). Its own issue.

⚠ One correction for whoever picks it up, because #24 states the opposite:
attributes are **not** colour-tier-only. v0.2.2's `FallbackDriver` emits
`Reverse` and `Bold` and drops only the other four, surfacing that as an
`ErrorEvent{Info}`. So reverse video may be a legitimate *replacement* for the
brackets at both tiers rather than an addition at one — but that is a question to
answer with a pty, not to assume in either direction. The rule the brackets exist
for ("the cursor is a pair of characters, not a colour") was written against a
driver that dropped *colour*; an attribute that survives the floor is a different
argument.

---

## What Epic 0 built

- **CMake scaffold** from cpp-template, project name `glyphcade` (follows the
  directory name, as did the repo at the time).
- **termforge consumed** via [cmake/deps/termforge.cmake](cmake/deps/termforge.cmake)
  — `find_package(termforge ... CONFIG)` first, FetchContent as the fallback.
  Epic 0 pinned v0.1.7; the pin is now **v0.6.0** — see below.
- **`GLYPHCADE_WITH_AUDIO`** auto-detection in [cmake/audio.cmake](cmake/audio.cmake).
  Epic 2 answered the export question (term-game#13): rtaudio is linked by
  `src/audio_backend/` alone, which is never installed and never exported.
- **`run_or_report`** — the process's exception boundary. Until v0.1.10 it was
  also the terminal-restore workaround for termforge #71; that half is
  upstream's now, and what is left converts an escaping exception into a
  readable diagnostic and exit 1 instead of 134 via SIGABRT. Renamed from
  `guarded_run` by term-game#16. See below.
- **Tests:** `00bootstrap` (the audio option reached the compiler),
  `10render` (headless render, no tty), `21exception` (upstream tore the
  terminal down before the throw reached us, and our boundary turned it into
  exit 1), `pty-restore` (the same claim in a real pty, where the escape bytes
  are visible — added by term-game#16).
- **CI** (then Gitea Actions, since ported to GitHub) — **green since
  `term-game#10`**; see the CI
  section below for what it runs and why it took three epics to get there.

Epic 0's `BootApp` no longer exists — Epic 1 replaced it with the Shell.

---

## What Epic 1 built

- **`Game`** ([include/glyphcade/arcade/game.hpp](include/glyphcade/arcade/game.hpp))
  — `meta/start/tick/on_event/draw/done`, with the Shell's guarantees written
  into the docstrings because a game author reads that file and nothing else.
- **`GameMeta`** — `slug/title/description/tag/icon`, all `string_view` so the
  type is literal and the registry table can `static_assert` over it. Mirrors
  HTML-Games' `games.json` minus `href`/`theme`.
- **`GameContext`** — `capabilities()`, `border_style()`, `quit_to_menu()`.
  Minimal on purpose; see the deferrals below.
- **The registry** ([src/lib/arcade/all_games.cpp](src/lib/arcade/all_games.cpp))
  — a `constexpr` table, no mechanism, two `static_assert`s (unique slugs, safe
  icons). Adding a game is one line here.
- **The Shell** ([src/lib/arcade/shell.cpp](src/lib/arcade/shell.cpp)) — the one
  `termforge::App`. Selector ↔ InGame ↔ Paused, a two-pane selector over
  `ListWidget`+`Frame`+`TextBox`, pause as a `ConfirmDialog` overlay, and
  `set_tick_hz(60)` forwarding the framework's fixed timestep to the game.
- **`StubGame`** — one diagnostic game, with a deletion condition. **Deleted by Epic 3.**
- **Tests:** `10render` (repointed off `BootApp`; the selector paints at three
  sizes), `11selector` (the state machine, incl. the Escape regression and the
  use-after-free probe), `12registry` (incl. "the factory and the metadata
  agree", which no compiler can check), `13tick` (routing, the stall clamp, and
  pause stopping the simulation, over a fake clock).

Each of the three claims this epic actually rests on was **mutation-tested**:
restoring `App::on_event(ev)` in the in-game key path, deleting `set_tick_hz`,
and removing the pause gate in `on_tick` each turn a test red.

### What Epic 1 deliberately did not build

Every one of these is a decision with a condition attached, not an oversight.

| Deferred | Condition to revisit |
|---|---|
| Audio in `GameContext` | **shipped in Epic 2** as `audio() -> const audio::Player&`, additively, exactly as the seam promised |
| High-score persistence | **shipped after Epic 4** as `scores() -> const scores::Recorder&`, additively, exactly as the seam promised — and the "second scoring game, not the first" condition is what kept it from shipping as one integer. `term-game#14` |
| One static library target per game | **done** — landed ahead of Epic 4 as its own change, since a build restructure bundled with a new game makes a red CI run ambiguous. `src/lib` is now `glyphcade_lib` → `_roster` → `_game_<name>` → `_core`; see the section below |
| `StubGame` | **done** — deleted by Epic 3 |
| `Shell::quit_requested()` | **done** — retired by `term-game#17`; termforge [#73](https://github.com/gobha-me/termforge/issues/73) shipped `App::running()` in v0.1.14. ⚠ Not a drop-in: see the section below |
| The selector's gutter marker | **done** — retired by `term-game#17`; termforge [#72](https://github.com/gobha-me/termforge/issues/72) shipped in v0.1.11 and the two columns went back to the list |

---

## One static library per game

Landed ahead of Epic 4, deliberately on its own: it is a **pure refactor** — no
C++ moved, no symbol left the program, only which archive holds it — and a build
restructure bundled with a new game makes a red CI run ambiguous between the two.

```
glyphcade_lib      arcade/shell.cpp, arcade/exception_boundary.cpp
  ↓                the ALIAS, the export, the only spelling outside src/lib
glyphcade_roster   arcade/all_games.cpp
  ↓
glyphcade_game_minesweeper
  ↓
glyphcade_core     build_info.cpp, audio/*.cpp
```

`glyphcade_lib` stayed the umbrella, which is why **`src/bin` and all 14
auto-discovered test dirs needed no change at all** — they link
`${PROJECT_NAME}::lib`, still an ALIAS to a STATIC target, now carrying the whole
chain PUBLIC.

### ⚠ What would silently undo this

- **`arcade/shell.cpp` must stay in `glyphcade_lib`, above the roster.** It is the
  only TU that references `all_games()` (three sites), so its archive must be
  scanned first. Tidying it down into `core` closes a cycle
  `core → roster → game → core`. That fails *loudly* — `undefined reference to
  glyphcade::all_games()` — and, verified both ways, in **every** link declaration
  order: CMake emits the topological order, and `game → core` pins core last, so
  no `target_link_libraries` argument order can rescue it and `core` never gets
  duplicated. The fix is always to move the file.
- **`audio/*.cpp` must stay in `core`, below the games.** `minesweeper.cpp` calls
  `audio::Engine::play`. That one edge is the whole reason core reads
  "build_info + audio" rather than "everything that is not a game".
- **`target_link_libraries(_roster PUBLIC ${_game_targets})` is load-bearing
  twice.** Removing it fails at *compile* time, not link time —
  `all_games.cpp:20` cannot find `<glyphcade/arcade/registry.hpp>`, because that
  edge carries core's include directory as well as the game archives.
- **`GLYPHCADE_WITH_AUDIO` belongs on `core`, PRIVATE.** `build_info.cpp` is the
  one TU in the repo that reads it. Promoting it to `glyphcade_lib` to look
  tidier stops it reaching that TU, `build_has_audio()` answers false in an
  audio-ON build, and `test/00bootstrap` goes red — the tripwire it exists to arm.
- **Every target must be in the export set.** This one enforces itself:
  `install(EXPORT)` refuses to name a target it cannot resolve, so adding a game
  and forgetting `src/lib`'s game list stops *generation*. The list is handed to
  `cmake/install.cmake` via `PARENT_SCOPE`, and `EXPORT_NAME`s are derived by
  stripping the `glyphcade_` prefix, so a new game needs no line there.

**Packaging changed, intentionally**: the install prefix gains three `.a` files
and `glyphcadeTargets.cmake` now defines four imported targets —
`glyphcade::lib`, `::roster`, `::game_minesweeper`, `::core`.
`find_package(glyphcade)` + `target_link_libraries(app glyphcade::lib)` is
unchanged. `include/` and `bin/` are unchanged. A consumer linking
`glyphcade::core` alone would get an undefined `all_games()`, which is the
roster becoming a substitutable piece rather than a defect.

**A guarantee the layering now provides for free:** a game cannot reach the Shell,
because core sits *below* the games, so `glyphcade::Shell` is not on a game's link
line. AGENTS.md's "games never touch `App`, `Terminal`, or each other" is a link
error rather than a convention.

### How it was proven to be a pure refactor

Measured against a build of the parent commit on the same box, not asserted:

- **175/175 compile commands identical** modulo `-DGLYPHCADE_WITH_AUDIO`, which
  five TUs lost and **none of them reads** (only `build_info.cpp` does).
- **Archive symbols conserved exactly** — 1500 defined, 101 undefined, zero diff
  between the old single archive and the union of the four.
- **Same program**: identical 4258-symbol set, 600 functions, 130435 function
  bytes. `.text` is 16 bytes *smaller*, and that was chased down rather than
  waved at: total symbol bytes are identical, so it is inter-section padding from
  a different input-section order.
- **`ctest -N` byte-identical** — no test appeared or vanished.
- **Two-tier pty capture renders identically**, including 353 truecolor SGRs at
  the colour tier and zero at the bare one, one `?1049h`/`?1049l` pair each.
- Six arms green at 20/20: GCC, GCC `GLYPHCADE_WITH_AUDIO=OFF`, Clang, TSan
  (`RelWithDebInfo`), ASan, UBSan — all `-Werror`, no `ctest -E`.

Mutation-tested five ways, and **two of the five findings corrected a claim** in
the design that had been reasoned out carefully and was wrong — the word-order
story above, and the roster edge failing at compile rather than link time. See
the commit message for all five verbatim.

---

## What Epic 5 built (term-game#6)

Snake, in four pieces — minesweeper's shape exactly, and 2048's minus the tween:

- **[`board.hpp`](include/glyphcade/games/snake/board.hpp)** — the rules AND the
  step clock. Snake body as a deque with an incrementally-maintained occupancy
  grid, a two-deep turn queue, food, both wall modes, the speed curve. It
  includes **no termforge header**, which is what makes `test/25snake` *unable*
  to construct a `Screen`.
- **[`layout.hpp`](include/glyphcade/games/snake/layout.hpp)** — 28x16 cells at
  **two columns each**, **58x20 needed**. No `cell_at`: Snake takes no mouse
  input, so nothing hit-tests.
- **[`glyphs.hpp`](include/glyphcade/games/snake/glyphs.hpp)** — two tiers and
  three `static_assert`s (7-bit, exactly `kCellCols` wide, pairwise distinct).
- **[`snake.hpp`](include/glyphcade/games/snake/snake.hpp)** — the `Game`, and the
  only file that knows `Screen`, `Event` or `GameContext` exist.

**There is deliberately no `anim.hpp`.** 2048 needed one because sliding *is* the
mechanic; Snake occupies whole cells, and a sub-cell tween in a character grid is
a feel decision with no reference behind it to answer it.

### The step clock, and why it is not the accumulator AGENTS.md bans

AGENTS.md says "do not hand-roll an accumulator, in the Shell or in a game", and
`Board::tick()` contains one. They are different objects and the distinction is
worth keeping straight:

| | owned by | turns | into |
|---|---|---|---|
| the **banned** one | `termforge::App` (#59) | wall-clock frame deltas | fixed 1/60 s `dt` |
| the one **here** | `snake::Board` | fixed 1/60 s `dt` | game steps at 33-8 Hz |

The Shell still calls `set_tick_hz(60)` and nothing about that changed. What
Snake adds is that 60 Hz is not one of the speed curve's values — the curve runs
from 120 ms down to a 30 ms floor — so something has to turn ticks into steps,
and doing it from `dt` alone is what keeps the model drivable by N ticks with no
clock and no TTY.

### Three reference defects fixed rather than ported

Each is pinned by its own case in `test/25snake`, and each is the same *shape* as
something this repo has already been bitten by.

| Reference | Ours | Why |
|---|---|---|
| `game.js:78` **assigns** the frame timestamp after a step instead of subtracting the interval | accumulate and subtract | The reference's own speed table is intent, not behaviour: every step rounds up to the next ~16.7 ms rAF boundary, so its "100 ms" is 100-117 ms and its 30 ms floor is really ~33 ms |
| `snake.js:14` keeps **one** queued direction, judged against the last *applied* one | a two-deep queue, each turn judged against the **previous queued** direction | ⚠ This is the bug term-game#6 names. A fast double-tap loses its first turn. And Right→Up→Left judges Left against a stale Right — the reference survives that only because the other bug then discards the Up, so one bug masks the other |
| `food.js:13` is an **unbounded rejection sampler** with no board-full case | pick the k-th free cell in two passes; a full board is a **win** | Exactly the trap minesweeper's mine placement already replaced. ⚠ Restoring the reference's version there makes `test/14minesweeper` **hang, not fail** — and a hang is a much worse thing to diagnose than a red case |

**Stripped**, on the 2048 power-tile precedent: local 2-4 player multiplayer
(`multiplayer.js` + `controls.js`, about half the JS in the directory) and the
"ghost trail" toggle. Both are worth a sentence because neither is merely extra:
the multiplayer mode leaves eliminated snakes on the board as **invisible lethal
obstacles** (`checkCollisions` ignores `alive`, the renderer does not), and the
ghost trail is **dead code** — it paints translucent rectangles at the exact
coordinates `drawSnakeBody` then overpaints opaquely, so enabling it has no
visual effect whatsoever. Same category as 2048's slide animation that never
fires.

### Wrap is ours, and it keys the record

term-game#6 asks for "wrap-vs-wall as a mode" and the reference has no wrap at all.
It ships as a real player-facing toggle (`m`) rather than a compile-time option,
because an unexposed mode is dead code and this repo has just finished deleting
the last of that.

⚠ **The high-score key carries BOTH settings** — `best_score_<level>_<walls>`,
six records. Wrap removes four of the five ways to die, so a single
per-difficulty record would let a wrap run permanently outrank every solid one.
The key is switched on the **enums**, never derived by lowercasing the UI labels,
which is minesweeper's `time_key()` rule for the same reason: rename a label and
a derived key orphans every record a player has earned.

**One key, not two.** 2048 keeps `best_score` *and* `best_tile` because they are
genuinely independent. Here length is `kStartLen + eaten` and score is
`kFoodScore * eaten`, so a `best_length` record would be an affine restatement of
the same number — two values that can never disagree, which is a format inviting
a future reader to ask which one is authoritative.

### Audio: one new id, not the three the issue lists

`SfxId` gains **`Eat`** and nothing else. A turn is `Click` (a turn is a generic
acknowledged gesture, which is what Click is for) and dying is `Lose`, so eating
was the only one with nothing in the bank that already meant it.

- **No `Step` effect.** Snake advances several times a second with no input at
  all, so a per-step sound is not feedback, it is a metronome. Same argument that
  kept `Spawn` out for 2048, but stronger — a step does not even follow a
  keystroke.
- **A refused turn is silent**, matching both other games: there is no deny blip
  in the bank and inventing one is a feel decision nobody who cannot hear it
  should make.
- The `test/18audio-synth` fingerprint was **generated by measurement**. ⚠ Still
  unheard, like the rest of the bank.

### Mutation-tested, and four claims turned out to be decoration

Thirteen mutations. Nine went red immediately; one is a **compile error by
design** (making the ASCII body glyph equal the head glyph trips the distinctness
`static_assert` — the guard that matters most here, because the reference
separates head, body and food by **colour alone** and `FallbackDriver` discards
colour). Three went green, and all three were findings:

1. **Deleting the status row's width budget changed nothing observable.** Every
   width the case swept was wide enough that the priority loop stopped before the
   fields could reach the right-aligned word. ⚠ Epic 4's status-row mutation went
   green for the same reason, which makes this the second time — the fix is that
   the case now includes **40 and 50**, *narrower than the game's own 58x20
   minimum*, which is exactly where the status row is still drawn and an
   unbudgeted one truncates a field mid-number.
2. **Moving the step counter below the death returns left every case green**, so
   `board.cpp`'s comment claiming a fatal step still counts was decoration.
   Asserted now, because `steps` is what the frame-rate cases are written in
   terms of.
3. **Making `score_key()` ignore the wall mode left every case green** — the
   keying case only ever recorded in `Solid`, so the wrap branch was never
   evaluated. It now records in both, with a *higher* score in wrap, so a shared
   key would walk the solid record up rather than merely writing twice.

⚠ A fourth "green" was not a finding at all: the `sed` pattern spanned two lines,
which `sed` does not match, so the mutation had never been applied. Applied
properly it is red. **A mutation harness that cannot fail loudly reports every
no-op as a passing test** — check that the edit landed, not just that the build
did.

### What Epic 5 deliberately did not build

| Deferred | Condition to revisit |
|---|---|
| A **mouse** gesture | Snake's only verb is a direction. There is nothing a click could mean that a key does not already say, and `layout.hpp` therefore has no `cell_at` at all |
| A **tween** between cells | See above. Half-block sub-cell motion is possible at the colour tier and impossible at the 7-bit floor, so it would be a tier-only feel change — and feel is the one thing this container cannot judge |
| A minimum terminal size in `GameMeta` | `term-game#15`, the same answer Epics 3 and 4 gave: the game ships its own too-small screen (needs 58x20; the Shell's floor is 20x8) |
| A **tuned** speed curve | The reference's numbers, ported exactly, as named constants in `board.hpp`. Whoever can play it has one place to change them |
| `best_length` | Affine in `best_score` — see above. Revisit if a rule ever makes them independent (a bonus food worth more than one segment would) |

---

## What the score store is (term-game#14)

Landed straight after Epic 4, on the condition the issue itself set: **the second
scoring game, not the first**. That condition paid for itself. 2048 wants a best
*score*, Minesweeper wants a best *time per difficulty* — so a record is a keyed
value with a **direction** (`Better::Higher` / `Better::Lower`), and had this
shipped with Minesweeper alone it would have been one integer per game and wrong.
Wiring the *second* game is what proved the shape; wiring the first only closed
the issue.

**Where it lives.** `include/glyphcade/arcade/scores.hpp` +
`src/lib/arcade/scores.cpp`, in **`glyphcade_core`** — both games call it, which
is the rule at `src/lib/CMakeLists.txt`. The `arcade/` prefix does *not* imply
`_lib`; its three siblings sit in three different targets. No new dependency:
`<filesystem>` and `<fstream>` were already in core via `audio/sink.cpp`.

**The file**, at `$XDG_DATA_HOME/glyphcade/scores` (else `$HOME/.local/share/…`,
else memory-only):

```
# glyphcade scores v1
2048 best_score 20488
2048 best_tile 2048
minesweeper best_time_easy 42
```

Greppable and hand-editable beats compact at this scale. Versioned from the first
commit; an unknown version is **refused, not clobbered** — read-only, a notice,
and `flush()` reports rather than overwriting a newer format's file. A single
**malformed line** is skipped instead, so a truncated last line from a crash does
not cost a player every record above it.

**Two types, not one.** `Store` owns the file and has `flush()`. `Recorder` is
what a game gets and has no `flush()` — *when* to write is the Shell's I/O policy.
That is `audio::Player`'s argument transposed, null-object discipline included:
`ctx.scores().record(...)` needs no null check and no `has_scores()`.

**`record()` is monotone**, and that one property removes every hard case. 2048
records after *every* move with no end-of-run hook, and undo genuinely lowers
`Board::score()` — the record simply does not follow it down. A call in the undo
path would be decoration: removable with nothing going red.

### Five things here that a plausible change breaks

1. **`getenv` appears nowhere in core.** `resolve_path()` takes both directories
   as strings, so the **library cannot name a real file** and a
   default-constructed `Shell` is memory-only *by construction*. Only
   `src/bin/main.cpp` reads the environment. Move the resolution "down where it
   belongs" and the test suite starts writing into a developer's `$HOME`.
2. **Every diagnostic is a fixed 7-bit literal** — never a path, never
   `std::error_code::message()` (locale-translated). Errors reach `m_notice`,
   `m_notice` is painted on the selector footer, and `test/11selector` sweeps
   every cell for bytes `>= 0x80`. This found a **pre-existing** bug: the audio
   notice used an em dash, so a headless run that failed to open a device would
   have turned that case red. Fixed here.
3. **The direction is stored per entry, not just passed per call.** `flush()`
   re-reads and merges, so it must know the direction for a key it did not record
   this session. A merge that assumes `Higher` publishes a *slower* Minesweeper
   time as a record.
4. **The temp file name is unique per writer** (`scores.tmp.<hex>`). A fixed
   `scores.tmp` lets two arcades interleave into one file and rename the mixture
   — the only way this design can produce a file that was never any process's
   view of the records.
5. **`Board::elapsed()` is not a duplicate of `seconds()`.** `seconds()` clamps
   at 999 for a three-column HUD; storing the clamp writes a 1200-second win as
   999, which is wrong *and* unbeatable-by-tie. The store gets the unclamped
   value, the row shows a frozen `BEST 999`.

### What is deliberately not guaranteed

- **A SIGKILL loses the current run's records.** Flush happens once per game exit
  and once at teardown, never per improvement — a flush per record would be a
  write syscall on every 2048 move, which is what "no syscalls on the frame path"
  exists to prevent.
- **Concurrency is honest, not total.** Read-merge-`rename` means the file is
  never half-written and never clobbered key-wise by a process that read it, and
  a same-key race resolves to the *better* value — *provided the loser's value was
  on disk when the winner read*. A write landing inside another process's
  read→rename window is lost, not merged. There is no lock file.
- **On the ASCII tier the startup scores notice is outranked** by the colour
  notice, because `m_notice` keeps only the most recent message and the colour one
  describes the whole session. A bare-terminal player learns their file is
  unusable on the **first game exit** instead, which is why the Shell reports from
  two places rather than one.
- **The selector's detail pane does not show high scores**, and could not without
  a fix first: `refresh_detail()` early-returns on an unchanged index, so its call
  at the end of `apply_transitions()` is currently **decoration**. Filed rather
  than smuggled in here.

⚠ **Feel is unverified**, as ever. Whether `record` and `BEST` are in the right
place on the two status rows, and whether a persisted best is actually rewarding,
need a human. What is verified is the format, the merge, both directions, the
degraded modes, and the end-to-end path in a real pty.

---

## What Epic 4 built

2048, in five pieces — minesweeper's four-file split plus the one it never needed:

- **[`board.hpp`](include/glyphcade/games/twenty48/board.hpp)** — the rules.
  Slide/merge per direction, 90/10 spawning, win as a latch, loss via
  `can_move()`, one level of undo. It includes **no termforge header**, which is
  what makes `test/22twenty48` *unable* to construct a `Screen`.
- **[`anim.hpp`](include/glyphcade/games/twenty48/anim.hpp)** — the tween. Also no
  termforge, and it does not know `Board` either: it takes a span of cell values,
  so it is drivable by N fixed ticks with no terminal.
- **[`layout.hpp`](include/glyphcade/games/twenty48/layout.hpp)** — 6×3 tiles, gap
  1, **29×19 needed**. `tile_x`/`tile_y` have `double` overloads, which is the
  tween's only entry into geometry.
- **[`glyphs.hpp`](include/glyphcade/games/twenty48/glyphs.hpp)** — the colour ramp
  ported from the reference's CSS, the ASCII lattice, and four `static_assert`s.
- **[`twenty48.hpp`](include/glyphcade/games/twenty48/twenty48.hpp)** — the `Game`,
  and the only file that knows `Screen`, `Event` or `GameContext` exist.

### The tween was designed, not ported

**The reference has no working slide animation.** `2048/css/style.css:161`
declares `transition: transform 0.12s ease-out` and it never fires: `render()`
does `tileLayer.innerHTML = ''` and rebuilds every element with its transform
already set, and a freshly inserted element has no prior computed value to
interpolate from. Tiles teleport. Worse, the two `@keyframes` that *do* fire
animate `transform` — the same property carrying position, and a CSS animation
outranks an inline declaration — so every new or merged tile drops its
`translate()` and renders at the layer origin for 200 ms before snapping back.

What made a real tween possible: `Board::move()` reports the **motion facts** the
reference destroys inside `slideRow`. Its `filter(Boolean)` drops positions before
anything decides what merges, and although it does issue tile ids, **nothing ever
reads them** — identity dies at its view boundary. Ours is that discarded
information made explicit: `from`, `to`, the pre-move value, and whether this tile
is one of two merging into one cell.

Two properties are structural rather than maintained by care:

- **`draw()` renders from the `Anim`, always** — never from the `Board`, not even
  at rest. A finished `Anim` holds exactly the resting board at integer positions,
  so there is one path to the pixels and it cannot disagree with itself. Same
  argument as one-`Layout`-per-frame.
- **Input is never queued and never blocked.** A direction arriving mid-slide
  snaps the animation and resolves immediately, so ten moves produce the same
  board whether they arrive one per frame or all in one frame. That is AGENTS.md's
  "animation is never a participant in game logic" as a test.

### Divergences from the reference, each pinned by a case

| Reference | Ours | Why |
|---|---|---|
| ⚡ power tiles interleaved through the move loop | **stripped**, classic 2048 | decided before the epic; the mechanic is not decoration |
| the 2-vs-4 spawn odds live *inside* the power ternary (`game.js:57`) | 90/10 preserved | ⚠ **the biggest strip trap**: deleting the mechanic naively deletes every 4, producing a game that looks right and plays easier |
| `saveState()` runs unconditionally at `move()` entry; an illegal move then nulls it | snapshot only on a move that changed something | in the reference, a good move followed by a no-op direction silently destroys your undo — while leaving the button enabled |
| `undo()` force-sets `gameOver = false` | restores the recorded state | right for the same reason; the reference's version would be wrong after a win |
| win on `value === 2048` | `>= kWinTile` | values only double so they agree in practice; `>=` does not depend on that staying true |
| a win overlay that does not gate input | `Won` is a latch, play continues, `Lost` overrides it | a modal that fails to block input is a lie about state |
| `Math.random()` | `arcade/rng.hpp` | `std::uniform_int_distribution` is not specified bit-for-bit — the argument `board.hpp` already made |

`splitmix64` **moved** out of `minesweeper/board.hpp` into
[`arcade/rng.hpp`](include/glyphcade/arcade/rng.hpp) rather than being copied,
since being byte-identical across toolchains is the whole reason it is
hand-rolled. `minesweeper::Rng` still resolves via a using-declaration, so no call
site moved — and minesweeper's seed-pinned mine layouts are unchanged, which is
how we know the move was code-identical.

### Audio

`SfxId` gains **Slide** and **Merge**. A move that merges plays Merge *instead of*
Slide, so one gesture is still one sound.

- **No Spawn effect**, despite term-game#5 listing one: a spawn happens on every
  legal move, so a spawn sound is a second blip on every gesture.
- **One Merge, not one per tile value.** Pitching by the result needs
  `2^(cents/1200)`, i.e. `exp`, which is the portability trap the synth exists to
  avoid.
- Fingerprints in `test/18audio-synth` were **regenerated by measurement**, not
  guessed. ⚠ Still unheard, like the rest of the bank.

### Mutation-tested, and three findings changed the code

Six mutations; every one now turns something red, but two did not at first:

1. **Folding the merged value back** so a tile can merge twice → 2 tests red.
2. **`advance()` assuming a fixed 60 Hz dt** → `22twenty48` red. ⚠ It did **not**
   at first: the frame-rate case compared only the *final* state across
   chunkings, which `finish()` sets, so every chunking agreed no matter what the
   interpolation did — it never exercised a mid-slide frame. It now compares state
   at a **matched elapsed instant**, reached in 1 step and in 37.
3. **`phase()`'s clamp into [0,1] was unreachable** and is **gone**. Both call
   sites are already bounded — the slide branch runs only below `kSlide`, the pop
   branch only above it, and `advance()` finishes past `kSlide + kPop`. Removed on
   the precedent of `announce()`'s bool: a guard restating what the surrounding
   code guarantees reads as load-bearing to the next person simplifying around it.
4. **Dropping 2048 from `kGames`** → `12registry` red, via its new own
   `CMakeLists.txt` that passes CMake's game count. A by-name case cannot see a
   game that was compiled and linked and then left out of the table — which since
   the per-game split is a real shape of mistake, because adding a game touches a
   CMakeLists *and* `all_games.cpp`.
5. **Removing the status row's reserved gap** → `23twenty48-ui` red. ⚠ Also not at
   first, and the reason is worth keeping: there were **two** independent
   mechanisms for one property, and each alone protected the assertion being made.
   The budget prevents overlap; drawing the word last makes the word win any
   overlap. The case only checked the word and the *first* counter, so a run where
   `moves 0` was rendered as `moves` with its digits eaten passed. It now requires
   every field that appears to appear in full. **The budget is the fix; the draw
   order is a chosen failure mode, not a second guard.**
6. **Neutering `announce()`'s no-op guard** → `23twenty48-ui` red. A rejected key
   must be *silent*: there is no deny blip in the bank and inventing one is a feel
   decision nobody who cannot hear it should make.

### ⚠ A 7-bit violation the existing tests could not see

2048's `description` originally read `"Reach 2048 — then keep going"`. The em dash
reached a **bare pty**, because the selector prints the description on the
no-colour tier.

`glyphs.hpp` asserts 7-bit for tiles and `icon_is_safe()` covers the one field
that is deliberately not ASCII — but **nothing covered the prose**, which is the
field most likely to be written by someone reaching for a nice dash. And
`test/11selector`'s 7-bit sweep runs at 60×20, where the detail pane wraps the
description and the offending byte fell outside the visible rows.

That is the general hazard of asserting on rendered output: **the assertion covers
only what the viewport happened to include.** Hence the new check is at compile
time against the source string — `meta_text_is_ascii()` in `arcade/game_meta.hpp`,
asserted over the whole table in `all_games.cpp`. Verified by building against the
unfixed description first.

### What Epic 4 deliberately did not build

| Deferred | Condition to revisit |
|---|---|
| **High-score persistence** | **done**, immediately after Epic 4 — `term-game#14`. Wired into *both* games, and that is what proved the record is not one integer |
| A **mouse** gesture | 2048 is four directions and an undo. Nothing a click could mean that a key does not already say, and inventing one is a feel decision with no reference behind it |
| A minimum terminal size in `GameMeta` | `term-game#15`, same answer as Epic 3: the game ships its own too-small screen (needs 29×19; the Shell's floor is 20×8) |
| A **tuned** tween | 90 ms slide, 70 ms pop, linear. Named constants in `anim.hpp` rather than inline, precisely so whoever can play it has one place to change. An ease curve is a feel decision |
| **In-game pop at the ASCII tier** | A character cell cannot scale a glyph, and a merge can produce a six-digit label in a six-column tile, so there is no room for decoration. The pop is colour-tier emphasis; at the bottom tier the number changed, which is the information |

---

## What Epic 3 built

Minesweeper, in four pieces, three of which name no termforge type at all:

- **[`board.hpp`](include/glyphcade/games/minesweeper/board.hpp)** — the rules.
  Deferred mine placement, flood fill, marks, chording, win/loss, and a clock
  driven only by `dt`. It includes **no termforge header**, which is what makes
  `test/14minesweeper` unable to construct a `Screen` rather than merely not
  doing so.
- **[`layout.hpp`](include/glyphcade/games/minesweeper/layout.hpp)** — integer
  geometry. One `Layout` per frame feeds both `draw()` and `on_event()`, so
  drawing and hit-testing cannot drift apart; `cell_at()` is round-tripped over
  every cell at every size.
- **[`glyphs.hpp`](include/glyphcade/games/minesweeper/glyphs.hpp)** — the two
  tile tiers, with three `static_assert`s (7-bit ASCII, one column each,
  pairwise distinct).
- **[`minesweeper.hpp`](include/glyphcade/games/minesweeper/minesweeper.hpp)** —
  the `Game`. The only file that knows `Screen`, `Event` or `GameContext` exist.

**Three divergences from the HTML-Games reference**, each pinned by a test:

| Reference | Ours | Why |
|---|---|---|
| Rejection sampler for mine placement | partial Fisher–Yates over eligible cells | the reference's loop is unbounded once `mines > rows*cols - 9`. ⚠ **Restoring it makes `test/14minesweeper` HANG, not fail** — verified. If ctest ever times out there, that is what happened. |
| First click on a flagged cell places mines and starts the clock | the flag guard runs *before* placement | a move that did nothing must not arm the game |
| Chording bound to `auxclick` only | left-click on a revealed number also chords | most trackpads have no middle button, and chording is core to playing well |

Plus a question-mark mark state, which the reference does not have.

**Mutation-tested** — the five claims this epic rests on:

1. Shrinking the safe zone to the clicked cell → 3 logic cases red.
2. Deleting the flag check in `flood_reveal` → 1 red.
3. Deleting the `pressed == true` guard in `handle_mouse` → 1 red.
4. Hardcoding the Unicode tile table at the call site → 3 red, **including the
   7-bit sweep** — which is how we know the `static_assert` and the runtime
   sweep are not the same check.
5. Replacing the bracket cursor with a `kFocusBg` highlight → 3 red.

**Two traps that cost a debugging round**, both now in the test file headers:

- **Never hold a `Screen&` across a `step()`.** `App::test_run_frames`
  reassigns `m_screen` on every call, so a cached reference dangles — and it
  surfaces as a segfault mid-suite, not a wrong value.
- **Never read the `Game*` after dispatching a key that ends the game.**
  `Shell::handle_in_game_key` calls `apply_transitions()` as soon as the game
  consumes a key, so `done()` is polled and the game destroyed *before*
  `dispatch_event` returns. ASan found this one.

The hand-rolled splitmix64 in `board.hpp` exists because
`std::uniform_int_distribution` is not specified bit-for-bit. **Verified**, not
assumed: the same seeds produce byte-identical mine layouts from the GCC and
Clang builds.

### What Epic 3 deliberately did not build

| Deferred | Condition to revisit |
|---|---|
| **SFX** (reveal, flag, explode, win) | **shipped in Epic 2.** Bound in `minesweeper.cpp` via `announce()`, which compares board state across the verb — `Board` learned nothing about audio. |
| **High-score persistence** | **done** — `term-game#14`, after Epic 4 met its "second scoring game" condition. The diagnosis here was right: a fresh `Game` per entry is exactly why the store had to live on the Shell. Minesweeper now shows `BEST nnn` beside the timer. |
| A minimum terminal size in `GameMeta` | Hard needs 63x20 and the Shell's floor is 20x8, so the selector will launch a board the terminal cannot show. Epic 3 ships the in-game too-small screen instead — `term-game#15`. |

---

## What Epic 2 built

A complete audio path, offline-testable end to end, with the device backend
quarantined outside the exported package.

- **`audio/ring.hpp`** — a lock-free SPSC command ring. Monotonic 64-bit indices
  masked at use, so it holds exactly N with no sacrificial slot; acquire/release
  pairing between the slot write and the index publish; the two indices padded
  onto separate cache lines.
- **`audio/synth.hpp`** — `Wave`, a linear `Adsr`, `SfxSpec`, and a **ten**-entry
  bank: Click, Reveal, Flag, Explode, Win, Lose, MenuMove, MenuSelect, and — added
  by Epic 4 — Slide and Merge. ⚠ Appended at the END of the enum on purpose:
  `kBank` is indexed by the enum's numeric value, so inserting rather than
  appending renumbers existing effects.
- **`audio/sink.hpp`** — `AudioSink` plus `NullSink` and `WavFileSink`. No
  rtaudio anywhere near it.
- **`audio/engine.hpp`** — the mixer, the `Engine`, and the `Player` handle a
  game is given.
- **`src/audio_backend/`** — the RtAudio backend, in a target that is never
  installed and never exported.

### Decisions worth not re-litigating

| Decision | Why |
|---|---|
| The ring drops the **newest** command, not the oldest | term-game#3 said oldest; that cannot be done inside the SPSC contract without the producer writing the consumer's index. The issue was amended. |
| No `sin`/`exp` anywhere — integer phase, linear envelopes | glibc's transcendentals are not correctly-rounded and move between versions; `-ffp-contract` defaults differ. Same argument `board.hpp` makes for splitmix64. Result: byte-identical output across GCC -O0/-O2/-O3 and Clang -O2. |
| Numeric fingerprints, **not** golden files | AGENTS.md asked for golden files and forbade binary blobs two sections earlier. A cross-toolchain byte digest is also a portability trap; "we measured it identical" is not "it is specified". |
| Headroom by `static_assert`, not a limiter | 8 voices × 1/8 FS cannot clip, provably. A limiter is something you must HEAR to trust. Cost: one sound peaks at −18 dBFS. |
| `play()` short-circuits on a Discard sink | Otherwise the ring fills once and `dropped()` climbs forever on the `GLYPHCADE_WITH_AUDIO=OFF` arm CI runs — destroying the one counter that means "the audio thread is in trouble". |
| rtaudio in a never-exported target (term-game#13) | A PRIVATE link still reaches the exported Targets file as `$<LINK_ONLY:...>`. Guarded by the `audio-export-clean` ctest. |

### Mutation-tested, and three of them changed the code

Not a formality — these all looked correct and were not:

1. `announce()` took the verb's `bool` and returned early on false. Deleting
   that guard left the suite green, because the state and revealed-count
   comparisons already subsume it. **The parameter was removed.**
2. `announce_mark()`'s `bool` guard could *also* be deleted with the suite
   green — but for the opposite reason: nothing covered it. **A test was added**
   ("marking a revealed cell is silent"), and the guard is load-bearing.
3. The zero-crossing tolerance was `max(2, 1%)`, and a semitone of pitch error
   on Click (660→623 Hz) passed straight through — ±2 crossings is ±6.5% on a
   24 ms effect, and a square wave's peak and RMS do not move with frequency.
   **Tightened to `max(1, 0.2%)`.**
4. `audio-export-clean` was mutation-tested with the exact violation it exists
   for — a PRIVATE `PkgConfig::RTAUDIO` link into `glyphcade_lib` — and catches
   it, confirming PRIVATE is no defence.

### ⚠ Traps in this area

- **`Engine::play()` must keep short-circuiting on a Discard sink.** It reads as
  a redundant branch. It is what keeps `dropped()` meaningful on the arm CI runs.
- **`Engine::render` OVERWRITES; `Voice`/`Mixer` ADD.** It is the top of the
  render stack. Making it add too gives a device the previous block again, as an
  echo that grows.
- **`Shell::on_tick`'s `m_audio.pump(dt)` sits ABOVE the pause gate**, and is not
  test scaffolding — without it an offline sink writes an empty file.
- **The audio notice is emitted BEFORE the ASCII-tier one** in
  `sync_capabilities()`, because `m_notice` keeps only the most recent message
  and the colour notice is the one `test/11selector` and the pty recipe assert on.
- **`GLYPHCADE_WITH_AUDIO` now looks unused inside `src/lib`** — no audio source
  is `#ifdef`'d on it. It is not unused: it is what `build_has_audio()` reports
  and what `test/00bootstrap` asserts against CMake's own belief.

### What Epic 2 deliberately did not build

| Deferred | Condition to revisit |
|---|---|
| **A tuned bank** | Nothing here has been heard. Render a session with `GLYPHCADE_AUDIO_WAV=/tmp/session.wav ./build/src/bin/glyphcade` and listen; the fingerprints in `test/18audio-synth` record what the bank IS, not what it should sound like, and are expected to move when it is retuned. |
| **In-game cursor-move SFX** | A blip per keystroke under a held arrow key needs a rate limit; a rate limit needs a clock; `dt` inside `Game::tick` is the only clock a game may read. A feel decision requiring an ear. |
| **A positive `MenuMove` assertion** | **done** — Epic 4 registered a second game, so the `size() > 1` guard now passes and the assertion runs. |
| **Coverage of the `State::Selector` guard** in both selector handlers | Established by mutation: deleting it from the mouse path leaves the suite green, because with one game a click does not move the selection. Kept because the argument is sound; untested until a second game exists. |
| **Detune / per-play gain** | Detune needs `2^(cents/1200)`, i.e. `exp`, i.e. the portability trap the synth was written to avoid. Nothing consumes either yet. |
| **A limiter** | Would let the bank be louder than −18 dBFS. Must be heard to be trusted. |

### An upstream gap this surfaced

`termforge::App::setup` and `::teardown` are **private and non-virtual**, so a
subclass has no hook to bring up and tear down its own resources inside the
loop's lifetime. The audio device therefore opens in the `Shell` constructor —
before any terminal exists to report a failure on — and the failure message is
stashed and drained on the first frame through `sync_capabilities()`. It works,
but it is upstream's shape forcing ours. Filed as termforge
[#97](https://github.com/gobha-me/termforge/issues/97), asking for protected
`on_start()`/`on_stop()` hooks called inside the terminal's lifetime. The
workaround is commented at both sites; its deletion condition is that issue
shipping.

---
