# STATUS — term-game

Live state. Update this when something lands; do not let it drift.

**Last updated: 2026-07-28**

---

## Where the project actually is

**Epic 0 has landed. The repo builds.** `cmake -B build && cmake --build build
&& ctest` is green on GCC 14 and Clang 20, with and without audio, under ASan and
UBSan, and `cmake --install` produces a package an external `find_package(term-game
CONFIG)` consumer can actually link. Verified in a pty: the binary enters the
alternate screen, renders the placeholder, and ESC restores the terminal and
exits 0 — see the `script(1)` recipe in [AGENTS.md](AGENTS.md).

What exists is a scaffold, not a game: [`BootApp`](include/termgame/arcade/boot_app.hpp)
is a placeholder that proves the wiring, and **it is not the Shell** — Epic 1
builds that.

**Next move: Epic 1 (arcade shell).** Nothing blocks it.

---

## Blocking state

| Epic | State | Blocked by |
|---|---|---|
| 0 — Bootstrap | **done** | — |
| 1 — Arcade shell | **ready** | — |
| 2 — Audio engine | **ready** | — |
| 3 — Minesweeper | not started | Epic 1 |
| 4 — 2048 | not started | Epic 1 |
| 5 — Snake | not started | Epic 1 |
| 6 — Tetris | not started | Epic 1; termforge #60 (degradable) |
| 7 — Sokoban | not started | termforge #64 → #63 |
| 8 — Solitaire | not started | termforge #63 |

**Both of the blockers that held Epic 0 since the design phase are cleared.**
termforge [#27](https://github.com/gobha-me/termforge/issues/27) is closed —
v0.1.7 ships `install(EXPORT)`, a package config, and `PROJECT_IS_TOP_LEVEL`
gating on every option, so both consumption paths work and both give
`termforge::lib`. cpp-template CT-15 was fixed upstream at `8f62930`; the
build-tree `export(EXPORT ...)` block no longer exists, so there was nothing for
us to delete. See [docs/cpp-template-audit.md](docs/cpp-template-audit.md) for
what those two actually cost us.

---

## What Epic 0 built

- **CMake scaffold** from cpp-template, project name `term-game` (follows the
  directory name, as does the gitea repo).
- **termforge consumed** via [cmake/deps/termforge.cmake](cmake/deps/termforge.cmake)
  — `find_package(termforge 0.1 CONFIG)` first, FetchContent at **v0.1.7** as
  the fallback.
- **`TERMGAME_WITH_AUDIO`** auto-detection in [cmake/audio.cmake](cmake/audio.cmake).
  Nothing links rtaudio yet; Epic 2 owns that, and it is an export question —
  see the note at the bottom of that file.
- **`guarded_run`** — the process's exception boundary, because
  `termforge::App::run()` does not have one. See below.
- **Tests:** `00bootstrap` (the audio option reached the compiler),
  `10render` (headless render, no tty), `21exception` (a throwing frame exits 1
  *and* unwinds the App).
- **CI** at [.gitea/workflows/ci.yaml](.gitea/workflows/ci.yaml) — written, not
  yet proven green; the runner image's toolchain was not verifiable when it was
  written, which is why its first job asserts the floor rather than assuming it.

---

## Upstream framework work (termforge, GitHub)

| Issue | Gap | State |
|---|---|---|
| [#27](https://github.com/gobha-me/termforge/issues/27) | CMake consumption (install/export, `PROJECT_IS_TOP_LEVEL` gating) | **closed — shipped in v0.1.7** |
| [#58](https://github.com/gobha-me/termforge/issues/58) | Frame pacing: idle loop capped ~7.5fps | **closed — fixed** |
| [#59](https://github.com/gobha-me/termforge/issues/59) | No `on_tick(dt)` hook | open — **work in progress upstream** |
| [#60](https://github.com/gobha-me/termforge/issues/60) | No key release (Kitty keyboard protocol) | open |
| [#61](https://github.com/gobha-me/termforge/issues/61) | `Key` enum stops at F4 | open |
| [#62](https://github.com/gobha-me/termforge/issues/62) | `Cell` has no text attributes | open |
| [#63](https://github.com/gobha-me/termforge/issues/63) | `Image` has no blit/alpha compositing | open |
| [#64](https://github.com/gobha-me/termforge/issues/64) | MapWidget (Epic 3.6) | open |

Check state with `gh` rather than trusting this table if it looks stale.

### #59 is being worked on upstream — do not design around its absence yet

As of 2026-07-28 the termforge checkout carries an in-flight `feat/59-on-tick`
branch adding `on_tick(dt)`, `set_tick_hz(n)` (a fixed-timestep accumulator) and
a max-dt clamp — i.e. exactly the thing DESIGN.md says the Shell must implement
as a workaround. **It is not in v0.1.7**, which is what we pin. Epic 1 should
re-check before writing that accumulator by hand; if it has landed, use it and
delete the workaround note in DESIGN.md rather than duplicating the framework.

### `App::run()` does not restore the terminal on the exception path

Found while building Epic 0, and filed upstream. `app.hpp` promises the terminal
is "always restored on exit … even on exception", but `App::run()` has no
try/catch — a throw from `on_render` skips `teardown()` entirely. What rescues it
is `~App()`, which only runs if the App is destroyed by *unwinding*, which does
not happen when the exception escapes `main()` with no handler.

Our workaround is [`guarded_run`](include/termgame/arcade/run_guard.hpp), and it
has a deletion date: when `run()` guards its own exception path, delete that file,
its header, and the unwinding assertion in `test/21exception`.

---

## Divergences from cpp-template

Two, both deliberate, both filed upstream, both with a deletion condition. Do not
"re-sync" these files from the template without reading this.

| File | Divergence | Delete when |
|---|---|---|
| [cmake/toolchain/default.cmake](cmake/toolchain/default.cmake) | Appends to `CMAKE_CXX_FLAGS` instead of replacing it, plus `include_guard(GLOBAL)`. Upstream's plain `set()` silently discards `-DCMAKE_CXX_FLAGS=-Werror` from the command line, because a toolchain normal variable shadows the cache — CI stays green while enforcing nothing. termforge already carries this exact fix. | upstream appends |
| [cmake/check_artifacts.cmake](cmake/check_artifacts.cmake) | Rule A1's path filter narrowed from `.*` to `^README\.md$`. The rule's own label says "README CI badge", but upstream scans every tracked file, which makes it impossible for a fork to document its own provenance — something NEW_PROJECT.md Step 1 tells you to do. | upstream narrows it |

---

## Decisions already made (don't relitigate)

Settled and recorded in [DESIGN.md](DESIGN.md) with reasoning:

- **Suite, not one long-form game** — each game is an independent vertical slice
  that dogfoods one subsystem, so a blocked game blocks only itself.
- **Roster ordered by increasing framework risk** — Minesweeper needs nothing,
  Snake forced #58, Solitaire is the flagship.
- **Explicit game registration**, never self-registering statics.
- **SFX synthesized, not sampled** — no decoder, no asset pipeline, no blobs in
  git. Venice is for sprite/title art instead.
- **Audio behind an `AudioSink`** with `WavFileSink` for offline testing.
- **Port logic from HTML-Games** rather than reinventing game rules.
- **Spelling split:** the project, repo and CMake target are **`term-game`**; the
  include dir and C++ namespace are **`termgame`**. A hyphen is illegal in a
  namespace. Not a typo — do not "fix" it.

---

## Environment facts that bite

- **rtaudio is installed in the dev container, but there is no sound hardware.**
  `librtaudio-dev 5.2.0` and `libasound2-dev` are present; `/dev/snd` is not.
  Two consequences, and the first is easy to miss:
  1. `TERMGAME_WITH_AUDIO` **auto-detects to ON here.** The OFF arm — the
     configuration CI runs and the one this repo promises always works — has to
     be built explicitly (`-DTERMGAME_WITH_AUDIO=OFF`) or it rots unnoticed.
  2. Nothing audio can ever be *device*-verified from this container. Report
     audio work as "builds clean, offline sink tests pass, needs device
     verification" — never as verified.
- **Debian ships no `RtAudioConfig.cmake`**, only a `.pc` file, so pkg-config is
  the load-bearing detection path in `cmake/audio.cmake`. A `find_package(RtAudio
  CONFIG)`-only design would look right and never fire on any apt-based box.
- **Two forges.** term-game and HTML-Games are gitea (`tea`); termforge and
  cpp-template are GitHub (`gh`). See [AGENTS.md](AGENTS.md).
- **github.com over HTTPS, git.gobha.me over SSH.** There is no GitHub SSH key
  here, which is why the termforge FetchContent URL is HTTPS.
- `arcadectl` on gitea is a **k8s controller for game-server deployments** —
  unrelated to this project despite the name.
