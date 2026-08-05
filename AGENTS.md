# AGENTS.md — conventions for AI agents working in this repo

If you're an LLM (or an LLM-driven editor) about to make changes here, read this
first, then [DESIGN.md](DESIGN.md) for the architecture and
[STATUS.md](STATUS.md) for what is actually true right now.

This is **glyphcade**: a TUI arcade suite in C++23, built on
[TermForge](https://github.com/gobha-me/termforge) for UI and
[RtAudio](https://github.com/thestk/rtaudio) for sound.

## ⚠ Two repos — check which one before filing anything

This is the single easiest mistake to make here. Both are on GitHub, both use
`gh`, and the split is by *subject*, not by tool.

| Repo | What goes there |
|---|---|
| **gobha-me/glyphcade** (this) | project work, game epics, anything about the arcade |
| **gobha-me/termforge** | framework gaps and bugs in the rendering layer |

If TermForge is missing something this project needs, that is an issue against
**termforge** — not a workaround buried in game code. That feedback loop is half
the point of this repo.

⚠ **Issue numbers below 15 or so may predate the move.** This project was
migrated from a self-hosted tracker before it went open source; references to
issues that did not come across are written as plain text like
`` `term-game#42` `` rather than as links, precisely so GitHub does not
auto-link them to an unrelated issue of the same number. If you see that form,
it points at history that is not public — treat it as a note, not a lead.

## Baseline

- **CMake ≥ 3.28**, **C++23** (GCC 13+ / Clang 19+), Catch2 v3 via FetchContent.
- Scaffolded from [cpp-template](https://github.com/gobha-me/cpp-template);
  compiler respects the environment, clang and the sanitizers are opt-in
  toolchains.
- Project name is **`glyphcade`**, written literally in `project()` and *not*
  derived from the directory name — it becomes the target, `glyphcade_lib`, and
  every `-Dglyphcade_*` option. The checkout directory can be called anything.
- **rtaudio is optional.** `GLYPHCADE_WITH_AUDIO` auto-detects and defaults OFF
  when absent. The repo must always build and test without it.

## Hard rules (project-specific)

- **One `App`, many `Game`s.** The Shell is the only `termforge::App` — it owns
  the terminal, the loop, and the audio engine. Games are **not** `App`
  subclasses and never touch `App`, `Terminal`, or each other. Shared services
  reach them only through `GameContext`.
- **Register games explicitly** in `src/lib/arcade/all_games.cpp`. **Never use
  self-registering statics.** The linker drops unreferenced objects out of static
  libraries, so the registrar vanishes and the game silently disappears from the
  menu with no error at any stage. Explicit registration fails at compile time
  instead.
- **One static library per game.** A game is
  `src/lib/games/<name>/` with its own `CMakeLists.txt`, listed by
  `add_subdirectory` **and** in `src/lib`'s `_game_targets` list; its public
  headers stay under `include/glyphcade/games/<name>/`, because
  `cmake/install.cmake` ships them with one `install(DIRECTORY include/ …)`.
  `<name>` is the game's **namespace**, which is not always its slug — a slug may
  begin with a digit and a namespace may not.
- **A game links `glyphcade_core` and nothing else in this repo.** Not
  `glyphcade_lib`, not another game. Because core sits *below* the games in the
  link chain, a game physically cannot reference the Shell — that is the previous
  rule enforced by the linker instead of by review. If a game needs something the
  Shell has, it belongs in `GameContext`, or in core.
- **The audio callback is a realtime thread.** No locks, no allocation, no
  syscalls, no I/O inside it — ever. Commands cross a lock-free SPSC ring.
  Overflow drops and counts; it never blocks the UI thread and never grows.
- **Every game is playable at the bottom tier.** Pixel sprites are an
  enhancement over a glyph fallback that always exists — the same relationship
  `Widget::draw_pixels` has to `Widget::draw`. A game that needs Kitty is a bug.
- **Rules extent never scales; presentation may, and has a ceiling as well as a
  floor.** Board size **is** the game — Minesweeper's board size is its
  difficulty, 2048 is 4x4, Tetris is 10x20 — so a bigger terminal must never buy
  a bigger board. ⚠ For Snake that is **correctness, not taste**:
  `score_key(Level, Walls)` does not include the field size, so growing the
  field silently makes every stored record incomparable with every new one. What
  may scale is the *cell*; `kCellCols = 2` is already an aspect-ratio correction
  rather than a unit. Every game declares its floor in `GameMeta::geometry`
  **with its kind** — `SizeFloor::Drawable` is arithmetic (the board has one
  size and the terminal has room or does not), `Playable` is a judgement (only a
  game that can always draw itself, i.e. Sokoban's camera, is entitled to one) —
  and the selector states them with the same **force** — both say "needed",
  because both kinds refuse below their floor — differing only in the **reason**
  a `Playable` one adds ("to play well"). ⚠ A draft that printed "recommended"
  for the `Playable` game read as "you may go below this one", and the game then
  refused anyway: the menu promising what the game does not keep, one keystroke
  apart, which is the very defect this rule exists to prevent. ⚠ The ceiling is
  on **columns only**: rows past a floor are
  *capacity* (more roster visible in a scrolling list, more of a pile in a card
  game), columns past a prose measure are not. There is no `kSelectorMaxRows`
  and there should not be one.
- **Degradation is an event** (inherited from TermForge). Any fallback raises an
  `ErrorEvent`, never a silent downgrade. ⚠ But `Shell::m_notice` is **sticky** —
  it holds the most recent event until the next game entry clears it, so on a
  no-colour terminal the capability report sits in the footer for the whole
  session. That is why the selector's size warning takes that row while it
  applies rather than deferring to the notice; the other order made the warning
  unreachable at the bottom tier entirely.
- **Fixed timestep, clamped delta.** Game logic advances in constant-`dt` ticks,
  and the real frame delta is clamped so a breakpoint or a laptop suspend cannot
  deliver one enormous `dt`. Determinism here is what makes logic testable.
  **This is `termforge::App::set_tick_hz` / `set_max_tick_dt` (#59, v0.1.8) —
  do not hand-roll an accumulator, in the Shell or in a game.** And do not read
  a clock inside `Game::tick`: `dt` is the only time that exists there, which is
  exactly what makes a game drivable by N ticks with no `Screen` and no TTY.
- **The Shell never chains to `termforge::App::on_event`.** The base default
  quits on Escape; inside a game Escape means "back to the menu", and the two
  cannot both be true if the base runs. So `Shell::on_event` is total and
  handles Ctrl+C itself, because it is no longer inheriting it. ⚠ Every
  termforge example ends its `on_event` with `App::on_event(ev);` — **do not
  "restore" that line.** `test/11selector` fails if you do.
- **A running game owns the whole `Screen`.** The Shell draws no chrome over it,
  so `draw()` coordinates and `MouseEvent` coordinates are the same coordinates.
  A game that assumes an offset is a game that is wrong somewhere else too.
- **A game's slug and its namespace need not match, and for 2048 they do not.**
  The slug is `"2048"` — a user-visible stable id that keys the menu and the score
  file — while the directory, namespace and class are `twenty48`/`Twenty48`,
  because a C++ identifier may not begin with a digit. The directory follows the
  **namespace**, so
  `#include <glyphcade/games/twenty48/board.hpp>` matches
  `glyphcade::twenty48::Board`.
- **Every `GameMeta` text field except the icon must be 7-bit ASCII**, and that is
  a `static_assert` in `src/lib/arcade/all_games.cpp`, not a convention. The
  selector prints slug, title, tag and description on the no-colour tier, which by
  definition cannot render anything else. ⚠ An em dash in a description is the
  usual culprit and it has happened — and note that a rendering test cannot be
  relied on to catch it, because such a test only covers what its viewport
  happened to include.
- **A `GameMeta` icon must pass `icon_is_safe()`.** It is a `static_assert` in
  `src/lib/arcade/all_games.cpp`, not a convention. Variation-selector emoji
  (⚒️ ⚔️ ⚙️) measure one column and render two, which shifts every cell to their
  right for the rest of the run — and looks identical to a safe icon in an
  editor. Pick from U+1F300+ or leave it empty.
- **Game logic must not know about rendering.** Animation is a presentation layer
  over already-resolved state, never a participant in it.

## Testing philosophy

Inherited from TermForge: **the interesting things are testable without a
terminal**, and **failures are the feature**.

- **Game logic** — drive N fixed ticks, assert state. No `Screen`, no TTY. If a
  rule can only be tested by playing, the model and the view are tangled.
- **Rendering** — draw into an offscreen `Screen`, assert cells.
- **Audio** — render through `WavFileSink` and assert samples. A mixer test that
  N simultaneous voices neither clip nor drift.

  ⚠ This used to say "golden files per SFX", which contradicted the no-binary-
  blobs rule two sections up and was a portability trap besides. What ships
  instead is a **compact numeric fingerprint** per effect — frames exact; peak,
  RMS and zero-crossing count within tolerances — committed as source text in
  `test/18audio-synth`, plus a **bit-exact self-determinism** check that two
  renders inside one build match. A committed cross-toolchain byte digest is
  deliberately NOT asserted: the synth is measurably byte-identical across GCC
  -O0/-O2/-O3 and Clang, but "we measured it" is not "it is specified", and a
  digest would turn a future glibc bump into a mystery failure that looks like a
  synth bug. Same distinction `board.hpp` makes about `<random>`.
- **The SPSC ring** — TSan-clean producer/consumer test. This is the one place a
  bug is a heisenbug, so it gets the most adversarial treatment.

## ⚠ Audio cannot be verified in the dev container

There is **no `/dev/snd`** and no PulseAudio here. Audio is developed against
`NullSink`/`WavFileSink` and verified on the maintainer's own machine, which has
`librtaudio-dev` installed.

Report audio changes as *"builds clean, offline sink tests pass, needs device
verification"* — **never as verified**. Claiming otherwise is unfounded.

## How to verify before a PR

Four configurations minimum. The second is not redundant with the first: this
container has `librtaudio-dev`, so detection defaults **ON** here and the OFF arm
— the one CI runs and the one this repo promises always works — is only exercised
if you ask for it.

```bash
# 1. GCC, audio auto-detected
cmake -B build -DCMAKE_CXX_FLAGS=-Werror && cmake --build build \
  && ctest --test-dir build --output-on-failure

# 2. GCC, audio explicitly OFF
cmake -B build-noaudio -DGLYPHCADE_WITH_AUDIO=OFF -DCMAKE_CXX_FLAGS=-Werror \
  && cmake --build build-noaudio \
  && ctest --test-dir build-noaudio --output-on-failure

# 3. Clang
cmake -B build-clang -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/clang.cmake \
  -DCMAKE_CXX_FLAGS=-Werror && cmake --build build-clang \
  && ctest --test-dir build-clang --output-on-failure

# 4. Thread sanitizer — the arm that judges the audio command ring
cmake -B build-tsan -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/thread.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-Werror \
  && CMAKE_BUILD_PARALLEL_LEVEL=2 cmake --build build-tsan \
  && ctest --test-dir build-tsan --output-on-failure
```

### ⚠ Do not add `--parallel` back to those commands

They used to read `cmake --build <dir> --parallel`, with **no value**, and that is
worse than redundant. `cmake --build` consults `CMAKE_BUILD_PARALLEL_LEVEL` *only
when `--parallel` is absent*; this project uses the Unix Makefiles generator, so a
bare `--parallel` becomes `make -j` with **no limit at all**, and whatever cap the
environment set is silently discarded.

That matters because the machine lies about its size. Check the container, not the
kernel: `/sys/fs/cgroup/cpu.max` and `/sys/fs/cgroup/memory.max`, never `nproc` or
`free`, which report the host. On the development box `nproc` says 20 while the
cgroup allows 8 CPUs and 16 GiB — shared with other C++ projects, so the real
budget at any moment is well under that. **Exit code 137 is the cgroup OOM killer,
not a compiler error**, and it takes the whole session down with it.

Omit the flag and the environment's cap applies. **The TSan arm needs a lower cap
than the rest** — it OOM-killed `cc1plus` at 4 parallel jobs and needs 2, because
`RelWithDebInfo` plus TSan instrumentation plus Catch2 is the heaviest translation
unit combination in the repo. Hence the explicit `CMAKE_BUILD_PARALLEL_LEVEL=2` on
command 4 and nowhere else.

CI carries the same rule: `.github/workflows/ci.yml` passes `--parallel 4`, and
`2` on the TSan arm. It used to pass a bare `--parallel` against a
container-limited runner, which is the failure described above; that was fixed
as part of the move to GitHub Actions.

⚠ **TSan is its own build, never a flag added to another one** — it does not
compose with ASan or UBSan. It needs an explicit `-DCMAKE_BUILD_TYPE`, because
`cmake/toolchain/thread.cmake` deliberately does not force one (forcing `-O2`
there would clobber the Debug default's `-O0 -g` for everyone).

The audio engine's SPSC command ring is the reason this arm exists: it is the one
place in the repo where a bug is a heisenbug, so "green without TSan" says
nothing about it. `30sanitizer-smoke` proves the sanitizer is actually engaged
rather than silently absent — under TSan it must **print a `ThreadSanitizer`
report**, which is what `test/CMakeLists.txt` turns into the pass condition. A
green run with no report is a no-op toolchain, not a clean tree.

Pass `-DCMAKE_CXX_FLAGS=-Werror` — and note that it only works because our
`cmake/toolchain/default.cmake` appends rather than replaces. If you ever re-sync
that file from cpp-template you will silently lose it; check with
`grep -- -Werror build/compile_commands.json`. Sanitizers via
`cmake/toolchain/{address,undefined}.cmake`.

`ctest` must be green with **no `-E` exclusion** — in particular `artifact-check`
runs in enforce mode and must print `CLEAN`. If it reports Class-B failures right
after you add files, `git add` them first: it reads `git ls-files`, so untracked
files look like missing ones.

Check `ctest -N` lists **`pty-restore`**, **`audio-export-clean`** and
**`consumer-resolves`**. All three are conditionally registered — `pty-restore`
on `find_program(script)`, the other two on `${PROJECT_NAME}_INSTALL` — so on a
box or in a configuration that lacks the gate they silently are not there, and a
green run without them has not checked what only they can show: the one thing a
pty reveals, and whether the installed package is resolvable by anybody at all.
**Absent means skipped, not passed**; the configure log says which.

### Checking the terminal itself, without a human

An agent has no terminal, but it can allocate a pty with `script(1)` and read
back exactly what the app wrote. Use this instead of claiming a rendering or
terminal-restore change is unverifiable:

```bash
{ sleep 3; printf '\033'; sleep 2; } | timeout 20 \
  script -q -c "$PWD/build/src/bin/glyphcade" /tmp/pty.raw
# entered and left the alternate screen?
grep -c $'\033\[?1049h' /tmp/pty.raw   # 1
grep -c $'\033\[?1049l' /tmp/pty.raw   # 1  <- terminal was restored
# what was actually rendered (the renderer positions every cell, so text is
# never contiguous — strip the escapes before grepping for it):
perl -pe 's/\e\][^\a\e]*(\a|\e\\)//g; s/\e_[^\e]*\e\\//g; s/\e\[[0-9;?]*[a-zA-Z]//g' \
  /tmp/pty.raw | tr -s ' '
```

Send input *after* setup finishes — a keystroke delivered during the capability
probe is eaten by the DA1 response parser, and the app never sees it.

**The Epic 1 acceptance flow, end to end, in the same harness** — selector,
into a game, back out, quit:

```bash
{ sleep 3; printf '\r'; sleep 3; printf '\033'; sleep 2; printf '\033'; sleep 2; } | \
  timeout 30 script -q -c "$PWD/build/src/bin/glyphcade" /tmp/pty.raw
```

Then check both tiers, because they render differently and CI only ever sees
one. Bare (the fallback driver reports no colour, so the Shell picks
`BorderStyle::Ascii`) should show `Up/Down select`, no icon, and the
`no colour capability` notice. Re-run with `TERM=xterm-256color
COLORTERM=truecolor` and it should show `↑↓ select`, the 🧪 icon, `38;2;`
truecolor SGR in the raw capture, and **no** notice.

⚠ The renderer diffs, so a string is written in full only the first time. Do not
expect to find `ticks: 42` in the capture — later frames only repaint the digits
that changed. Look for a *sequence* of distinct `elapsed: N.NNs` values instead;
that is the evidence the simulation is advancing.

⚠ **Grep the STRIPPED text for words and the RAW capture for escapes**, and get
this backwards and a check goes vacuous rather than red. `grep 'Resume'` on the
raw file returns 0 at every pin — the renderer positions every cell, so screen
adjacency is not byte adjacency. A colour check that also proves the widget
never rendered proves nothing (term-game#36, and the same family as the `MINES`
capture in term-game#38).

⚠ **A "this colour never appears" check needs a control that CAN appear.** Four
were used for #36's press flash, and the fourth is the one that matters: the
same button's *focused* background, `48;2;64;128;255`, does appear — which is
what shows the grep can match a Button's own colour at all. Without it, "the
pressed colour is absent" is indistinguishable from "no Button was ever drawn".
Better still where a second binary exists: run the SAME capture against the old
pin's binary and diff the counts. For #36 that was 1 against 0, which is
evidence a single arm cannot produce.

⚠ **The same rule holds for glyphs, and there the control is easy to get
wrong.** term-game#44 checked that the pause dialog stopped painting box-drawing
characters. Drive it — note the *two* `\r`, because Minesweeper is roster entry
0 and has a pre-start options screen:

```bash
{ sleep 3; printf '\r'; sleep 1; printf '\r'; sleep 1; printf 'p'; sleep 3;
  printf '\033'; sleep 1; printf '\033'; sleep 1; printf '\033'; sleep 2; } | \
  timeout 40 script -q -c "$PWD/build/src/bin/glyphcade" /tmp/pause.raw
```

Then, on the **stripped** text: `grep -c 'Paused'` and `grep -c 'Resume'` must
both be ≥ 1 *before any glyph count is believed* — they are the Frame's own
title and a Button label, so they prove the widget under test drew at all.
`grep -c '┌'` is a good discriminator precisely because **`BorderStyle::Single`
is a family this application never chooses** (the tier resolves only to `Ascii`
or `Rounded`), so its only possible source is an unstyled sub-widget. At the
bare tier finish with `LC_ALL=C grep -cP '[\x80-\xff]'` — the whole-session
analogue of `all_seven_bit()`. At the colour tier the control is `grep -c '╭'`,
which must be ≥ 1 *and go up by the fix*: 7 → 8 is the positive half, showing
the dialog joined the rounded family rather than merely stopping. ⚠ Do not
expect `┤` or `│` to move at the colour tier — `Rounded` shares both with
`Single`, and only the corners differ. A check written against those would read
as a failed fix.

⚠ **A glyph that IMPROVES at one tier must be shown not to change at the
other**, and one `\r` is enough to reach it. term-game#45 swapped the options
cycler's `<`/`>` for `‹`/`›` above the ASCII tier, where the old code was
correct and had to stay so:

```bash
{ sleep 3; printf '\r'; sleep 3; printf '\033'; sleep 1; printf '\033';
  sleep 2; } | \
  TERM=xterm-256color COLORTERM=truecolor timeout 30 \
  script -q -c "$PWD/build/src/bin/glyphcade" /tmp/cycler.raw
```

On the stripped text, `grep -ac 'Level:'` and `grep -ac '╭'` must both be ≥ 1
first — Minesweeper's own option label and the rounded family — or a glyph count
of zero just means nothing drew. Then `grep -ac '›'` goes **0 → 1** against the
old binary and `grep -ao 'Easy >' | wc -l` goes **1 → 0**. Re-run with
`env -u TERM -u COLORTERM` and **both** numbers must be unchanged (`Easy >`
still 1, `LC_ALL=C grep -acP '[\x80-\xff]'` still 0).

⚠ **Use `grep -a`.** A pty capture contains NUL bytes — 22 lines' worth in the
run above — and grep classifies such a file as binary, printing `Binary file
matches` *instead of a count*. It does not do this reliably: GNU grep decides
from the first buffer, so whether a bare `grep -c` returns the right number or
nothing at all depends on where the NULs happen to land. It returned the right
number on the capture above and nothing on a three-line probe with an early NUL.
An intermittently-silent count is worse than a wrong one, so pass `-a` always.

**The throw path is automated** — `ctest -R pty-restore -V`, or by hand
`cmake/pty_restore.sh build/test/pty-restore-probe`. It drives a
deliberately-throwing binary through the same harness and asserts one `?1049h`,
one `?1049l`, and that the leave sequence appears *before* `glyphcade: fatal:` in
the byte stream. The headless suite cannot reach any of that: nothing there
enters the alternate screen, so there are no escape bytes to look at.

⚠ Its probe constructs its App **outside** the exception boundary, unlike
`src/bin/main.cpp`. That is deliberate — shaped like `main.cpp`, `~App()` also
restores the terminal, and the test passes even with termforge's guard removed.
Read `test/pty_restore_probe.cpp` before "fixing" it to match.

**Measuring that the loop runs while nothing is pressed** — the check Epic 5
needed, and the only one that can judge termforge #58 from here. Enter a game
that advances on its own, put it in a mode where it survives, then send *nothing*
and count repaints:

```bash
{ sleep 3; printf '\033[B\033[B'; sleep 1; printf '\r'; sleep 1; printf 'm'; \
  sleep 8; printf '\033'; sleep 1; printf '\033'; sleep 2; } | \
  timeout 40 script -q -c "$PWD/build/src/bin/glyphcade" /tmp/idle.raw
perl -pe 's/\e\][^\a\e]*(\a|\e\\)//g; s/\e_[^\e]*\e\\//g; s/\e\[[0-9;?]*[a-zA-Z]//g' \
  /tmp/idle.raw | grep -o '@@' | wc -l    # 91 over 8 s == ~11.4/s
```

Snake steps every 100 ms at Normal, so ~80 head repaints in eight seconds is the
floor for a healthy loop; #58 capped an idle loop at ~7.5 fps, which puts a hard
ceiling of ~60 on the same window. **Counting a glyph is the measurement** — do
not try to read a frame rate out of the capture directly, and remember the
renderer diffs, so only cells that changed are rewritten.

⚠ **Pick a glyph that belongs to ONE thing.** Snake's `@@` is only ever the head,
so the count is steps. Tetris' `##` is the whole active piece, so its count is
steps x cells-repainted and only its ORDER OF MAGNITUDE means anything — 28
repaints over 8 s at a 1000 ms gravity is about seven steps of a four-cell piece,
which is right, and a per-step number would need a glyph only one cell uses.

**Checking that a game got the keyboard tier it asked for.** Since term-game#32 a
game declares a `KeyboardMode` in its `kMeta` and the Shell sets it per entry, so
the request is visible in the byte stream:

```bash
grep -c $'\033\[>27u' /tmp/pty.raw    # 1 == Enhanced pushed on game entry
grep -aoc $'\033\[=0;1u' /tmp/pty.raw # 1 == restored to Legacy on the way out
grep -c $'\033\[<u' /tmp/pty.raw      # 1 == popped by leave_screen — ALWAYS 1
```

⚠ **A run with no game that asks for a tier is the control**, and it is not
optional: the first two counts must be **zero** there, or the evidence says
nothing about the game you entered.

⚠ **The third count is NOT part of the control, and this file used to say it
was.** `\033[<u` comes from `detail::kLeaveSequence`, a fixed string literal
that `Terminal::leave_screen()` emits unconditionally — so it is **1 in every
run**, including one that never enters a game. Measured with three captures side
by side during Epic 7: selector-only and Sokoban-only both give `push=0
restore=0 pop=1`, and Tetris gives `1 1 1`. Demanding zero from all three makes
the control fail forever and invites someone to "fix" a game that is behaving
correctly. Sokoban is the natural control here, because it is a real game that
declares `Legacy`.

⚠ To reconstruct a whole screen from a capture, replay the CUP sequences AND
handle `\r`/`\n`. A replay that ignores them silently drops rows and looks
exactly like a game that failed to draw.

This covers rendering and terminal restore. It does **not** cover *feel* — frame
pacing, input latency, animation smoothness still need a human to play it, and
audio needs a human to hear it. Say so rather than guessing.

**But audio can at least be captured for someone who can hear it.** Since Epic 2:

```bash
GLYPHCADE_AUDIO_WAV=/tmp/session.wav ./build/src/bin/glyphcade
```

returns a `WavFileSink` instead of a device, in **both** audio arms, and
`Shell::on_tick` pumps it once per frame — so the file follows the game clock
rather than wall time and is deterministic under a fake clock. Play the session,
send the wav. That is the only way anything in this container gets judged by
ear, and it is how the SFX bank is meant to be tuned.

## Porting from HTML-Games

HTML-Games (a private sibling repo) has
working browser implementations of most of this roster — minesweeper, snake,
sokoban, solitaire, tetris, 2048. **The game logic is solved there**; a port is a
rendering and feel exercise, not a design one. Read the reference before
reinventing rules, especially fiddly ones (SRS wall kicks, 2048's double-merge,
first-click mine safety).

Its `SPEC.md` also documents the self-contained-game-per-directory convention
this repo inherits.

⚠ **Read the reference's CODE, never its README or its comments.** Two epics
running, the prose was the wrong half while the logic was fine:

- Tetris' README speed table was off by one level from its own code.
- Sokoban's README says its twenty pars are "derived from the optimal solution
  length". They are not read by the game at all, only three of twenty are
  correct, and **eight are below the mathematical minimum for their own level**.
  Its cited verifier is a path in `/tmp`, outside the repository.

If a reference states a number, **re-derive it** before porting it. A BFS over
`(player, boxes)` settled Sokoban's twenty in minutes and is the kind of thing
worth writing rather than trusting.

## Attribution

Agent-authored commits carry a trailer naming the model, e.g.

```
Co-Authored-By: Claude <noreply@anthropic.com>
```
