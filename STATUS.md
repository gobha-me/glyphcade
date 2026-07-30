# STATUS — term-game

Live state. Update this when something lands; do not let it drift.

**Last updated: 2026-07-30** (gitea #14 — high-score persistence)

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
responsiveness and whether the board is pleasant to play still need a human.
Minesweeper now asks for sounds (Epic 2, below), but whether they are the RIGHT
sounds is equally a question only an ear can answer.

**Epic 2 has landed.** There is an audio engine: a lock-free SPSC command ring,
integer-phase oscillators with a linear envelope, an eight-voice mixer that
cannot clip, three sinks, and SFX bound into both the selector and Minesweeper.
gitea [#13](https://git.gobha.me/xcaliber/term-game/issues/13) is decided and
enforced by a test. See "What Epic 2 built" below.

⚠ **It has never been heard.** Nothing in this container can play a sound. What
is verified is that it builds in six arms, that the offline path renders what it
claims to, and that the no-device path degrades correctly — not that the bank
sounds good, or indeed that a real device works at all.

**Epic 4 has landed. There are two games.** 2048 is registered, playable with
arrows/hjkl/wasd, one level of undo, and it is the first game with **motion** — a
designed tween rather than a ported one, because the HTML reference has no
working slide animation to port. See "What Epic 4 built" below.

That second registered game also turned on what it promised: four `size() > 1`
assertions in `test/11selector` went live for free, and the case they could never
cover — a click on a **non**-selected row, the only gesture that discriminates the
`State::Selector` guard in the mouse path — is now written.

⚠ What is verified is rules, geometry, rendering and sound-intent. **Feel is
not**: whether 90 ms of slide and 70 ms of pop are right, whether the board is
pleasant to play, and whether Slide and Merge sound like anything — all still
need a human. Nothing in this container can judge any of them.

**Epic 4's follow-up has landed too: high scores persist** (gitea
[#14](https://git.gobha.me/xcaliber/term-game/issues/14)). Both games keep a
record across quit-to-menu and across restarts — 2048 a best score, Minesweeper
a best time per difficulty — in a versioned text file under `$XDG_DATA_HOME`.
See "What the score store is" below.

**Next move: Epic 5 (Snake)**, or gitea
[#24](https://git.gobha.me/xcaliber/term-game/issues/24) (the termforge pin is
five tags behind).

Since Epic 3, two housekeeping issues have landed.
gitea [#16](https://git.gobha.me/xcaliber/term-game/issues/16) moved the pin to
termforge v0.1.10 and retired the `guarded_run` workaround — see the
exception-boundary section below for what survived it and why. gitea
[#17](https://git.gobha.me/xcaliber/term-game/issues/17) moved it again, to
**v0.1.15**, and retired **both remaining workarounds**: the selector's gutter
marker and `Shell::quit_requested()`.

**There are now no workarounds left in this repo.** Three termforge issues were
filed from building it — [#71](https://github.com/gobha-me/termforge/issues/71),
[#72](https://github.com/gobha-me/termforge/issues/72) and
[#73](https://github.com/gobha-me/termforge/issues/73) — all three were fixed
upstream, and all three of our stopgaps are gone, each on the deletion condition
it was written with. That loop closing is the thesis of running the two projects
together, so it is worth stating once rather than leaving implied.

---

## Blocking state

| Epic | State | Blocked by |
|---|---|---|
| 0 — Bootstrap | **done** | — |
| 1 — Arcade shell | **done** | — |
| 2 — Audio engine | **done** | — |
| 3 — Minesweeper | **done** | — |
| 4 — 2048 | **done** | — |
| 5 — Snake | **ready** | — |
| 6 — Tetris | **ready** | termforge #60 (degradable — feel only) |
| 7 — Sokoban | not started | ~~termforge #64 → #63~~ — both shipped upstream; blocked on the pin bump, gitea [#24](https://git.gobha.me/xcaliber/term-game/issues/24) |
| 8 — Solitaire | not started | ~~termforge #63~~ — shipped upstream; blocked on the pin bump, gitea [#24](https://git.gobha.me/xcaliber/term-game/issues/24) |

**Nothing that ever blocked Epics 0–5 is still open.** termforge
[#27](https://github.com/gobha-me/termforge/issues/27) (install/export),
[#58](https://github.com/gobha-me/termforge/issues/58) (frame pacing),
[#59](https://github.com/gobha-me/termforge/issues/59) (`on_tick`) and
[#61](https://github.com/gobha-me/termforge/issues/61) (F5–F12) are all closed,
and we pin **v0.1.15** to get them, plus
[#71](https://github.com/gobha-me/termforge/issues/71) (terminal restore on the
exception path), [#72](https://github.com/gobha-me/termforge/issues/72)
(`ListWidget` marks its own selection) and
[#73](https://github.com/gobha-me/termforge/issues/73) (`App::running()`). cpp-template CT-15 was fixed upstream at
`8f62930`; the build-tree `export(EXPORT ...)` block no longer exists, so there
was nothing for us to delete. See
[docs/cpp-template-audit.md](docs/cpp-template-audit.md) for what those cost us.

---

## What Epic 0 built

- **CMake scaffold** from cpp-template, project name `term-game` (follows the
  directory name, as does the gitea repo).
- **termforge consumed** via [cmake/deps/termforge.cmake](cmake/deps/termforge.cmake)
  — `find_package(termforge ... CONFIG)` first, FetchContent as the fallback.
  Epic 0 pinned v0.1.7; the pin is now **v0.1.15** — see below.
- **`TERMGAME_WITH_AUDIO`** auto-detection in [cmake/audio.cmake](cmake/audio.cmake).
  Epic 2 answered the export question (gitea #13): rtaudio is linked by
  `src/audio_backend/` alone, which is never installed and never exported.
- **`run_or_report`** — the process's exception boundary. Until v0.1.10 it was
  also the terminal-restore workaround for termforge #71; that half is
  upstream's now, and what is left converts an escaping exception into a
  readable diagnostic and exit 1 instead of 134 via SIGABRT. Renamed from
  `guarded_run` by gitea #16. See below.
- **Tests:** `00bootstrap` (the audio option reached the compiler),
  `10render` (headless render, no tty), `21exception` (upstream tore the
  terminal down before the throw reached us, and our boundary turned it into
  exit 1), `pty-restore` (the same claim in a real pty, where the escape bytes
  are visible — added by gitea #16).
- **CI** at [.gitea/workflows/ci.yaml](.gitea/workflows/ci.yaml) — **green since
  gitea [#10](https://git.gobha.me/xcaliber/term-game/issues/10)**; see the CI
  section below for what it runs and why it took three epics to get there.

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
| Audio in `GameContext` | **shipped in Epic 2** as `audio() -> const audio::Player&`, additively, exactly as the seam promised |
| High-score persistence | **shipped after Epic 4** as `scores() -> const scores::Recorder&`, additively, exactly as the seam promised — and the "second scoring game, not the first" condition is what kept it from shipping as one integer. gitea [#14](https://git.gobha.me/xcaliber/term-game/issues/14) |
| One static library target per game | **done** — landed ahead of Epic 4 as its own change, since a build restructure bundled with a new game makes a red CI run ambiguous. `src/lib` is now `term-game_lib` → `_roster` → `_game_<name>` → `_core`; see the section below |
| `StubGame` | **done** — deleted by Epic 3 |
| `Shell::quit_requested()` | **done** — retired by gitea [#17](https://git.gobha.me/xcaliber/term-game/issues/17); termforge [#73](https://github.com/gobha-me/termforge/issues/73) shipped `App::running()` in v0.1.14. ⚠ Not a drop-in: see the section below |
| The selector's gutter marker | **done** — retired by gitea [#17](https://git.gobha.me/xcaliber/term-game/issues/17); termforge [#72](https://github.com/gobha-me/termforge/issues/72) shipped in v0.1.11 and the two columns went back to the list |

---

## One static library per game

Landed ahead of Epic 4, deliberately on its own: it is a **pure refactor** — no
C++ moved, no symbol left the program, only which archive holds it — and a build
restructure bundled with a new game makes a red CI run ambiguous between the two.

```
term-game_lib      arcade/shell.cpp, arcade/exception_boundary.cpp
  ↓                the ALIAS, the export, the only spelling outside src/lib
term-game_roster   arcade/all_games.cpp
  ↓
term-game_game_minesweeper
  ↓
term-game_core     build_info.cpp, audio/*.cpp
```

`term-game_lib` stayed the umbrella, which is why **`src/bin` and all 14
auto-discovered test dirs needed no change at all** — they link
`${PROJECT_NAME}::lib`, still an ALIAS to a STATIC target, now carrying the whole
chain PUBLIC.

### ⚠ What would silently undo this

- **`arcade/shell.cpp` must stay in `term-game_lib`, above the roster.** It is the
  only TU that references `all_games()` (three sites), so its archive must be
  scanned first. Tidying it down into `core` closes a cycle
  `core → roster → game → core`. That fails *loudly* — `undefined reference to
  termgame::all_games()` — and, verified both ways, in **every** link declaration
  order: CMake emits the topological order, and `game → core` pins core last, so
  no `target_link_libraries` argument order can rescue it and `core` never gets
  duplicated. The fix is always to move the file.
- **`audio/*.cpp` must stay in `core`, below the games.** `minesweeper.cpp` calls
  `audio::Engine::play`. That one edge is the whole reason core reads
  "build_info + audio" rather than "everything that is not a game".
- **`target_link_libraries(_roster PUBLIC ${_game_targets})` is load-bearing
  twice.** Removing it fails at *compile* time, not link time —
  `all_games.cpp:20` cannot find `<termgame/arcade/registry.hpp>`, because that
  edge carries core's include directory as well as the game archives.
- **`TERMGAME_WITH_AUDIO` belongs on `core`, PRIVATE.** `build_info.cpp` is the
  one TU in the repo that reads it. Promoting it to `term-game_lib` to look
  tidier stops it reaching that TU, `build_has_audio()` answers false in an
  audio-ON build, and `test/00bootstrap` goes red — the tripwire it exists to arm.
- **Every target must be in the export set.** This one enforces itself:
  `install(EXPORT)` refuses to name a target it cannot resolve, so adding a game
  and forgetting `src/lib`'s game list stops *generation*. The list is handed to
  `cmake/install.cmake` via `PARENT_SCOPE`, and `EXPORT_NAME`s are derived by
  stripping the `term-game_` prefix, so a new game needs no line there.

**Packaging changed, intentionally**: the install prefix gains three `.a` files
and `term-gameTargets.cmake` now defines four imported targets —
`term-game::lib`, `::roster`, `::game_minesweeper`, `::core`.
`find_package(term-game)` + `target_link_libraries(app term-game::lib)` is
unchanged. `include/` and `bin/` are unchanged. A consumer linking
`term-game::core` alone would get an undefined `all_games()`, which is the
roster becoming a substitutable piece rather than a defect.

**A guarantee the layering now provides for free:** a game cannot reach the Shell,
because core sits *below* the games, so `termgame::Shell` is not on a game's link
line. AGENTS.md's "games never touch `App`, `Terminal`, or each other" is a link
error rather than a convention.

### How it was proven to be a pure refactor

Measured against a build of the parent commit on the same box, not asserted:

- **175/175 compile commands identical** modulo `-DTERMGAME_WITH_AUDIO`, which
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
- Six arms green at 20/20: GCC, GCC `TERMGAME_WITH_AUDIO=OFF`, Clang, TSan
  (`RelWithDebInfo`), ASan, UBSan — all `-Werror`, no `ctest -E`.

Mutation-tested five ways, and **two of the five findings corrected a claim** in
the design that had been reasoned out carefully and was wrong — the word-order
story above, and the roster edge failing at compile rather than link time. See
the commit message for all five verbatim.

---

## What the score store is (gitea #14)

Landed straight after Epic 4, on the condition the issue itself set: **the second
scoring game, not the first**. That condition paid for itself. 2048 wants a best
*score*, Minesweeper wants a best *time per difficulty* — so a record is a keyed
value with a **direction** (`Better::Higher` / `Better::Lower`), and had this
shipped with Minesweeper alone it would have been one integer per game and wrong.
Wiring the *second* game is what proved the shape; wiring the first only closed
the issue.

**Where it lives.** `include/termgame/arcade/scores.hpp` +
`src/lib/arcade/scores.cpp`, in **`term-game_core`** — both games call it, which
is the rule at `src/lib/CMakeLists.txt`. The `arcade/` prefix does *not* imply
`_lib`; its three siblings sit in three different targets. No new dependency:
`<filesystem>` and `<fstream>` were already in core via `audio/sink.cpp`.

**The file**, at `$XDG_DATA_HOME/term-game/scores` (else `$HOME/.local/share/…`,
else memory-only):

```
# term-game scores v1
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

- **[`board.hpp`](include/termgame/games/twenty48/board.hpp)** — the rules.
  Slide/merge per direction, 90/10 spawning, win as a latch, loss via
  `can_move()`, one level of undo. It includes **no termforge header**, which is
  what makes `test/22twenty48` *unable* to construct a `Screen`.
- **[`anim.hpp`](include/termgame/games/twenty48/anim.hpp)** — the tween. Also no
  termforge, and it does not know `Board` either: it takes a span of cell values,
  so it is drivable by N fixed ticks with no terminal.
- **[`layout.hpp`](include/termgame/games/twenty48/layout.hpp)** — 6×3 tiles, gap
  1, **29×19 needed**. `tile_x`/`tile_y` have `double` overloads, which is the
  tween's only entry into geometry.
- **[`glyphs.hpp`](include/termgame/games/twenty48/glyphs.hpp)** — the colour ramp
  ported from the reference's CSS, the ASCII lattice, and four `static_assert`s.
- **[`twenty48.hpp`](include/termgame/games/twenty48/twenty48.hpp)** — the `Game`,
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
[`arcade/rng.hpp`](include/termgame/arcade/rng.hpp) rather than being copied,
since being byte-identical across toolchains is the whole reason it is
hand-rolled. `minesweeper::Rng` still resolves via a using-declaration, so no call
site moved — and minesweeper's seed-pinned mine layouts are unchanged, which is
how we know the move was code-identical.

### Audio

`SfxId` gains **Slide** and **Merge**. A move that merges plays Merge *instead of*
Slide, so one gesture is still one sound.

- **No Spawn effect**, despite gitea #5 listing one: a spawn happens on every
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
| **High-score persistence** | **done**, immediately after Epic 4 — gitea [#14](https://git.gobha.me/xcaliber/term-game/issues/14). Wired into *both* games, and that is what proved the record is not one integer |
| A **mouse** gesture | 2048 is four directions and an undo. Nothing a click could mean that a key does not already say, and inventing one is a feel decision with no reference behind it |
| A minimum terminal size in `GameMeta` | gitea [#15](https://git.gobha.me/xcaliber/term-game/issues/15), same answer as Epic 3: the game ships its own too-small screen (needs 29×19; the Shell's floor is 20×8) |
| A **tuned** tween | 90 ms slide, 70 ms pop, linear. Named constants in `anim.hpp` rather than inline, precisely so whoever can play it has one place to change. An ease curve is a feel decision |
| **In-game pop at the ASCII tier** | A character cell cannot scale a glyph, and a merge can produce a six-digit label in a six-column tile, so there is no room for decoration. The pop is colour-tier emphasis; at the bottom tier the number changed, which is the information |

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
| **SFX** (reveal, flag, explode, win) | **shipped in Epic 2.** Bound in `minesweeper.cpp` via `announce()`, which compares board state across the verb — `Board` learned nothing about audio. |
| **High-score persistence** | **done** — gitea [#14](https://git.gobha.me/xcaliber/term-game/issues/14), after Epic 4 met its "second scoring game" condition. The diagnosis here was right: a fresh `Game` per entry is exactly why the store had to live on the Shell. Minesweeper now shows `BEST nnn` beside the timer. |
| A minimum terminal size in `GameMeta` | Hard needs 63x20 and the Shell's floor is 20x8, so the selector will launch a board the terminal cannot show. Epic 3 ships the in-game too-small screen instead — gitea [#15](https://git.gobha.me/xcaliber/term-game/issues/15). |

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
| The ring drops the **newest** command, not the oldest | gitea #3 said oldest; that cannot be done inside the SPSC contract without the producer writing the consumer's index. The issue was amended. |
| No `sin`/`exp` anywhere — integer phase, linear envelopes | glibc's transcendentals are not correctly-rounded and move between versions; `-ffp-contract` defaults differ. Same argument `board.hpp` makes for splitmix64. Result: byte-identical output across GCC -O0/-O2/-O3 and Clang -O2. |
| Numeric fingerprints, **not** golden files | AGENTS.md asked for golden files and forbade binary blobs two sections earlier. A cross-toolchain byte digest is also a portability trap; "we measured it identical" is not "it is specified". |
| Headroom by `static_assert`, not a limiter | 8 voices × 1/8 FS cannot clip, provably. A limiter is something you must HEAR to trust. Cost: one sound peaks at −18 dBFS. |
| `play()` short-circuits on a Discard sink | Otherwise the ring fills once and `dropped()` climbs forever on the `TERMGAME_WITH_AUDIO=OFF` arm CI runs — destroying the one counter that means "the audio thread is in trouble". |
| rtaudio in a never-exported target (gitea #13) | A PRIVATE link still reaches the exported Targets file as `$<LINK_ONLY:...>`. Guarded by the `audio-export-clean` ctest. |

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
   for — a PRIVATE `PkgConfig::RTAUDIO` link into `term-game_lib` — and catches
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
- **`TERMGAME_WITH_AUDIO` now looks unused inside `src/lib`** — no audio source
  is `#ifdef`'d on it. It is not unused: it is what `build_has_audio()` reports
  and what `test/00bootstrap` asserts against CMake's own belief.

### What Epic 2 deliberately did not build

| Deferred | Condition to revisit |
|---|---|
| **A tuned bank** | Nothing here has been heard. Render a session with `TERMGAME_AUDIO_WAV=/tmp/session.wav ./build/src/bin/term-game` and listen; the fingerprints in `test/18audio-synth` record what the bank IS, not what it should sound like, and are expected to move when it is retuned. |
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
| [#75](https://github.com/gobha-me/termforge/issues/75) | Mouse tracking mode hardcoded to `?1002h`; no `?1003h`, no way to disable | **closed — shipped as `Terminal::set_mouse_mode` in v0.1.15, and we are pinned to it — but nothing calls it.** The default, `MouseMode::Drag`, is byte-for-byte what we already emitted, so taking the tag changed nothing. `MouseMode::Motion` is what Minesweeper wants for buttonless hover; deferred to gitea [#18](https://git.gobha.me/xcaliber/term-game/issues/18) because it is a *feel* change and the dev container cannot verify feel |
| [#71](https://github.com/gobha-me/termforge/issues/71) | `App::run()` skips `teardown()` on a throw | **closed — shipped in v0.1.10, and we are on it** (gitea [#16](https://git.gobha.me/xcaliber/term-game/issues/16)). The terminal-restore workaround is gone; our boundary survives as a diagnostic. `test/21exception` asserts the upstream guarantee via `test_winch_hooked()`, `pty-restore` asserts the escape bytes. |
| [#72](https://github.com/gobha-me/termforge/issues/72) | `ListWidget` selection invisible at the fallback tier | **closed — shipped in v0.1.11, and we are on it** (gitea [#17](https://git.gobha.me/xcaliber/term-game/issues/17)). Our gutter marker is gone and the two columns went back to the list. The marker is `ListWidget`'s now, on by default and **inside `rect()`**, so a click on it selects — which the workaround could not do. `test/11selector` asserts the glyph in cell text at the ASCII tier, coverage the workaround never had. |
| [#73](https://github.com/gobha-me/termforge/issues/73) | No way to observe `quit()`; `test_run_frames` re-arms `m_running` | **closed — shipped in v0.1.14, and we are on it** (gitea [#17](https://git.gobha.me/xcaliber/term-game/issues/17)). ⚠ `App::running()` is **not** a drop-in for the accessor it replaced: it is not sticky, and `test_run_frames` still re-arms `m_running` on entry. Assert it *before* the `step()` you needed for a state transition, or it is vacuous. |

Check state with `gh` rather than trusting this table if it looks stale.

### The pin is v0.1.15, and the version request is patch-level

`cmake/deps/termforge.cmake` asks `find_package(termforge 0.1.15 …)`, not `0.1`.
termforge's package version file is `SameMinorVersion`, so `0.1` accepts *any*
installed 0.1.x, and that bites in three ways:

- **0.1.7** has no `set_tick_hz`, so accepting it turns "your copy is too old,
  falling back to FetchContent" into a wall of compiler errors in `shell.cpp`,
  on whichever machine happens to have a stale system copy. Loud, at build time.
- **0.1.9** compiles clean and links clean, and hands you an `App` whose `run()`
  does not restore the terminal when a frame throws. Nothing fails. The only
  symptom is a wedged terminal, on one developer's machine, on the day something
  happens to throw.
- **0.1.10** compiles, links and runs clean, and shows **no selection marker at
  all** on the fallback tier. Since gitea #17 the Shell does not draw one — it
  relies on `ListWidget` doing it, which 0.1.10's cannot. The suite stays green,
  because no test can see another package's glyphs.

The last two are why this matters more than it looks: three times now we have
depended on API introduced in a *patch* release, and twice missing it is
**silent**. A floor at minor granularity would not have caught any of the three.

⚠ From 0.1.11 this is also an **ABI** floor. That release added members to
`ListWidget`, and `Shell` holds one *by value* in `arcade/shell.hpp`, which we
install. A consumer resolving an older 0.1.x compiles our public header against
a different object layout than `term-game_lib` was built with — no link error,
just disagreement about a size. Anything held by value in an installed header
turns the build floor into an ABI floor.

The same argument is in the comment at the top of `cmake/deps/termforge.cmake`,
and the floor is repeated in `cmake/project-config.cmake.in` for the consumer
side. **All three places must say the same thing**; the config file used to say
`0.1` while claiming to match, which is exactly the drift to watch for.

### `App::run()` used not to restore the terminal on the exception path

Resolved history, kept because it is why the pin is patch-level and why there is
still a boundary in `main()` at all.

Found while building Epic 0 and filed as termforge #71. `app.hpp` promised the
terminal was "always restored on exit … even on exception", but `App::run()` had
no try/catch — a throw from `on_render` skipped `teardown()`. What rescued it was
`~App()`, which only runs if the App is destroyed by *unwinding*, which does not
happen when the exception escapes `main()` with no handler; `std::terminate` is
called without unwinding, and the terminal was left to the SIGABRT backstop.
Our workaround was `guarded_run`, and its whole mechanism was providing a
handler at the bottom of the stack so that unwinding happened at all.

**Fixed in v0.1.10**, and we are on it (gitea #16). `run()` now guards `setup()`
and delegates the loop to `run_loop()`, both `catch (...) { teardown(); throw; }`.

**What we kept, and why it diverges from #16's own instructions.** The issue said
delete the file. Upstream *rethrows* rather than converting to an exit code — on
the stated grounds that "an int has no room for it, and the library will not
decide that your exception was meaningless" — and points the consumer at exactly
this: "Catch it around `run()` if you want a diagnostic of your own." Deleting
outright would move a thrown frame from `1` + `term-game: fatal: <what>` to `134`
+ SIGABRT + silence. So the file survives as
[`run_or_report`](include/termgame/arcade/exception_boundary.hpp), stripped of
every terminal claim, and it no longer has a deletion condition — it has an
upstream dependency.

**What proves it now**, since the old unwinding assertion was pinning a mechanism
that is no longer load-bearing:

- `test/21exception` drives `App::test_run_guarded` and asserts, via
  `test_winch_hooked()`, that `teardown()` ran on the throw path — plus
  `REQUIRE_THROWS_AS`, which pins that upstream still rethrows.
- `pty-restore` ([cmake/pty_restore.sh](cmake/pty_restore.sh)) runs a
  deliberately-throwing probe under `script(1)` and asserts the alt-screen leave
  appears in the byte stream **before** the fatal message. ⚠ Its probe builds its
  App *outside* the boundary on purpose: shaped like `main.cpp`, `~App()` still
  restores the terminal and the test passes even with the upstream guard removed.
  That was found by mutating it, not by reading it.

### `ListWidget` used not to show its selection at the bottom tier

Found building Epic 1's selector, and filed as
[#72](https://github.com/gobha-me/termforge/issues/72). `ListWidget`'s only
selection affordance was the `theme::kFocusFg`/`kFocusBg` inversion, its colour
members were private with no setters, and `FallbackDriver` discards colour
entirely — so on a bare TTY the selected row was byte-identical to every other
row. That is the tier this repo promises always works, *and* the tier every
headless test runs under, since `test_run_frames` installs the fallback driver.

Our workaround was a two-column gutter with a `>` marker, drawn in
`Shell::draw_selector` from the widget's public `selected()`/`scroll_offset()`,
with a known limitation: a click in the gutter landed outside `m_list.rect()`
and did not select.

**Fixed in v0.1.11**, and we are on it (gitea
[#17](https://git.gobha.me/xcaliber/term-game/issues/17)). Upstream says the
selection twice — inverted colours *and* a marker glyph in a gutter the widget
reserves on every row, on by default, sourced from `mark_glyphs(BorderStyle)` so
it degrades to ASCII with everything else. The Shell's block is gone and the
list gets its full rect back.

**Handing the two columns back cost the text nothing**, which is worth recording
because it looks like it should have moved something: `ListWidget` computes
`text_x = rect.x + gutter_cols()` and `max_w = rect.w - gutter - 1`, so at 60x20
the item text still starts at x=3 with 19 columns, before and after. Nothing in
the layout shifted — which is also why no *existing* test could see this change.
The workaround's limitation is now inverted into a guarantee: upstream's gutter
is inside `rect()`, so a click on the marker selects its row.

⚠ **`m_list.set_style(style)` is load-bearing.** Only `BorderStyle::Ascii` yields
`>`; every other family yields `▸` (U+25B8), and the widget's default is
`Single`. Dropping that one line puts three bytes of UTF-8 on a terminal that has
told us it cannot draw a box. This is not hypothetical — it is what the pty
capture showed mid-branch, before the call was added.

**What proves it now**, since the workaround shipped for two epics with *no* test
at all — `test/11selector` had 13 cases and none of them looked at the marker:

- the selected row differs from an unselected one in **cell text**, not colour,
  read back through `Screen::at()` under the fallback driver, and the mark moves
  with the arrow keys.
- the whole selector screen is 7-bit at the ASCII tier — the only assertion that
  catches a dropped `set_style`.
- a click in the marker gutter enters that row's game.

Mutation-tested: dropping `set_style` reddens the marker and 7-bit cases,
`set_marker_enabled(false)` reddens the marker case, and restoring the old
`inner.x + 2` shrink reddens the marker and click cases.

### `App` had no way to observe `quit()`

Filed as [#73](https://github.com/gobha-me/termforge/issues/73) while writing
Epic 1's state-machine tests. `App::quit()` set a private `m_running` with no
reader, and `test_run_frames` re-arms it on entry, so a headless test could not
distinguish "Escape quit the app" from "Escape did nothing" — which is the single
most important regression in that epic. Our workaround was `Shell::quit_requested`,
a latched bool duplicating state the framework already had.

**Fixed in v0.1.14** (`App::running()`), and we are on it (gitea
[#17](https://git.gobha.me/xcaliber/term-game/issues/17)).

⚠ **It is not a drop-in, and the difference is silent.** `m_quit_requested` was
sticky; `running()` is not, because `test_run_frames` still sets `m_running =
true` on entry. So `running()` answers "did a quit happen during the *last* run",
not "has one ever happened". Six of the eight call sites inverted directly; two
asserted *after* a `step()` they needed for a state transition, and there the
substitution is vacuous — both moved above their step.

Demonstrated rather than argued, by mutating `request_to_menu()` so returning to
the menu also quits (the state still becomes `Selector`, so only the loop state
can see it): the assertion before the step goes **red**, the same assertion after
the step **passes**. `test/11selector`'s trap list carries this, because the two
early assertions look exactly like something to tidy up.

`test/13tick` needs no reordering and says why: its `run()` is one uninterrupted
`test_run_frames`, so nothing re-arms `m_running` between `quit()` and the check.

---

## CI, and why it was red for three epics

Gitea Actions at [.gitea/workflows/ci.yaml](.gitea/workflows/ci.yaml). Green as
of gitea [#10](https://git.gobha.me/xcaliber/term-game/issues/10) — before that
**every run since Epic 0 failed**, including the merge of PR #19.

**The cause was not in this repo.** Every job died in 2–3 seconds, including
`version-selftest`, which is nothing but a checkout and `cmake -P`. A
compiler-free job cannot fail that fast for a code reason. The runner image has
no CMake and no Clang — it is a Node/Docker image, which is all its other
consumer (HTML-Games, on the same runner) has ever needed from it. The
`toolchain` job diagnosed this correctly on every single run; nobody could read
what it said.

⚠ **Gitea 1.25.4 does not expose Actions logs to the API.**
`/api/v1/repos/{o}/{r}/actions/runs/{n}/jobs` returns an empty list *even for
runs that succeeded*, and every log endpoint 404s or 500s. From a dev container
the only signal is a job's **name** and whether it passed. That is why #10 was
split out of Epic 0 in the first place, and it is still true — plan on it if CI
ever goes red again. The technique that worked: a temporary matrix of one-line
jobs, each *named for the question it answers*, so the answers arrive as job
statuses. They were deleted once they had reported.

**What it runs now.** Every compiling job declares
`container: image: debian:trixie`, which clears the whole floor from base repos
in one `apt-get` — cmake 3.31, gcc 14, clang 19.1 — and ships util-linux, so
`script(1)` exists for `pty-restore`. The matrix is gcc/default, clang/default,
ASan, UBSan and TSan, all with `-DCMAKE_CXX_FLAGS=-Werror` and
`-DTERMGAME_WITH_AUDIO=OFF`.

Pinning the image *here* rather than asking for the runner image to be bumped is
deliberate: a bumped image fixes one repo on one host and silently un-fixes
itself the next time it is rebuilt. This is version-controlled and reviewable.

### Three lines in that file look like clutter and are load-bearing

| Line | What breaks without it |
|---|---|
| `nodejs` installed into a C++ build container | `actions/checkout` is a JavaScript action; the runner needs an interpreter **inside** the container to execute it |
| `git` installed **before** the checkout, not with the compilers | `actions/checkout` falls back to a tarball, leaving no `.git`; `git describe --tags` finds nothing and the build silently reports `0.0.0` |
| `git config --global --add safe.directory "$PWD"` | same `0.0.0` symptom from a third cause — checkout leaves the workspace owned by another uid and git refuses to read it |

All three fail *silently or confusingly*, which is why they are commented at
their sites as well as here.

### ⚠ The project name follows the checkout directory name

cpp-template derives `project()` from the directory name, so `PROGRAM_NAME`
follows wherever the checkout lands, and `test/00bootstrap` asserts on it. A
workspace named anything but `term-game` reddens that test with a message
(`"src" == "term-game"`) that points nowhere near the cause. Found by tripping
over it: the local container rehearsal of #10 checked out into `/src`. Gitea's
workspace is `.../xcaliber/term-game`, so it holds — but it is an assumption,
not a guarantee, and it was verified with a probe rather than assumed.

### There is no CI badge, deliberately

`git.gobha.me` does not serve badge routes. The URL Gitea's own API advertises
as this workflow's `badge_url` returns 404, as do `/badges/release.svg`,
`issues.svg` and `stars.svg` — and so does the equivalent URL for a sibling repo
whose workflow has been succeeding for months. It is an instance setting, not
something this repo can fix, so README links the Actions page in words instead.
Tracked in gitea [#21](https://git.gobha.me/xcaliber/term-game/issues/21).

Consequence worth noting: `check_artifacts` rule A1 is narrowed to
`^README\.md$` on the grounds that its label is "README CI badge" (see the
divergences table below). With no badge to protect, A1 is currently inert
either way; gitea [#12](https://git.gobha.me/xcaliber/term-game/issues/12) still
owns reverting the narrowing when upstream narrows it.

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
