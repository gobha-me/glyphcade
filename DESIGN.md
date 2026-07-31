# term-game — Design

A TUI arcade suite in C++23. **TermForge** renders it, **RtAudio** sounds it.

Sibling to [HTML-Games](https://git.gobha.me/xcaliber/HTML-Games) — same
arcade-of-small-games idea, same roster to raid, but TUI and C++ instead of
HTML/CSS/JS. Where HTML-Games is a browser launcher over static directories,
term-game is a single binary hosting compiled-in games.

---

## Why a suite and not one big game

TermForge has three known gaps that block real-time play (see *Framework
dependencies* below). A single long-form game would be blocked on all three at
once, with nothing shippable until they land. A suite lets each game be an
independent vertical slice:

- **Early games need zero framework changes** — a turn-based, mouse-driven game
  runs on TermForge exactly as it exists today.
- **Each game dogfoods one subsystem**, so gaps are found by a real consumer
  rather than hypothesized. Every issue filed so far came out of this exercise.
- **Later games escalate deliberately** into the parts that need framework work,
  so a blocked game blocks only itself.

The suite is also the honest test of TermForge's actual pitch — that a
stdlib-only C++23 framework can do notcurses-class inline graphics. One game
proves a code path; six games prove an API.

---

## Architecture

### One App, many Games

Exactly one `termforge::App` exists — the **Shell**. It owns the terminal, the
event loop, the audio engine, and the selector UI. Games are *not* `App`
subclasses; only one thing can own a terminal.

```cpp
class Game {
 public:
  virtual ~Game() = default;

  // Identity for the selector: slug, title, description, tag, icon.
  [[nodiscard]] virtual auto meta() const -> const GameMeta& = 0;

  // Entering the game. Fresh state every time; the Shell owns the lifetime.
  virtual auto start(GameContext&) -> void = 0;

  // Advance simulation by dt. Fixed timestep — see Time below.
  virtual auto tick(Duration dt) -> void {}

  // Input. Return true if consumed; the Shell handles anything declined
  // (so every game gets pause/quit-to-menu for free).
  virtual auto on_event(const termforge::Event&) -> bool = 0;

  // Draw. Immediate mode, same contract as termforge Widgets.
  virtual auto draw(termforge::Screen&) -> void = 0;

  // True when the game wants to hand control back to the selector.
  [[nodiscard]] virtual auto done() const -> bool { return false; }
};
```

`GameContext` is the game's only channel to shared services. Games never touch
the `App`, the `Terminal`, or each other.

What it carries: the probed `capabilities()`, the `border_style()` the Shell
chose from them, `quit_to_menu()`, `audio()` and `scores()`. The last two were
**seams, not omissions** — declared here before they existed and filled later,
each additively, without a line changing in any existing game.

Both deferrals had conditions, and both conditions earned their keep. Audio
waited for an engine, so its handle was not an API invented before anything
consumed it. High-score persistence waited for the **second** scoring game,
because a format chosen before there is anything to persist is a format that
gets migrated — and when 2048 arrived it wanted a best *score* while Minesweeper
wanted a best *time per difficulty*, so a record is a keyed value with a
**direction**. Had it shipped with the first scoring game it would have been one
integer, and wrong.

There are now no reserved seams left. A sixth service is a new design question,
not a promise already made.

`quit_to_menu()` sets a flag; it never calls back. Returning to the menu
destroys the `Game`, so a synchronous callback would destroy a game that called
it from inside its own `tick()` while that frame was still live. The Shell polls
the flag once per frame, after every game entry point has unwound.

### The Shell's state machine

Three states — `Selector`, `InGame`, `Paused` — and two rules worth stating
because both are load-bearing and neither is visible in a diff.

**Escape means different things, so the Shell handles every event itself.**
`termforge::App::on_event`'s default quits on Escape. In the selector that is
right; inside a game Escape must mean "back to the menu", and there is no way to
express that while still chaining to the base — it would already have quit. So
`Shell::on_event` **never calls `App::on_event`**: it is total, and it handles
Ctrl+C explicitly because it is no longer inheriting that either. Every TermForge
example ends its `on_event` with `App::on_event(ev);`, which makes this an easy
line to "restore"; `test/11selector` is what goes red when someone does.

**A running game owns the whole `Screen`.** The Shell draws no chrome over it, so
`draw()` coordinates and `MouseEvent` coordinates are the same coordinates, with
no offset arithmetic to get wrong. Pause is an overlay, which TermForge dims and
composites over the game's own frame — the game visibly freezes behind it.

Entry constructs a **fresh instance from the registry's factory** and exit
destroys it. Freshness is therefore structural rather than a discipline each
game must re-implement in `start()`: the alternative fails silently, per-game,
and only on the *second* entry — the case nobody tests. Because `GameEntry`
carries a copy of the metadata, no game object exists at all while the menu is
up.

**Games are self-contained**, per the HTML-Games convention that has already
proven out: one directory, one static library target, no cross-game includes. A
game may use the shared services in `include/termgame/`, but never another
game's headers.

**Shipped** — deferred through Epic 1–3, landed ahead of Epic 4 as its own
change, because a build restructure bundled with a new game makes a red CI run
ambiguous between the two. `src/lib` is four static libraries:

```
term-game_lib      arcade/shell.cpp, arcade/exception_boundary.cpp
  ↓                the ALIAS, the export, the only spelling outside src/lib
term-game_roster   arcade/all_games.cpp — the registry table
  ↓
term-game_game_<name>   one per game
  ↓
term-game_core     build_info.cpp, audio/*.cpp — what a game MAY call
```

**Which file sits in which target is the design, and it was measured rather than
chosen.** `shell.cpp` is the only TU that references `all_games()`, so it must be
scanned *before* the roster; `minesweeper.cpp` calls `audio::Engine::play`, so
audio must be scanned *after* the games. The intuitive shape — core as
everything-but-games — closes a cycle `core → roster → game → core` that a
single-pass linker cannot resolve. It fails unconditionally, in every link
declaration order, because CMake emits the topological order and `game → core`
pins core last; so the fix is always to move a *file*, never to reorder a link
line. The rule: **if a game may call it, it belongs in core; if it drives the
roster or owns the terminal, it belongs in `term-game_lib`.**

Two guarantees fall out of the layering rather than being maintained by hand. A
game **cannot** reach the Shell — core sits below the games, so `termgame::Shell`
is not on a game's link line, which makes "games never touch `App`, `Terminal`,
or each other" a link error. And every target must join the export set, because
`install(EXPORT)` refuses to name a target it cannot resolve — so adding a game
and forgetting to list it stops *generation* rather than shipping a package with
a game missing.

⚠ One thing this deliberately did **not** do, contrary to what this section used
to promise: `src/lib/games/<name>/` and `include/termgame/games/<name>/` did not
collapse into a top-level `games/<name>/`. `cmake/install.cmake` ships public
headers with a single `install(DIRECTORY include/ …)`, so a header that leaves
`include/` leaves the package; `.clangd` and every `#include <termgame/games/…>`
in the tree assume that root too. The CMake target is what buys the isolation.
The directory move would buy nothing and cost a tree-wide rewrite.

⚠ And `games/<name>/` is the game's **namespace**, which is not always its slug —
a slug is a user-visible id and may begin with a digit, which a namespace may
not.

### Registration is explicit, not self-registering

The tempting design is a self-registering static in each game's `.cpp`:

```cpp
static const auto reg = register_game<Minesweeper>();  // DON'T
```

**This silently fails when games are static libraries.** The linker drops object
files that nothing references, taking the registrar with them, and the game just
never appears in the menu — with no error at any stage. The workarounds
(`--whole-archive`, `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`, OBJECT libraries) are
all platform- or generator-sensitive.

Instead: one `src/lib/arcade/all_games.cpp` naming every game explicitly. Adding
a game means editing one file. Boring, greppable, and it fails at *compile* time
rather than at runtime with an empty menu.

### Time

The Shell owns a monotonic clock and runs a **fixed-timestep accumulator**: it
measures the real frame delta, clamps it (so a debugger breakpoint or a laptop
suspend cannot deliver one enormous delta and tunnel objects through walls), and
calls `Game::tick()` an integer number of times with a constant `dt`.

Fixed timestep is not a preference — it is what makes game logic deterministic,
which is what makes it testable without a terminal. A game's simulation must be
drivable by feeding it N ticks and asserting state, with no rendering involved.

**None of that machinery is ours.** TermForge #59 landed in v0.1.8, and the
accumulator, the delta clamp and the per-frame tick bound all live in
`App::tick_step`. The Shell's entire contribution is `set_tick_hz(60)` in its
constructor, leaving `max_tick_dt` at the framework's 250 ms default — which
bounds a frame at `ceil(0.25 × 60) = 15` game ticks however long that frame took.
`Shell::on_tick` then forwards `dt` to the running game verbatim.

An earlier version of this section described hand-rolling the accumulator inside
`on_render` as a workaround pending #59. **Do not reintroduce it.** A game must
not read a clock either; `dt` is the only time that exists inside `Game::tick`.

### Audio

The audio callback is a realtime thread. The rules are absolute: **no locks, no
allocation, no syscalls, no I/O** in the callback. The design falls out of that.

```
UI thread                    lock-free SPSC ring                 audio thread
  play(Sfx::Explode)  ──────────► [command queue] ──────────►  drain, trigger
                                                                 voice, mix
                                                              ┌──────────────┐
                                                              │  AudioSink   │
                                                              └──────────────┘
                                                    RtAudioSink │ NullSink │ WavFileSink
```

- **`AudioSink`** abstracts the device. Three implementations:
  - `RtAudioSink` — the real thing. Compiled only when `TERMGAME_WITH_AUDIO=ON`.
  - `NullSink` — consumes and discards. Lets the whole repo build and run on a
    box with no sound hardware.
  - `WavFileSink` — renders the mix to disk. This is the interesting one: it
    makes the entire audio path **deterministically testable offline**, which is
    the same offline-testability discipline TermForge applies to its drivers.
- **SFX are synthesized, not sampled.** Square/triangle/saw/noise oscillators
  plus a linear ADSR, with each effect a small declarative struct. This removes
  the WAV decoder, the asset pipeline, and the binary blobs in git — and it is
  the correct sound for an arcade. Venice is better spent on sprite and title
  art.

**What Epic 2 decided, and why** — four things that are not obvious from the
diagram, all with their full argument at the implementation site:

- **The ring drops the NEWEST command, not the oldest**, and gitea #3 was
  amended to say so. Drop-oldest cannot be done inside the SPSC contract: it
  means the producer advancing the consumer's index, which turns a lock-free
  queue into a CAS problem and puts a retry loop on the audio thread. See
  `include/termgame/audio/ring.hpp`.
- **Integer phase, integer sweeps, linear envelopes — no `sin`, no `exp`.** Not
  for speed: glibc's transcendentals are not correctly-rounded and move between
  versions, and `-ffp-contract` defaults differ between compilers. The same
  argument `board.hpp` makes for hand-rolling splitmix64. It works — the output
  is byte-identical across GCC -O0/-O2/-O3 and Clang -O2.
- **Headroom is arithmetic, not a limiter.** Eight voices capped at 1/8 FS each
  cannot clip, and that is a `static_assert` rather than something you have to
  hear to trust. The honest cost is that one sound peaks at −18 dBFS.
- **rtaudio is linked by exactly one never-exported target** (gitea #13). See
  the note at the bottom of `cmake/audio.cmake`, and the `audio-export-clean`
  ctest that keeps it true.

The mixer and voice pool live in `audio/engine.hpp` and the SFX bank in
`audio/synth.hpp`, rather than in headers of their own.

**Development reality:** the dev container has `librtaudio-dev` installed but no
`/dev/snd` and no PulseAudio — the *library* is present, the *hardware* is not.
So `TERMGAME_WITH_AUDIO` auto-detects to **ON** there, and the no-audio arm has
to be built explicitly (`-DTERMGAME_WITH_AUDIO=OFF`, which is what CI runs) or it
rots unnoticed. Either way nothing can be heard from that container, so CI tests
the mixer through `WavFileSink` and real-device verification happens on the
maintainer's own machine. The abstraction is therefore not speculative — it is
load-bearing from the first commit.

### Graphics tiers

term-game inherits TermForge's degradation contract rather than inventing one:
capability tier picks the glyph family (`BorderStyle::Ascii` on a bare TTY) and
decides sprite-vs-glyph tiles. Degradation surfaces as an `ErrorEvent`, never a
silent downgrade.

Every game must be **playable at the bottom tier**. Pixel sprites are an
enhancement over a glyph fallback that always exists — the same relationship
`Widget::draw_pixels` has to `Widget::draw`.

**Every widget that draws chrome takes its `BorderStyle` from
`GameContext::border_style()`, and the push happens at the moment of use** —
per frame for the selector's four widgets, at the push site for an overlay.
Not once at construction, and not once at probe time. Two reasons, and the
second is the one that keeps being relearned:

- A widget built in a constructor **cannot know the tier**; the constructor has
  no terminal to ask. A one-shot at probe time is no better, because it can
  never reach a widget built after the first frame.
- **Styling the widgets you name does not style the widgets they own.**
  Upstream's defaults are `BorderStyle::Single` and the light scrollbar glyphs —
  neither of which this application ever *chooses*, since the tier resolves only
  to `Ascii` or `Rounded`. So an unstyled sub-widget is not merely inconsistent,
  it is drawing from a family nothing here selected. This has now cost two
  releases twice: the detail pane's scrollbar (Epic 6) and the pause dialog's
  frame and title delimiters (gitea #44).

Venice-generated PNG art is decoded via a vendored `stb_image` in term-game.
TermForge's stdlib-only policy is *its* constraint and correctly keeps decode out
of `Image`; term-game has no such policy and is the right place for it.

---

## Roster

Ordered so that framework risk increases monotonically. Games 1–2 ship against
TermForge as it exists today.

| # | Game | Dogfoods | Blocked on |
|---|------|----------|-----------|
| 1 | **Shell + selector** | ListWidget, Frame, dialogs, theme, FocusRing | — |
| 2 | **Minesweeper** | mouse L/R routing, grid rendering | — |
| 3 | **2048** ✓ | tween animation, renderer diff | #59 (shipped) |
| 4 | **Snake** ✓ | real-time tick | #58, #59 (both shipped) |
| 5 | **Tetris** ✓ | tick + held-key feel | #58, #59, #60 (all shipped) |
| 6 | **Sokoban** ✓ | tile maps, camera, layers | ~~#64~~ (glyph tier shipped v0.1.19, taken) |
| 7 | **Solitaire** *(flagship)* | Kitty sprites, venice art, mouse drag | #63 |

Reference implementations for all seven exist in HTML-Games — the game *logic*
is solved, so each port is a rendering and feel exercise rather than a design
one. Later candidates from that roster: Breakout, Space Invaders, Pong, Typing,
Oregon Trail.

---

## Framework dependencies

Filed against [gobha-me/termforge](https://github.com/gobha-me/termforge) from
this design pass.

> **This table is point-in-time — it records what was true when the design was
> written.** [STATUS.md](STATUS.md) is the live state; check it (or `gh`) before
> assuming anything here still blocks. #27 and #58 have both been fixed
> upstream, and #59 is in progress.

| Issue | Gap | Blocks |
|---|---|---|
| [#27](https://github.com/gobha-me/termforge/issues/27) | No install/export; consumer options default ON | ~~Epic 0~~ — **fixed in v0.1.7** |
| [#58](https://github.com/gobha-me/termforge/issues/58) | Idle loop capped ~7.5fps; `set_frame_ms` can't raise it | ~~all real-time games~~ — **fixed** |
| [#59](https://github.com/gobha-me/termforge/issues/59) | No `on_tick(dt)` hook | ~~animation~~ — **fixed in v0.1.8** |
| [#60](https://github.com/gobha-me/termforge/issues/60) | No key release (Kitty keyboard protocol) | Tetris/Snake feel |
| [#61](https://github.com/gobha-me/termforge/issues/61) | `Key` enum stops at F4 | ~~UI polish~~ — **fixed in v0.1.9** |
| [#62](https://github.com/gobha-me/termforge/issues/62) | `Cell` has no text attributes | UI polish, low tiers |
| [#63](https://github.com/gobha-me/termforge/issues/63) | `Image` has no blit/alpha compositing | sprite games |
| [#64](https://github.com/gobha-me/termforge/issues/64) | MapWidget (Epic 3.6) | ~~Sokoban~~ — **glyph tier fixed in v0.1.19 and spent by Epic 7.** Still open upstream against the SPRITE tier, which does not exist at any tag through v0.5.1 |

Of these, #27, #58, #59 and #61 were the first done, and term-game pins a tag far
enough forward to get them (plus #71, #72 and #73 — see below). #62/#63 degrade
rather than block, which is why the design never waited on them.

⚠ This table is **point-in-time**, as the rest of this document is: every gap in
it has since been closed upstream, and the pin has moved five more times. Read
[STATUS.md](STATUS.md) for which tag we are actually on and what each one bought
— not this paragraph, which is kept for the reasoning rather than the numbers.

Three gaps this design pass had not predicted have since been added to the list,
all found by building against the framework rather than reasoning about it —
which is the feedback loop this repo exists for:

- **`App::run()` did not restore the terminal when a frame throws** (Epic 0) —
  filed as #71, **fixed in v0.1.10**. The workaround is retired; what remains is
  an ordinary diagnostic boundary, because upstream rethrows rather than
  converting to an exit code. See STATUS.md.
- **`ListWidget`'s selection was invisible at the bottom tier** (Epic 1) — it
  had no colour setters, and the fallback driver discards the theme inversion
  that was its only selection affordance. Filed as #72, **fixed in v0.1.11**:
  the widget now marks its own selection with a glyph, in a gutter it reserves
  itself. The selector's hand-drawn marker is retired.
- **`App` had no way to observe `quit()`** (Epic 1) — filed as #73, **fixed in
  v0.1.14** (`App::running()`). `Shell::quit_requested()` is retired.

**All three are closed, and every workaround written against them is gone**, each
on the deletion condition it shipped with. That is the design-level claim this
project exists to make, so it is worth stating rather than leaving implied: the
gaps were found by consumption, filed upstream rather than papered over locally,
and the stopgaps had exit conditions written the day they were added.

---

## Repository layout

**This is the destination, not a checklist to stub out.** Epic 0 built only what
it needed; a header that does not exist yet is better than an empty one, because
an empty `include/termgame/audio/` makes the audio engine look done. `✓` marks
what exists today.

```
term-game/
├── CMakeLists.txt            # ✓ from cpp-template
├── DESIGN.md  AGENTS.md  README.md  STATUS.md         # ✓
├── include/termgame/
│   ├── build_info.hpp        # ✓ version + build_has_audio()
│   ├── arcade/   exception_boundary.hpp ✓                      (Epic 0)
│   │              game.hpp ✓  game_meta.hpp ✓  context.hpp ✓
│   │              registry.hpp ✓  shell.hpp ✓         (Epic 1)
│   │              scores.hpp                   (deferred — see GameContext)
│   ├── games/<name>/ ✓       # a game's own headers — they stay HERE, not next
│   │                         #   to its sources, so install(DIRECTORY include/)
│   │                         #   still ships them
│   └── audio/    sink.hpp ✓ engine.hpp ✓ synth.hpp ✓ ring.hpp ✓
│                                                      (Epic 2)
├── src/audio_backend/        # ✓ rtaudio, in a NEVER-exported target (#13)
├── src/lib/                  # ✓ four targets: lib / roster / game_* / core
├── src/lib/games/<name>/ ✓   # a game's sources AND its own CMakeLists.txt —
│                             #   one static library per game. <name> is the
│                             #   namespace, not necessarily the slug.
├── src/bin/main.cpp          # ✓ the single binary
├── assets/                   # venice-generated art
├── vendor/stb_image.h
└── test/                     # ✓ Catch2, mirroring termforge's suite layout
```

Bootstrapped from [cpp-template](https://github.com/gobha-me/cpp-template) per
its `NEW_PROJECT.md` checklist. Project name follows the directory name, so the
directory, the gitea repo, and the CMake project must all read `term-game` —
while the include dir and namespace read `termgame`, because a hyphen is illegal
in a C++ namespace.

---

## Testing

Inherited from TermForge's philosophy: **the interesting things are testable
without a terminal.**

- **Game logic** — drive N fixed ticks, assert state. No `Screen`, no TTY.
- **Rendering** — draw into an offscreen `Screen`, assert cells.
- **Audio** — render through `WavFileSink`, assert samples. A compact numeric
  fingerprint per SFX (not a golden file — see AGENTS.md), a bit-exact
  self-determinism check, and a mixer test that N simultaneous voices neither
  clip nor drift.
- **The ring buffer** — TSan-clean producer/consumer test. This is the one piece
  where a bug is a heisenbug, so it gets the most adversarial treatment.

CI runs GCC + Clang, ASan/UBSan, and `TERMGAME_WITH_AUDIO=OFF` (no sound
hardware on runners) — mirroring termforge's matrix.
