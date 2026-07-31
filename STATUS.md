# STATUS — term-game

Live state. Update this when something lands; do not let it drift.

**Last updated: 2026-07-31** (gitea #44 — the pause dialog joins the border
tier; plus the first maintainer feel report, see
[Feel and the container](#feel-and-the-container))

---

## Where the project actually is

**Games now ask before they start.** gitea
[#38](https://git.gobha.me/xcaliber/term-game/issues/38) — pressing Enter on a
game used to start it immediately on its first setting, with the options as a
row of text along the bottom of a game already running, where using one
*restarted the game you just started*. Four of the five games now show a
pre-start screen instead; 2048, which has no settings, goes straight in and is
byte-for-byte unchanged. See "What the options screen is" below.


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

⚠ **Nothing in this container can play a sound**, so what is verified *here* is
that it builds in six arms, that the offline path renders what it claims to, and
that the no-device path degrades correctly.

**A real device works.** Confirmed by the maintainer on desktop hardware
2026-07-31 — the clause that used to end this paragraph, "not that a real device
works at all", is retired. ⚠ **That is the only clause it retires**, and the
bound is the maintainer's own: roughly **ten seconds, on one game**. Whether the
bank *sounds good*, and whether each effect is the RIGHT sound across five
games, is untouched by it and still needs an ear. Do not read "audio works" as
"the bank is verified" — see [Feel and the container](#feel-and-the-container)
below for why that distinction keeps mattering.

**Epic 4 has landed. There are two games.** 2048 is registered, playable with
arrows/hjkl/wasd, one level of undo, and it is the first game with **motion** — a
designed tween rather than a ported one, because the HTML reference has no
working slide animation to port. See "What Epic 4 built" below.

That second registered game also turned on what it promised: four `size() > 1`
assertions in `test/11selector` went live for free, and the case they could never
cover — a click on a **non**-selected row, the only gesture that discriminates the
`State::Selector` guard in the mouse path — is now written.

**The tween's feel is confirmed.** "2048 feels very smooth" — the maintainer, on
desktop hardware, 2026-07-31. That closes the one question this game could not
answer for itself: 90 ms of slide and 70 ms of pop were **designed blind**,
because the HTML reference has no working slide animation to port, so there was
no prior art to check them against and nine releases of no way to judge them.
The constants in `anim.hpp` are now measured, not guessed. ⚠ Which means
changing them is now a **regression risk** rather than a free tweak — an ease
curve is still a feel decision, but the linear baseline it would be replacing
has been played and liked.

⚠ Still not verified: whether the board is pleasant to play, and whether Slide
and Merge sound like anything. Sound-intent is tested; sound *quality* is not.

**Epic 4's follow-up has landed too: high scores persist** (gitea
[#14](https://git.gobha.me/xcaliber/term-game/issues/14)). Both games keep a
record across quit-to-menu and across restarts — 2048 a best score, Minesweeper
a best time per difficulty — in a versioned text file under `$XDG_DATA_HOME`.
See "What the score store is" below.

**The termforge pin is now v0.6.0** (gitea
[#36](https://git.gobha.me/xcaliber/term-game/issues/36)) — six tags, not the
four the issue was written against, and the first bump that **fixed a bug we
were shipping** rather than merely staying current. See "What the v0.6.0 bump
brought" below. Before it the pin was **v0.2.2** (gitea
[#24](https://git.gobha.me/xcaliber/term-game/issues/24)) — seven tags, not the
five that issue was written against. **Nothing upstream blocks any epic**:
#63 and #64 shipped and are taken, so Epics 7 and 8 are unblocked, and
#60 shipped in v0.2.2, so Epic 6's hold-to-move stops being OS auto-repeat
guesswork. One upstream behaviour change reached us at v0.2.2 and was decided
rather than inherited — the wheel. See "What the v0.2.2 bump brought" below.

**Epic 5 has landed. There are three games, and one of them moves on its own.**
Snake is registered, playable with arrows/hjkl/wasd, three difficulties, and
walls that either kill you or wrap you around — the last of which the reference
does not have at all. gitea
[#6](https://git.gobha.me/xcaliber/term-game/issues/6) is closed. See "What Epic
5 built" below.

It is the first game that is *directly* broken by a wobbling frame rate, which is
why the issue called it the forcing function for termforge
[#58](https://github.com/gobha-me/termforge/issues/58). #58 is fixed and we are
pinned past it, and this is where that gets checked against a real artifact:
**91 head repaints in eight seconds with nothing pressed**, i.e. about 11.4 a
second, against the ~7.5 fps ceiling #58 imposed on an idle loop. Recipe below.

⚠ **That number is a CEILING TEST, not a frame-rate target, and it was measured
in the container** — which is slower than real hardware. It proves the idle loop
is no longer capped; it does not describe how the game runs on a desktop. A
lower figure measured here later is a fact about the container before it is a
fact about the code. See [Feel and the container](#feel-and-the-container).

⚠ What is verified is rules, geometry, rendering, the clock and sound-intent.
**Feel is not**: whether the speed curve is right, whether two-column cells are
pleasant to play on, and whether `Eat` sounds like anything all still need a
human.

**The keyboard seam has landed.** gitea
[#32](https://git.gobha.me/xcaliber/term-game/issues/32) — `GameMeta` declares a
`termforge::KeyboardMode`, the Shell sets it per game entry and gives it back on
exit, and the Shell stopped acting on key *releases* it will start receiving the
moment any game asks for `Enhanced`. Landed on its own, before Tetris, for the
reason gitea #22 landed before Epic 4 and #24 before Epic 5. Nothing on the
roster asks for anything but `Legacy`, so all three games behave identically.
See "What the keyboard seam is" below — including the three mutations that went
green, two of which were claims written into a comment.

**Epic 7 has landed. There are five games, and one of them draws its board with
a widget instead of writing cells.** Sokoban is registered — twenty levels in
the standard Sokoban charset, push rules, unlimited undo, a move and a push
counter, per-level best scores, and deadlock detection the reference does not
have at all. gitea
[#8](https://git.gobha.me/xcaliber/term-game/issues/8) is closed. See "What Epic
7 built" below.

Every game already uses `Frame` for its chrome, but this is the first whose
PLAYFIELD is a termforge widget — `MapWidget` — rather than a loop writing cells
into the `Screen` itself. That was the point: gitea #8 made feedback into
termforge [#64](https://github.com/gobha-me/termforge/issues/64) a deliverable of
this epic, and hand-drawing tiles would have validated nothing. `MapWidget`'s
glyph tier is now **spent**. Four pieces of API friction came out of it, each
commented at the site that pays for it in
[sokoban.cpp](src/lib/games/sokoban/sokoban.cpp).

⚠ What is verified is rules, geometry, rendering at both tiers, the parser, the
deadlock detector and sound-intent. **Feel is not**: whether pushing crates is
pleasant, whether two-column tiles read square, whether the camera lurch on a
level larger than the window is comfortable, and whether `Seat` sounds like
anything. Nothing in this container can judge any of them.

⚠ **And the camera — the thing this epic exists to validate — is not exercised
by the level pack.** The largest bundled level is 12x11 tiles, which fits any
normal terminal with room to spare, so in ordinary play the camera pins to 0,0
and never moves. It is reached only by a window smaller than the level, which is
what `test/32sokoban-ui` drives. Saying "we validated the coordinate model" on
the strength of twenty levels that never scroll would be untrue.

**Epic 6 has landed. There are four games, and one of them wants a keyboard
tier.** Tetris is registered, with full SRS rotation and both wall-kick tables,
a seven-bag randomiser, hold, a three-piece preview, a ghost, lock delay with
move resets, T-spins and combos. gitea
[#7](https://git.gobha.me/xcaliber/term-game/issues/7) is closed. See "What Epic
6 built" below.

⚠ What is verified is rules, geometry, rendering, five clocks and sound-intent.
**Feel is not**, and this game has more of it than any other: whether 170 ms DAS
and 50 ms ARR are right, whether a 40 ms soft drop is right (the reference has
no rate at all, so that number is ours and unheard-of), whether two-column cells
play well, and whether `Lock`, `Tetris` and `LevelUp` sound like anything.

⚠ **And the arm that matters most is the one nothing here can reach.** Tetris is
the first game to declare `KeyboardMode::Enhanced`, and this container's terminal
has no kitty keyboard protocol — so every headless case, every pty capture and
every CI run exercises the **degraded** path. The held path is covered on the
model, where `HoldSupport` is a parameter, and nowhere else.

### Feel and the container

**Every performance figure in this file was measured in the dev container, and
the container is slower than the hardware this runs on.** The maintainer's
report, 2026-07-31: the games feel *fast* on a desktop, and a good part of what
reads as sluggishness here is the environment rather than the code.

⚠ **So a container measurement is a FLOOR, not a representative number**, and the
trap is reading one as a regression. The clearest example is already in this
file: Snake's **91 head repaints in eight seconds** with nothing pressed, the
artifact that judged termforge #58. That number is evidence the idle loop is not
capped at ~7.5 fps — which is all it was ever claimed to be. It is **not** a
frame-rate target, and a future session that measures a lower one here has
learned something about the container first and about the code second.

⚠ **No timing constant has been changed on the strength of this**, deliberately.
"It feels fast on the desktop" was given as an observation and explicitly not as
direction, and it is recorded that way. The tick rate, the DAS/ARR pair, the soft
drop, the tween — all untouched.

**What this does and does not license.** Feel is judgeable *only* on baremetal,
so a "feel" caveat elsewhere in this file is retired by the maintainer playing
the thing, and by nothing else — not by a test, not by a pty capture, not by a
timing measurement taken here. Two have been retired that way so far: 2048's
tween, and that a real audio device works at all. ⚠ **Retire them one at a time
and at the strength given.** "Audio worked" after ten seconds on one game is not
"the bank is verified", and the temptation to round the second up from the first
is exactly the error this section exists to prevent.

⚠ Still open, and not spoken to: Snake's speed curve, Minesweeper's click
latency and cursor responsiveness, Tetris' DAS/ARR/soft-drop trio, Sokoban's
push feel, and whether any effect in the bank sounds *right*. Do not infer these
from "it feels fast" — a frame rate is not a feel decision.

**Next move: Epic 8 (Solitaire)**, the flagship and the last of the roster — but
read the upstream note below before starting it. ⚠ We are now pinned to
**v0.6.0** (gitea #36), so the `draw_image(Rect cells, …)` contract is available
— but `MapWidget`'s **sprite tier still does not exist at any tag**. Its two
gates (#83/#84) lifted and the design doc was updated to say so, but no code
followed. Epic 8's premise — cards as pixel sprites — needs that tier or an
`Image`-and-`draw_image` path written by hand. What the bump changed is that the
hand-written path is now viable: at v0.2.2 `draw_image` took an image's *pixel*
dims as a *cell* count, so it could not have worked. See "What the v0.6.0 bump
brought" below.

Since Epic 3, two housekeeping issues have landed.
gitea [#16](https://git.gobha.me/xcaliber/term-game/issues/16) moved the pin to
termforge v0.1.10 and retired the `guarded_run` workaround — see the
exception-boundary section below for what survived it and why. gitea
[#17](https://git.gobha.me/xcaliber/term-game/issues/17) moved it again, to
**v0.1.15**, and retired **both remaining workarounds**: the selector's gutter
marker and `Shell::quit_requested()`.

That left **no workarounds in this repo** for three releases. Three termforge
issues were filed from building it —
[#71](https://github.com/gobha-me/termforge/issues/71),
[#72](https://github.com/gobha-me/termforge/issues/72) and
[#73](https://github.com/gobha-me/termforge/issues/73) — all three were fixed
upstream, and all three of our stopgaps went, each on the deletion condition it
was written with. That loop closing is the thesis of running the two projects
together, so it is worth stating once rather than leaving implied.

⚠ **Epic 7 adds one back, and it is counted rather than quietly absorbed.**
`Sokoban::handle_mouse` re-derives `MapWidget`'s camera, tile size and floored
viewport extent in app code, because the widget has no `tile_at(cell_x, cell_y)`
and its `viewport_tiles()` is private. There is no way to hit-test a tile map
without it. **Deletion condition: termforge
[#128](https://github.com/gobha-me/termforge/issues/128)** ships a tile-picking
accessor. Commented at the site, and item 2 of the feedback below. Saying "we
have no workarounds" while carrying one would be the kind of claim this file
exists to prevent.

---

## What the options screen is (gitea #38)

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
- **The game draws its own screen**, via `OptionsScreen` in `term-game_core`, in
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


## What the v0.6.0 bump brought (gitea #36)

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
| v0.3.0 | #83/#84 — `draw_image(Rect cells, …)`, `preferred_pixel_extent`, `draw_pixels` returns a borrowed `const Image*` and takes an `Extent` | **not today, but it is the point.** Breaking only for a `TerminalDriver` implementor or a `draw_pixels` override, and we are neither. ⚠ It is what **unblocks gitea #39**: at v0.2.2 `draw_image` used an image's *pixel* dims as a *cell* count, so an atlas rendered as one flat colour per cell |
| v0.4.0 | #69 — `Widget::on_tick(dt)`; `ProgressBar`/`Button` animation became **wall-clock** | ⚠ **yes** — see below. The issue says "only if we hold one", and we hold two without naming either |
| v0.5.0 | #122 — `Widget::reset_transient()` at a Dialog showing boundary | ⚠ **yes**, and it is v0.4.0's cure |
| v0.5.1 | #123 — container overloads for `route_mouse`/`tick_widgets`, `route_mouse` skips nulls | no. Our one call passes a braced list of one non-null pointer; the `initializer_list` overload still wins |
| v0.5.2 | #102 — `Screen::fill_rect` clips via `Rect::intersect` instead of `x + w` in `int` | no. Our three call sites pass small in-bounds values, so the arithmetic is identical for every input we produce |
| v0.6.0 | #22 — `TabBar`, and `MarkGlyphs` grew `arrow_left`/`arrow_right` (`all()` 9 → 11) | no. We read `.selector` **by name**, never `all()`, never an aggregate initialiser. ⚠ The options cycler hardcodes `<`/`>`, which is what the new fields are for — correct at the ASCII tier, wrong above it. gitea [#45](https://git.gobha.me/xcaliber/term-game/issues/45) |

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

⚠ **gitea #36's table is wrong on both rows** — it calls v0.4.0 conditional on
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

⚠ **`find_dependency(termforge 0.6.0)` is exercised by nothing in `ctest`.**
`cmake/check_export.cmake` installs to a scratch prefix and greps the generated
targets file; it never calls `find_package(term-game)`. Checked by hand here — a
throwaway consumer against the install tree resolves term-game 0.13.0 and
termforge 0.6.0 — and that gap is gitea
[#46](https://git.gobha.me/xcaliber/term-game/issues/46).

### The pause dialog was the one widget the tier never reached — fixed

gitea [#44](https://git.gobha.me/xcaliber/term-game/issues/44), landed on its
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
[#48](https://git.gobha.me/xcaliber/term-game/issues/48); it would unlock the
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
destroys that. Same reasoning that made gitea #24 its own issue — and it became
gitea [#36](https://git.gobha.me/xcaliber/term-game/issues/36), which also
carried the `Widget::on_tick` audit the bump needed.

⚠ **The `MapWidget` sprite tier still does not exist at any tag.** Its gates
(#83/#84) landed and `docs/map-widget.md` was updated to record that they had —
but no implementation followed. **Epic 8 (Solitaire) is written against cards as
pixel sprites**, so the first thing that epic must do is decide between waiting
on upstream, driving `Image`/`draw_image` directly from game code, or shipping
the glyph tier first. That is a real planning input and it is not visible from
the issue.

---

## Blocking state

| Epic | State | Blocked by |
|---|---|---|
| 0 — Bootstrap | **done** | — |
| 1 — Arcade shell | **done** | — |
| 2 — Audio engine | **done** | — |
| 3 — Minesweeper | **done** | — |
| 4 — 2048 | **done** | — |
| 5 — Snake | **done** | — |
| 6 — Tetris | **done** | ~~termforge #60~~ — shipped in **v0.1.19…v0.2.2** and taken. `KeyboardMode::Enhanced` gives real `KeyAction::Repeat`/`Release`; DAS is now expressible rather than inferred from OS auto-repeat. gitea **#32** built the seam that reaches it: declare `Enhanced` in `kMeta` and the Shell does the rest. ⚠ Still degradable: a terminal without the kitty protocol never delivers `Release` — and note the notice is **ours**, not upstream's, because `App::setup()` has already run by the time a game entry sets the mode. Tetris must fall back to discrete steps **knowingly** |
| 7 — Sokoban | **done** | ~~termforge #64 → #63~~ — both shipped and **taken**. `MapWidget` v1 (glyph tier) is now SPENT: Sokoban is its first consumer, and the four pieces of API friction it found are listed in "What Epic 7 built" |
| 8 — Solitaire | **ready, with a caveat** | ~~termforge #63~~ — `Image` sub-rect blit and alpha shipped at v0.1.18 and are taken. ⚠ But `MapWidget`'s **sprite tier does not exist at any tag through v0.6.0**, and the cards-as-sprites premise wants it. Not a block — `Image` plus `draw_image` is reachable from game code — but it is a design decision this epic must make first, not inherit |

**Nothing upstream blocks any epic.** That has been true since gitea #24, and it is what gitea
[#24](https://git.gobha.me/xcaliber/term-game/issues/24) bought. termforge
[#27](https://github.com/gobha-me/termforge/issues/27) (install/export),
[#58](https://github.com/gobha-me/termforge/issues/58) (frame pacing),
[#59](https://github.com/gobha-me/termforge/issues/59) (`on_tick`) and
[#61](https://github.com/gobha-me/termforge/issues/61) (F5–F12) are all closed,
and we pin **v0.6.0** to get them, plus
[#71](https://github.com/gobha-me/termforge/issues/71) (terminal restore on the
exception path), [#72](https://github.com/gobha-me/termforge/issues/72)
(`ListWidget` marks its own selection) and
[#73](https://github.com/gobha-me/termforge/issues/73) (`App::running()`). cpp-template CT-15 was fixed upstream at
`8f62930`; the build-tree `export(EXPORT ...)` block no longer exists, so there
was nothing for us to delete. See
[docs/cpp-template-audit.md](docs/cpp-template-audit.md) for what those cost us.

---

## What Epic 7 built (gitea #8)

Sokoban, in six headers and three TUs — Tetris' five-header shape plus a
level-pack header, and one more TU than any other game:

- **[`level.hpp`](include/termgame/games/sokoban/level.hpp)** + **`level.cpp`** —
  the standard Sokoban charset (`#` `@` `$` `.` `*` `+` and space) and a `parse()`
  that is total: it returns a `ParseError`, never throws, and refuses seven kinds
  of malformed level. **Its own header and its own TU** because the format is
  separable from the rules — a level is validated once, at load, and nothing
  looks at a character again — and because it is the only part of this game a
  future level-FILE loader would reuse unchanged.
- **[`levels.hpp`](include/termgame/games/sokoban/levels.hpp)** — the twenty maps,
  verbatim from the reference. Its own header for the same reason
  `tetris/pieces.hpp` is: it is the part taken verbatim, and that boundary is
  worth seeing in a file list. **The pars are not verbatim** — see below.
- **[`board.hpp`](include/termgame/games/sokoban/board.hpp)** + **`board.cpp`** —
  push rules, undo, counters, win detection, deadlock detection. No termforge
  header, so `test/31sokoban` *cannot* reach a `Screen`.
- **[`layout.hpp`](include/termgame/games/sokoban/layout.hpp)** — a viewport rect
  for a camera, not a coordinate per cell. **34x12 needed.**
- **[`glyphs.hpp`](include/termgame/games/sokoban/glyphs.hpp)** — two tiers, three
  `static_assert`s.
- **[`sokoban.hpp`](include/termgame/games/sokoban/sokoban.hpp)** — the `Game`,
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
blocker; all four are worth having before the API freezes, which is what gitea #8
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

gitea [#15](https://git.gobha.me/xcaliber/term-game/issues/15) is therefore
deferred a **fifth** time, and for a new reason rather than the same one. A
`GameMeta::min_cols` would sit Minesweeper's 63 — derivable — next to Sokoban's
34 — an opinion — and invite the selector to treat them as the same kind of fact.
The four earlier deferrals said "not yet"; this one says the field is the wrong
shape.

### Audio: one new id, the fewest of any game

gitea #8 asks for "step, push, crate-on-goal, level complete" and gets **one**
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
  what gitea #8's "the corpus loads directly" actually asks for, and `parse()` is
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

## What Epic 6 built (gitea #7)

Tetris, in five headers and two TUs — Snake's shape, plus one:

- **[`pieces.hpp`](include/termgame/games/tetris/pieces.hpp)** — the seven
  tetrominoes' four rotations and both SRS kick tables. **Its own header because
  it is the one part of the reference taken verbatim**, and that boundary is
  worth seeing in a file list. Two `static_assert`s: every state has exactly
  four cells, and nothing is set outside a piece's own bounding box (kick
  offsets are relative to that box, so a stray cell moves every kick).
- **[`board.hpp`](include/termgame/games/tetris/board.hpp)** — the rules and all
  five clocks. No termforge header, so `test/27tetris` *cannot* reach a Screen.
- **[`layout.hpp`](include/termgame/games/tetris/layout.hpp)** — 10x20 cells at
  two columns each, plus a 12-column panel. **35x24 needed.**
- **[`glyphs.hpp`](include/termgame/games/tetris/glyphs.hpp)** — two tiers, three
  `static_assert`s.
- **[`tetris.hpp`](include/termgame/games/tetris/tetris.hpp)** — the `Game`, and
  the only file that knows `Screen`, `Event` or `GameContext` exist.

### 24 rows: the first game that does not fit in twenty

Minesweeper Hard is 63x20, 2048 is 29x19, Snake is 58x20. A Tetris well is
twenty cells tall *before* any chrome, so this one needs **24** — the classic
80x24 exactly, with nothing to spare, which is also the UI probes' default size.
Any chrome beyond the status and hint rows and it stops fitting the terminal
most people still have.

gitea [#15](https://git.gobha.me/xcaliber/term-game/issues/15) (a minimum size
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

## What the keyboard seam is (gitea #32)

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

gitea [#24](https://git.gobha.me/xcaliber/term-game/issues/24). Landed on its own,
before Epic 5, for the reason gitea #22 landed before Epic 4: a dependency bump
carrying a breaking change, bundled with a new game, makes a red CI run ambiguous
between the two.

The issue was written when upstream was at v0.2.0. It was at **v0.2.2** by the
time this was implemented, and the two extra tags were not filler — v0.2.2 is
termforge #60, which this file listed as Epic 6's blocker. That is the third time
this project has found the blocking picture staler than the issue describing it,
after Epic 1's already-shipped tick accumulator and gitea #16's "delete the file"
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
diverging from the framework deliberately — the exact workaround shape gitea #16
and #17 spent two issues deleting, and "there are no workarounds left in this
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
entries (Epic 6)**, or when a test-only substitute for the `term-game_roster`
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
and two cursor cases in `test/15minesweeper-ui`, and it interacts with gitea
[#15](https://git.gobha.me/xcaliber/term-game/issues/15) (`GameMeta` minimum
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

- **CMake scaffold** from cpp-template, project name `term-game` (follows the
  directory name, as does the gitea repo).
- **termforge consumed** via [cmake/deps/termforge.cmake](cmake/deps/termforge.cmake)
  — `find_package(termforge ... CONFIG)` first, FetchContent as the fallback.
  Epic 0 pinned v0.1.7; the pin is now **v0.6.0** — see below.
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

## What Epic 5 built (gitea #6)

Snake, in four pieces — minesweeper's shape exactly, and 2048's minus the tween:

- **[`board.hpp`](include/termgame/games/snake/board.hpp)** — the rules AND the
  step clock. Snake body as a deque with an incrementally-maintained occupancy
  grid, a two-deep turn queue, food, both wall modes, the speed curve. It
  includes **no termforge header**, which is what makes `test/25snake` *unable*
  to construct a `Screen`.
- **[`layout.hpp`](include/termgame/games/snake/layout.hpp)** — 28x16 cells at
  **two columns each**, **58x20 needed**. No `cell_at`: Snake takes no mouse
  input, so nothing hit-tests.
- **[`glyphs.hpp`](include/termgame/games/snake/glyphs.hpp)** — two tiers and
  three `static_assert`s (7-bit, exactly `kCellCols` wide, pairwise distinct).
- **[`snake.hpp`](include/termgame/games/snake/snake.hpp)** — the `Game`, and the
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
| `snake.js:14` keeps **one** queued direction, judged against the last *applied* one | a two-deep queue, each turn judged against the **previous queued** direction | ⚠ This is the bug gitea #6 names. A fast double-tap loses its first turn. And Right→Up→Left judges Left against a stale Right — the reference survives that only because the other bug then discards the Up, so one bug masks the other |
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

gitea #6 asks for "wrap-vs-wall as a mode" and the reference has no wrap at all.
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
| A minimum terminal size in `GameMeta` | gitea [#15](https://git.gobha.me/xcaliber/term-game/issues/15), the same answer Epics 3 and 4 gave: the game ships its own too-small screen (needs 58x20; the Shell's floor is 20x8) |
| A **tuned** speed curve | The reference's numbers, ported exactly, as named constants in `board.hpp`. Whoever can play it has one place to change them |
| `best_length` | Affine in `best_score` — see above. Revisit if a rule ever makes them independent (a bonus food worth more than one segment would) |

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
| [#60](https://github.com/gobha-me/termforge/issues/60) | No key release (Kitty keyboard protocol) | **closed — shipped in v0.2.2, and we are pinned to it.** `KeyEvent` gained `action` (`Press`/`Repeat`/`Release`) and `App::set_keyboard_mode` picks the tier. Additive and opt-in: the default, `KeyboardMode::Legacy`, is byte-for-byte what every earlier tag emitted, so nothing calls it yet. **Epic 6 (Tetris) is what wants `Enhanced`**, and gitea #32 built the seam that gets there — and it must still degrade knowingly, because a terminal without the protocol never delivers `Release`. ⚠ The `ErrorEvent{Info}` upstream emits from `setup()` does **not** cover us: the mode is set at game entry, long after that call answered about `Legacy`, so the Shell raises its own |
| [#61](https://github.com/gobha-me/termforge/issues/61) | `Key` enum stops at F4 | **closed — shipped in v0.1.9** |
| [#62](https://github.com/gobha-me/termforge/issues/62) | `Cell` has no text attributes | **closed — shipped as `Attr` in v0.1.16, and we are pinned to it — but nothing uses it.** Still costs Minesweeper a column per cell (63 vs 33 for Hard). Spending it is its own issue; see "the payoff we did not spend" above, including the correction that `FallbackDriver` **does** emit `Reverse` |
| [#63](https://github.com/gobha-me/termforge/issues/63) | `Image` has no blit/alpha compositing | **closed — shipped in v0.1.18, and we are pinned to it.** Unblocks Epic 8 |
| [#127](https://github.com/gobha-me/termforge/issues/127) | `MapWidget::set_map_size()` wipes every layer while claiming to preserve the corner | **open** — filed from Epic 7. Not a blocker: size first, populate second |
| [#128](https://github.com/gobha-me/termforge/issues/128) | `MapWidget` has no `tile_at()`, so hit-testing re-derives the widget's private viewport arithmetic | **open** — filed from Epic 7, and the **deletion condition for this repo's one workaround** |
| [#64](https://github.com/gobha-me/termforge/issues/64) | MapWidget (Epic 3.6) | **shipped as `MapWidget` v1 (glyph tier) in v0.1.19, and SPENT by Epic 7** — Sokoban is its first consumer and produced four pieces of API feedback (see "What MapWidget's first consumer found"), reported upstream as a comment on #64 plus #127 and #128. ⚠ Still open upstream against the sprite tier, which **does not exist at any tag through v0.6.0** even though both its gates landed. We did not need it; **Epic 8 might** |
| [#75](https://github.com/gobha-me/termforge/issues/75) | Mouse tracking mode hardcoded to `?1002h`; no `?1003h`, no way to disable | **closed — shipped as `Terminal::set_mouse_mode` in v0.1.15, and the pin has been past it since — but nothing calls it.** The default, `MouseMode::Drag`, is byte-for-byte what we already emitted, so taking the tag changed nothing. `MouseMode::Motion` is what Minesweeper wants for buttonless hover; deferred to gitea [#18](https://git.gobha.me/xcaliber/term-game/issues/18) because it is a *feel* change and the dev container cannot verify feel |
| [#71](https://github.com/gobha-me/termforge/issues/71) | `App::run()` skips `teardown()` on a throw | **closed — shipped in v0.1.10, and we are on it** (gitea [#16](https://git.gobha.me/xcaliber/term-game/issues/16)). The terminal-restore workaround is gone; our boundary survives as a diagnostic. `test/21exception` asserts the upstream guarantee via `test_winch_hooked()`, `pty-restore` asserts the escape bytes. |
| [#72](https://github.com/gobha-me/termforge/issues/72) | `ListWidget` selection invisible at the fallback tier | **closed — shipped in v0.1.11, and we are on it** (gitea [#17](https://git.gobha.me/xcaliber/term-game/issues/17)). Our gutter marker is gone and the two columns went back to the list. The marker is `ListWidget`'s now, on by default and **inside `rect()`**, so a click on it selects — which the workaround could not do. `test/11selector` asserts the glyph in cell text at the ASCII tier, coverage the workaround never had. |
| [#73](https://github.com/gobha-me/termforge/issues/73) | No way to observe `quit()`; `test_run_frames` re-arms `m_running` | **closed — shipped in v0.1.14, and we are on it** (gitea [#17](https://git.gobha.me/xcaliber/term-game/issues/17)). ⚠ `App::running()` is **not** a drop-in for the accessor it replaced: it is not sticky, and `test_run_frames` still re-arms `m_running` on entry. Assert it *before* the `step()` you needed for a state transition, or it is vacuous. |

Check state with `gh` rather than trusting this table if it looks stale.

### The pin is v0.6.0, and the version request is patch-level

`cmake/deps/termforge.cmake` asks `find_package(termforge 0.6.0 …)`, not `0.6`.
termforge's package version file is `SameMinorVersion`, so `0.6` accepts *any*
installed 0.6.x. The worked examples below are all 0.1.x because that is where
the lesson was learned; every one of them survives the move, because the
dependency still ships load-bearing API in patch releases — v0.2.1, v0.2.2 and
v0.5.2 all did. Three ways minor granularity bit:

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
install. A consumer resolving an older 0.2.x compiles our public header against
a different object layout than `term-game_lib` was built with — no link error,
just disagreement about a size. Anything held by value in an installed header
turns the build floor into an ABI floor.

**v0.2.1 did it again**, adding the scrollbar's track and thumb colours to the
same class. Two size changes in two minor versions: treat "`ListWidget` grew a
member" as the base rate, not as bad luck.

⚠ **Crossing 0.1 → 0.2 makes `SameMinorVersion` cut the other way.** Asking for
`0.2.2` no longer accepts *any* 0.1.x — which is what we want, since the wheel
semantics differ — but by the same rule a request still saying `0.1.15` silently
stops matching a 0.2.x install. That is why the floor moved in one commit.

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
