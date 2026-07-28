# STATUS — term-game

Live state. Update this when something lands; do not let it drift.

**Last updated: 2026-07-28** (Epic 3)

---

## Where the project actually is

**Epic 3 has landed. There is a game.** The binary opens a selector, you pick
Minesweeper, and you play it — mouse or keyboard, three difficulties, a timer
and a mine counter, pause and quit-to-menu. Green on GCC 14 and Clang 20, with
and without audio, under ASan and UBSan, with `-Werror` throughout.

Verified in a pty at **both** tiers — the ASCII/no-colour tier (which is also
the only tier the headless tests can reach, since `test_run_frames` installs the
fallback driver) and, with `TERM=xterm-256color`, the colour tier with rounded
borders, the 💣 icon and the Unicode tile set. Recipe in [AGENTS.md](AGENTS.md).

**`StubGame` is gone**, on its own deletion condition — Minesweeper proves the
same four things the diagnostic existed to prove, while also being a game.
`test/11selector`, `test/12registry` and `test/13tick` now point at it.

⚠ What is verified is *rendering and rules*, not **feel**. Click latency, cursor
responsiveness and whether the board is pleasant to play still need a human, and
there is no sound at all.

**Next move: Epic 2 (audio, gitea [#3](https://git.gobha.me/xcaliber/term-game/issues/3))** —
Minesweeper is the SFX consumer waiting for it, and `Board`'s verbs already
return "did anything change", which is where reveal/flag/explode/win will bind.
Epics 4 (2048) and 5 (Snake) are equally unblocked; nothing blocks any of them.
There is also one small, well-defined piece of housekeeping now unblocked:
**gitea [#16](https://git.gobha.me/xcaliber/term-game/issues/16) — delete
`guarded_run`**, whose upstream fix (termforge #71) shipped in v0.1.10 during
this epic.

---

## Blocking state

| Epic | State | Blocked by |
|---|---|---|
| 0 — Bootstrap | **done** | — |
| 1 — Arcade shell | **done** | — |
| 2 — Audio engine | **ready** | — |
| 3 — Minesweeper | **done** | — |
| 4 — 2048 | **ready** | — |
| 5 — Snake | **ready** | — |
| 6 — Tetris | **ready** | termforge #60 (degradable — feel only) |
| 7 — Sokoban | not started | termforge #64 → #63 |
| 8 — Solitaire | not started | termforge #63 |

**Nothing that ever blocked Epics 0–5 is still open.** termforge
[#27](https://github.com/gobha-me/termforge/issues/27) (install/export),
[#58](https://github.com/gobha-me/termforge/issues/58) (frame pacing),
[#59](https://github.com/gobha-me/termforge/issues/59) (`on_tick`) and
[#61](https://github.com/gobha-me/termforge/issues/61) (F5–F12) are all closed,
and we pin **v0.1.9** to get them. cpp-template CT-15 was fixed upstream at
`8f62930`; the build-tree `export(EXPORT ...)` block no longer exists, so there
was nothing for us to delete. See
[docs/cpp-template-audit.md](docs/cpp-template-audit.md) for what those cost us.

---

## What Epic 0 built

- **CMake scaffold** from cpp-template, project name `term-game` (follows the
  directory name, as does the gitea repo).
- **termforge consumed** via [cmake/deps/termforge.cmake](cmake/deps/termforge.cmake)
  — `find_package(termforge ... CONFIG)` first, FetchContent as the fallback.
  Epic 0 pinned v0.1.7; the pin is now **v0.1.9** — see below.
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

Epic 0's `BootApp` no longer exists — Epic 1 replaced it with the Shell.

---

## What Epic 1 built

- **`Game`** ([include/termgame/arcade/game.hpp](include/termgame/arcade/game.hpp))
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
| Audio in `GameContext` | Epic 2 — do not guess the handle before the engine exists (gitea [#3](https://git.gobha.me/xcaliber/term-game/issues/3)) |
| High-score persistence | the *second* scoring game, not the first — gitea [#14](https://git.gobha.me/xcaliber/term-game/issues/14) |
| One static library target per game | the second real game; today `src/lib/games/<slug>/` compiles into `term-game_lib`. Minesweeper was the first, so this now triggers on the **next** one |
| `StubGame` | **done** — deleted by Epic 3 |
| `Shell::quit_requested()` | delete when termforge [#73](https://github.com/gobha-me/termforge/issues/73) lands; it is duplicated state that exists only because `App::m_running` is unreadable — **still open** |
| The selector's gutter marker | delete when termforge [#72](https://github.com/gobha-me/termforge/issues/72) lands, and give the two columns back to the list |

---

## What Epic 3 built

Minesweeper, in four pieces, three of which name no termforge type at all:

- **[`board.hpp`](include/termgame/games/minesweeper/board.hpp)** — the rules.
  Deferred mine placement, flood fill, marks, chording, win/loss, and a clock
  driven only by `dt`. It includes **no termforge header**, which is what makes
  `test/14minesweeper` unable to construct a `Screen` rather than merely not
  doing so.
- **[`layout.hpp`](include/termgame/games/minesweeper/layout.hpp)** — integer
  geometry. One `Layout` per frame feeds both `draw()` and `on_event()`, so
  drawing and hit-testing cannot drift apart; `cell_at()` is round-tripped over
  every cell at every size.
- **[`glyphs.hpp`](include/termgame/games/minesweeper/glyphs.hpp)** — the two
  tile tiers, with three `static_assert`s (7-bit ASCII, one column each,
  pairwise distinct).
- **[`minesweeper.hpp`](include/termgame/games/minesweeper/minesweeper.hpp)** —
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
| **SFX** (reveal, flag, explode, win) | Epic 2 does not exist yet — gitea [#3](https://git.gobha.me/xcaliber/term-game/issues/3). `Board::reveal/cycle_mark/chord` already return "did anything change", which is the binding point. Issue [#4](https://git.gobha.me/xcaliber/term-game/issues/4) scoped audio as optional, degrading to silence. |
| **High-score persistence** | the *second* scoring game — gitea [#14](https://git.gobha.me/xcaliber/term-game/issues/14). `GameContext` has no persistence seam, and a fresh `Game` is built per entry, so an in-memory best time would die on quit-to-menu. The timer and mine counter ship; only the record does not. |
| A minimum terminal size in `GameMeta` | Hard needs 63x20 and the Shell's floor is 20x8, so the selector will launch a board the terminal cannot show. Epic 3 ships the in-game too-small screen instead — gitea [#15](https://git.gobha.me/xcaliber/term-game/issues/15). |

---

## Upstream framework work (termforge, GitHub)

| Issue | Gap | State |
|---|---|---|
| [#27](https://github.com/gobha-me/termforge/issues/27) | CMake consumption (install/export, `PROJECT_IS_TOP_LEVEL` gating) | **closed — shipped in v0.1.7** |
| [#58](https://github.com/gobha-me/termforge/issues/58) | Frame pacing: idle loop capped ~7.5fps | **closed — fixed** |
| [#59](https://github.com/gobha-me/termforge/issues/59) | No `on_tick(dt)` hook | **closed — shipped in v0.1.8** |
| [#60](https://github.com/gobha-me/termforge/issues/60) | No key release (Kitty keyboard protocol) | open |
| [#61](https://github.com/gobha-me/termforge/issues/61) | `Key` enum stops at F4 | **closed — shipped in v0.1.9** |
| [#62](https://github.com/gobha-me/termforge/issues/62) | `Cell` has no text attributes | open — costs Minesweeper a column per cell (63 vs 33 for Hard); commented with that number |
| [#63](https://github.com/gobha-me/termforge/issues/63) | `Image` has no blit/alpha compositing | open |
| [#64](https://github.com/gobha-me/termforge/issues/64) | MapWidget (Epic 3.6) | open |
| [#75](https://github.com/gobha-me/termforge/issues/75) | Mouse tracking mode hardcoded to `?1002h`; no `?1003h`, no way to disable | open — filed from Epic 3; not a blocker |
| [#71](https://github.com/gobha-me/termforge/issues/71) | `App::run()` skips `teardown()` on a throw | **closed — fixed in v0.1.10.** ⚠ We are pinned at v0.1.9, so `guarded_run` is still load-bearing *here* but its deletion condition is met — gitea [#16](https://git.gobha.me/xcaliber/term-game/issues/16) |
| [#72](https://github.com/gobha-me/termforge/issues/72) | `ListWidget` selection invisible at the fallback tier | open — gutter marker covers it |
| [#73](https://github.com/gobha-me/termforge/issues/73) | No way to observe `quit()`; `test_run_frames` re-arms `m_running` | open — `Shell::quit_requested()` covers it |

Check state with `gh` rather than trusting this table if it looks stale.

### The pin is v0.1.9, and the version request is patch-level

`cmake/deps/termforge.cmake` asks `find_package(termforge 0.1.9 …)`, not `0.1`.
termforge's package version file is `SameMinorVersion`, so `0.1` would silently
accept an installed **0.1.7** — which has no `set_tick_hz` — and turn "your copy
is too old, falling back to FetchContent" into a wall of compiler errors in
`shell.cpp`, on whichever machine happens to have a stale system copy. This is
the first time we depend on API introduced in a *patch* release; a floor has to
be expressed at the granularity the dependency actually moves at.

### `App::run()` does not restore the terminal on the exception path

Found while building Epic 0, and filed upstream. `app.hpp` promises the terminal
is "always restored on exit … even on exception", but `App::run()` has no
try/catch — a throw from `on_render` skips `teardown()` entirely. What rescues it
is `~App()`, which only runs if the App is destroyed by *unwinding*, which does
not happen when the exception escapes `main()` with no handler.

Our workaround is [`guarded_run`](include/termgame/arcade/run_guard.hpp), and it
has a deletion date: when `run()` guards its own exception path, delete that file,
its header, and the unwinding assertion in `test/21exception`.

⚠ **FIXED UPSTREAM in v0.1.10**, released partway through Epic 3 and found by
re-checking rather than by trusting this table. We are still pinned at **v0.1.9**,
so `guarded_run` is load-bearing in *this* tree today — but its deletion
condition is met, and doing it is the small piece of work tracked as gitea
[#16](https://git.gobha.me/xcaliber/term-game/issues/16). Deliberately not folded
into Epic 3: it moves the process's exception boundary and touches
`test/21exception`, which deserves its own verification pass rather than a
footnote in a game release.

### `ListWidget` cannot show its selection at the bottom tier

Found building Epic 1's selector, and filed as
[#72](https://github.com/gobha-me/termforge/issues/72). `ListWidget`'s only
selection affordance is the `theme::kFocusFg`/`kFocusBg` inversion, its colour
members are private with no setters, and `FallbackDriver` discards colour
entirely — so on a bare TTY the selected row is byte-identical to every other
row. That is the tier this repo promises always works, *and* the tier every
headless test runs under, since `test_run_frames` installs the fallback driver.

Our workaround is a two-column gutter with a `>` marker, drawn in
`Shell::draw_selector` from the widget's public `selected()`/`scroll_offset()`.
It has a deletion date and a known limitation: a click in the gutter lands
outside `m_list.rect()` and does not select.

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

Added by Epic 1:

- **The Shell never chains to `termforge::App::on_event`** — the base quits on
  Escape, and inside a game Escape must mean "back to the menu". The Shell is
  therefore total and handles Ctrl+C itself. This is a line that must stay
  *absent*, which is why it is both an AGENTS.md hard rule and a test.
- **A fresh `Game` per entry, from a registry factory** — freshness is
  structural, not a `start()` routine each game has to get right.
- **The framework owns the fixed timestep** — `set_tick_hz`/`set_max_tick_dt`,
  never a hand-rolled accumulator.

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
