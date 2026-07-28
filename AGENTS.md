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

```bash
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
cmake -B build-clang -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/clang.cmake \
  && cmake --build build-clang && ctest --test-dir build-clang
```

Both compilers must build clean and pass, with and without
`TERMGAME_WITH_AUDIO`. Anything touching *feel* — frame pacing, input latency,
animation — also needs a human to actually play it; the agent cannot see a
terminal or hear a speaker. Say so rather than guessing.

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
