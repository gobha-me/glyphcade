# AGENTS.md — conventions for AI agents working in this repo

If you're an LLM (or an LLM-driven editor) about to make changes here, read this
first, then [DESIGN.md](DESIGN.md) for the architecture and
[STATUS.md](STATUS.md) for what is actually true right now.

This is **term-game**: a TUI arcade suite in C++23, built on
[TermForge](https://github.com/gobha-me/termforge) for UI and
[RtAudio](https://github.com/thestk/rtaudio) for sound.

## ⚠ Two forges — check which one before filing anything

This is the single easiest mistake to make here.

| Repo | Forge | Tool | What goes there |
|---|---|---|---|
| **term-game** (this) | gitea `xcaliber/term-game` | `tea` | project work, game epics |
| **termforge** | GitHub `gobha-me/termforge` | `gh` | framework gaps/bugs |

Gitea repos live under the **user** `xcaliber`, not an org — `tea repos create
--owner xcaliber` fails with "org does not exist"; omit `--owner`. `tea issues
create` has no `--body-file`, so use `-d "$(cat file)"` for long bodies. HTTPS
clone from git.gobha.me needs auth; **SSH works**.

If TermForge is missing something this project needs, that is a `gh` issue
against termforge — not a workaround buried in game code. That feedback loop is
half the point of this repo.

## Baseline

- **CMake ≥ 3.28**, **C++23** (GCC 13+ / Clang 19+), Catch2 v3 via FetchContent.
- Scaffolded from [cpp-template](https://github.com/gobha-me/cpp-template);
  compiler respects the environment, clang and the sanitizers are opt-in
  toolchains.
- Project name is **`term-game`** and follows the directory name — it becomes the
  target, `term-game_lib`, and every `-Dterm-game_*` option. Directory, gitea
  repo, and CMake project name must agree.
- **rtaudio is optional.** `TERMGAME_WITH_AUDIO` auto-detects and defaults OFF
  when absent. The repo must always build and test without it.

## Hard rules (project-specific)

- **`term-game` and `termgame` are both correct. Do not "fix" either.** The
  project, the gitea repo, the CMake project and every `-Dterm-game_*` option are
  **`term-game`**; the include directory and the C++ namespace are **`termgame`**,
  because a hyphen is illegal in a namespace. This trips everyone once.
- **One `App`, many `Game`s.** The Shell is the only `termforge::App` — it owns
  the terminal, the loop, and the audio engine. Games are **not** `App`
  subclasses and never touch `App`, `Terminal`, or each other. Shared services
  reach them only through `GameContext`.
- **Register games explicitly** in `src/lib/arcade/all_games.cpp`. **Never use
  self-registering statics.** The linker drops unreferenced objects out of static
  libraries, so the registrar vanishes and the game silently disappears from the
  menu with no error at any stage. Explicit registration fails at compile time
  instead.
- **The audio callback is a realtime thread.** No locks, no allocation, no
  syscalls, no I/O inside it — ever. Commands cross a lock-free SPSC ring.
  Overflow drops and counts; it never blocks the UI thread and never grows.
- **Every game is playable at the bottom tier.** Pixel sprites are an
  enhancement over a glyph fallback that always exists — the same relationship
  `Widget::draw_pixels` has to `Widget::draw`. A game that needs Kitty is a bug.
- **Degradation is an event** (inherited from TermForge). Any fallback raises an
  `ErrorEvent`, never a silent downgrade.
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
- **Audio** — render through `WavFileSink` and assert samples. Golden files per
  SFX; a mixer test that N simultaneous voices neither clip nor drift.
- **The SPSC ring** — TSan-clean producer/consumer test. This is the one place a
  bug is a heisenbug, so it gets the most adversarial treatment.

## ⚠ Audio cannot be verified in the dev container

There is **no `/dev/snd`** and no PulseAudio here. Audio is developed against
`NullSink`/`WavFileSink` and verified on the maintainer's own machine, which has
`librtaudio-dev` installed.

Report audio changes as *"builds clean, offline sink tests pass, needs device
verification"* — **never as verified**. Claiming otherwise is unfounded.

## How to verify before a PR

Three configurations minimum. The second is not redundant with the first: this
container has `librtaudio-dev`, so detection defaults **ON** here and the OFF arm
— the one CI runs and the one this repo promises always works — is only exercised
if you ask for it.

```bash
# 1. GCC, audio auto-detected
cmake -B build -DCMAKE_CXX_FLAGS=-Werror && cmake --build build --parallel \
  && ctest --test-dir build --output-on-failure

# 2. GCC, audio explicitly OFF
cmake -B build-noaudio -DTERMGAME_WITH_AUDIO=OFF -DCMAKE_CXX_FLAGS=-Werror \
  && cmake --build build-noaudio --parallel \
  && ctest --test-dir build-noaudio --output-on-failure

# 3. Clang
cmake -B build-clang -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/clang.cmake \
  -DCMAKE_CXX_FLAGS=-Werror && cmake --build build-clang --parallel \
  && ctest --test-dir build-clang --output-on-failure
```

Pass `-DCMAKE_CXX_FLAGS=-Werror` — and note that it only works because our
`cmake/toolchain/default.cmake` appends rather than replaces. If you ever re-sync
that file from cpp-template you will silently lose it; check with
`grep -- -Werror build/compile_commands.json`. Sanitizers via
`cmake/toolchain/{address,undefined}.cmake`.

`ctest` must be green with **no `-E` exclusion** — in particular `artifact-check`
runs in enforce mode and must print `CLEAN`. If it reports Class-B failures right
after you add files, `git add` them first: it reads `git ls-files`, so untracked
files look like missing ones.

### Checking the terminal itself, without a human

An agent has no terminal, but it can allocate a pty with `script(1)` and read
back exactly what the app wrote. Use this instead of claiming a rendering or
terminal-restore change is unverifiable:

```bash
{ sleep 3; printf '\033'; sleep 2; } | timeout 20 \
  script -q -c "$PWD/build/src/bin/term-game" /tmp/pty.raw
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
  timeout 30 script -q -c "$PWD/build/src/bin/term-game" /tmp/pty.raw
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

This covers rendering and terminal restore. It does **not** cover *feel* — frame
pacing, input latency, animation smoothness still need a human to play it, and
audio needs a human to hear it. Say so rather than guessing.

## Porting from HTML-Games

[HTML-Games](https://git.gobha.me/xcaliber/HTML-Games) (gitea, SSH clone) has
working browser implementations of most of this roster — minesweeper, snake,
sokoban, solitaire, tetris, 2048. **The game logic is solved there**; a port is a
rendering and feel exercise, not a design one. Read the reference before
reinventing rules, especially fiddly ones (SRS wall kicks, 2048's double-merge,
first-click mine safety).

Its `SPEC.md` also documents the self-contained-game-per-directory convention
this repo inherits.

## Attribution

Agent-authored commits carry a trailer naming the model, e.g.

```
Co-Authored-By: Claude <noreply@anthropic.com>
```
