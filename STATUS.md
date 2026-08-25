# STATUS — glyphcade

Live state. Update this when something lands; do not let it drift.

**Last updated: 2026-08-25** (TermForge repinned from v0.6.0 to v0.57.14; the
compact `Cell` API is adopted, the ASCII selector mark is now `*`, capability
replies are parsed as complete records, and Sokoban's #128 hit-test workaround
is retired. Before it #10 — Solitaire's
nineteen-card tableau has a
43x24 bottom-tier contract before the game exists; before it #12 — both
targetless package-config paths execute under `consumer-resolves`, and deleting
their guard is a measured red; before it #15 — the arcade is *installable*: CPack `.deb`,
`.rpm` and `.tar.gz`, built and inspected by a new CI `package` job and attached
to the release on a tag. Before it term-game#55 — Tetris's next-up preview is now
the spawn stream rather than a dead-end copy of it, the first defect here found
by playing on hardware; before it #42 + #15 — the geometry block: every game
declares the smallest terminal it wants *and what kind of ask that is*, and the
selector gets the suite's first ceiling; before it #46 — the exported package is
now *resolved* by a ctest rather than read as text, closing the one gap in the
install story; #45 — the options cycler joins the glyph tier, and the suite
renders above the ASCII tier for the first time; #44 — the pause dialog joins the
border tier; plus the first maintainer feel report, see
[Feel and the container](#feel-and-the-container))

---

## Where the project actually is

**The TermForge pin is v0.57.14, the latest published tag as of 2026-08-25.**
That is 110 tags and 288 commits past v0.6.0. The source-visible break is
v0.50.0's compact `Cell`: styled blanking now uses `Screen::clear(fg, bg)`, and
tests resolve graphemes through `Screen::text_at()` because text may live in
screen-owned spill storage. v0.11.1 deliberately changed the ASCII selector
mark from `>` to `*`, distinct from a right arrow; the selector and options
tests pin the new glyph at its exact column.

v0.57.14 also hardens the real terminal setup path without changing its public
API or emitted probe bytes: Kitty graphics APCs and DA1 replies are parsed as
complete records, so malformed or colliding substrings cannot select a stronger
driver tier. The documented bare and truecolour pty flows exercise that path.

The Release package arm exposed one test that still measured machine speed:
`test/15minesweeper-ui` spun 200 uncapped frames and assumed at least one 60 Hz
period had elapsed. An optimized CI run completed them sooner and correctly
delivered zero fixed ticks. Its `Probe` now owns a `SyntheticClock`; the case
advances 100 ms explicitly and still drives the production accumulator.

The first v0.21.0 tag run then exposed the same category one layer down in
`test/26snake-ui`: its nine-food score fixture advanced 150 ms even though the
board interval at that speed is 73 ms. That bought two moves, and a next food
randomly spawned directly ahead made the valid record 110 instead of the case's
expected 100. Repeating that case under UBSan also exposed rendering fixtures
that could move when enough real time elapsed between their load and draw. The
suite's `Probe` now owns a frozen `SyntheticClock`, and the score fixture
advances exactly one reported board interval.

The bump also closes the feedback loop from Epic 7. `MapWidget::tile_at()`
shipped in v0.6.1 (#128), so Sokoban no longer duplicates the widget's camera
and floored-viewport arithmetic. `set_map_size()` preservation followed in
v0.6.2 (#127), and the atlas-backed sprite tier shipped in v0.14.0 (#64). There
are again **no TermForge workarounds in this repository**.

**Solitaire's worst tableau now has a bottom-tier answer before the flagship is
being written.** #10. A pile can hold six hidden cards and a complete
thirteen-rank face-up build, but the six hidden identities are not information
the player may inspect. They render as one counted card-back strip; every
face-up card keeps its own row, and the last keeps its full 5x3 outline. The
nineteen-card worst case is therefore 16 tableau rows, and the whole table fits
exactly at **43x24**.

This is neither a visual cap nor an adaptive option: no playable information is
removed, and `tableau_card_at()` keeps each visible face-up strip addressable for
the future mouse path. The covered hidden strip has no hit; once exposed, the
full card back names the one card that may flip. `test/35solitaire-layout`
sweeps every valid hidden/face-up count and pins the width, height and hit-test
boundaries without a `Screen`. The game is deliberately not registered yet —
Epic 8 still owns its model and renderer. See "Solitaire's nineteen cards fit
without hiding one" in [docs/history.md](docs/history.md).

**The suite's fidelity contract is explicit.** Every roster entry now has a
documented floor, preferred experience and degradation story in
[`docs/fidelity.md`](docs/fidelity.md). Rendering profiles are names for output,
not a linear terminal grade: graphics, keyboard protocol, dimensions, mouse and
audio remain independent. In glyphcade only geometry may refuse entry; larger
Kitty- or half-block-first games belong in standalone repositories rather than
expanding this deliberately compact arcade.

**A package config without exported targets is rejected deliberately, and both
ways to produce one are now tested.** #12. `consumer-resolves` still proves a
normal scratch install resolves, then points the same generated consumer at the
build tree and at a real install configured with `glyphcade_BUILD_LIB=OFF`.
Both must fail with glyphcade's own "package config but no exported targets"
diagnosis, and neither may fall through to CMake's generic missing-include
error. Deleting the guard was red-verified: the build-tree arm reached the
missing `glyphcadeTargets.cmake` include and the test rejected that failure for
having the wrong reason. See "The targetless package configs are rejected on
purpose" in [docs/history.md](docs/history.md).

**There is a way to install this that is not "build it yourself."** #15. `cpack`
produces a `.deb`, an `.rpm` and a `.tar.gz`; a new CI `package` job builds them
against the whole test suite and *inspects* them before anything is uploaded; a
`v*` tag attaches them to the GitHub release.

The shape is the part worth knowing. The install tree is currently 113 files and
**one** of them is the game — the rest are static archives, headers, package
metadata and licence notices, which have no runtime role at all because a static
archive is consumed at link time and `bin/glyphcade` already contains that code.
So:

| artifact | carries |
|---|---|
| `.deb` / `.rpm` | the binary and its two licence notices. Three files. |
| `.tar.gz` | the whole install tree, every component, unfiltered |

⚠ **There is deliberately no `-dev`/`-devel` package.** Convention offers one and
the issue proposed one, but a `-dev` package is a promise to keep those archives
ABI-stable for third parties, and nothing here has made that promise. The tarball
says the same thing honestly: they are available, they are not a supported
interface. Revisit if somebody actually consumes the export.

⚠ **The audio dependency is derived, never written down** — `dpkg-shlibdeps`
from the binary's `DT_NEEDED` for the `.deb`, rpmbuild's `AUTOREQ` for the
`.rpm`. That is not fastidiousness: the same source produces `librtaudio6` on
Ubuntu 24.04 and **`librtaudio7`** on Debian trixie, measured, and a hardcoded
string would have been wrong on one of them. ⚠ And expect **five** names, not
the twenty `ldd` prints — `readelf -d` shows five `NEEDED` entries and
dpkg-shlibdeps reads those, not the transitive closure. jack, pulse, alsa and
dbus arrive through `librtaudio`'s own Depends. An assertion written against
`ldd` would be red on a correct package.

⚠ **`glyphcade`'s own `LICENSE.md` was never installed** until this landed, and
neither was a runtime-side copy of TermForge's. It never showed while the only
artifact was a build tree. TermForge is linked *statically*, so its code is
inside the binary and MIT's condition attaches to the binary's package — which
is why both notices are `COMPONENT runtime` and land in the `.deb`.

See [CI](#ci) for the job, and `cmake/check_package.cmake` for what "inspects"
means — the 0.0.0 refusal in there was demonstrated against a real `.git`-less
build, which produced `glyphcade_0.0.0-1_amd64.deb` without any other step
objecting.

**Tetris's next-up preview shows the pieces that are actually coming — for the
first time.** `term-game#55`,
the first defect in this repo found by *playing it on hardware* rather than by
reading it. The panel was not stale; it was **unrelated**. See "The preview was
a dead-end copy" below.

**The selector no longer disagrees with the games about size.** 
`term-game#42` and
`term-game#15`, designed and landed
together because they are the same struct field seen from two ends. A game now
declares the smallest terminal it wants in `GameMeta::geometry`, the menu says so
before you press Enter, and the selector's own body stops widening at 120 columns
and centres — which is what every game already did. See "What the geometry block
is" below.

⚠ **#15 took six deferrals and the last one was right.** The objection recorded
in `games/sokoban/layout.hpp` was not "not yet" but "the field is the wrong
shape": a bare `min_cols` would sit Minesweeper's 21x13 — arithmetic — next to
Sokoban's 34x12 — an opinion, because Sokoban has a camera and no level is ever
undrawable — and invite the selector to treat them as the same fact. So the kind
travels with the number: both floors say **"needed"**, while the `Playable` one
adds **"to play well"** to explain why its number is a judgement.

**Games now ask before they start.** 
`term-game#38` — pressing Enter on a
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
`term-game#13` is decided and
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

**Epic 4's follow-up has landed too: high scores persist** (
`term-game#14`). Both games keep a
record across quit-to-menu and across restarts — 2048 a best score, Minesweeper
a best time per difficulty — in a versioned text file under `$XDG_DATA_HOME`.
See "What the score store is" below.

**Before the current v0.57.14 pin, the termforge pin was v0.6.0** (
`term-game#36`) — six tags, not the
four the issue was written against, and the first bump that **fixed a bug we
were shipping** rather than merely staying current. See "What the v0.6.0 bump
brought" below. Before it the pin was **v0.2.2** (
`term-game#24`) — seven tags, not the
five that issue was written against. **Nothing upstream blocks any epic**:
#63 and #64 shipped and are taken, so Epics 7 and 8 are unblocked, and
#60 shipped in v0.2.2, so Epic 6's hold-to-move stops being OS auto-repeat
guesswork. One upstream behaviour change reached us at v0.2.2 and was decided
rather than inherited — the wheel. See "What the v0.2.2 bump brought" below.

**Epic 5 has landed. There are three games, and one of them moves on its own.**
Snake is registered, playable with arrows/hjkl/wasd, three difficulties, and
walls that either kill you or wrap you around — the last of which the reference
does not have at all. 
`term-game#6` is closed. See "What Epic
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

**The keyboard seam has landed.** 
`term-game#32` — `GameMeta` declares a
`termforge::KeyboardMode`, the Shell sets it per game entry and gives it back on
exit, and the Shell stopped acting on key *releases* it will start receiving the
moment any game asks for `Enhanced`. Landed on its own, before Tetris, for the
reason term-game#22 landed before Epic 4 and #24 before Epic 5. Nothing on the
roster asks for anything but `Legacy`, so all three games behave identically.
See "What the keyboard seam is" below — including the three mutations that went
green, two of which were claims written into a comment.

**Epic 7 has landed. There are five games, and one of them draws its board with
a widget instead of writing cells.** Sokoban is registered — twenty levels in
the standard Sokoban charset, push rules, unlimited undo, a move and a push
counter, per-level best scores, and deadlock detection the reference does not
have at all. 
`term-game#8` is closed. See "What Epic
7 built" below.

Every game already uses `Frame` for its chrome, but this is the first whose
PLAYFIELD is a termforge widget — `MapWidget` — rather than a loop writing cells
into the `Screen` itself. That was the point: term-game#8 made feedback into
termforge [term-game#64](https://github.com/gobha-me/termforge/issues/64) a deliverable of
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
move resets, T-spins and combos. 
`term-game#7` is closed. See "What Epic
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

**Next move: the art asset pipeline (#8), then Epic 8 (Solitaire)**, the flagship
and the last of the roster. Its bottom-tier layout prerequisite (#10) is now
done: 43x24 shows the nineteen-card worst case without hiding a face-up card or
scrolling. The remaining prerequisite is the sprite input, not the table.

We are now pinned to **v0.57.14**, so both `draw_image(Rect cells, …)` and
`MapWidget`'s atlas-backed sprite tier are available. Free-floating cards still
fit the direct `Image`/`draw_image` path better than a tile map; at v0.2.2 that
hand-written path was not viable because `draw_image` took an image's pixel
dimensions as a cell count. See "What the v0.6.0 bump brought" below for that
resolved history.

Since Epic 3, two housekeeping issues have landed.
`term-game#16` moved the pin to
termforge v0.1.10 and retired the `guarded_run` workaround — see the
exception-boundary section below for what survived it and why. 
`term-game#17` moved it again, to
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

Epic 7 added one back for a time: `Sokoban::handle_mouse` re-derived
`MapWidget`'s camera, tile size and floored viewport while waiting for
[#128](https://github.com/gobha-me/termforge/issues/128). v0.6.1 satisfied its
written deletion condition; the duplicated arithmetic is gone and the game now
delegates picking to `MapWidget::tile_at()`.

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
| 6 — Tetris | **done** | ~~termforge #60~~ — shipped in **v0.1.19…v0.2.2** and taken. `KeyboardMode::Enhanced` gives real `KeyAction::Repeat`/`Release`; DAS is now expressible rather than inferred from OS auto-repeat. `term-game#32` built the seam that reaches it: declare `Enhanced` in `kMeta` and the Shell does the rest. ⚠ Still degradable: a terminal without the kitty protocol never delivers `Release` — and note the notice is **ours**, not upstream's, because `App::setup()` has already run by the time a game entry sets the mode. Tetris must fall back to discrete steps **knowingly** |
| 7 — Sokoban | **done** | ~~termforge #64 → #63~~ — both shipped and **taken**. `MapWidget` v1 (glyph tier) is now SPENT: Sokoban is its first consumer, and the four pieces of API friction it found are listed in "What Epic 7 built" |
| 8 — Solitaire | **layout ready; art pipeline next** | #10 is done: the text table has a tested 43x24 contract. ~~termforge #63~~ shipped and is taken, so `Image` plus `draw_image` is reachable from game code. #8 still owns PNG decode and asset storage. `MapWidget`'s sprite tier also shipped in v0.14.0, but free-floating cards still fit the direct image path better than a tile widget |

**Nothing upstream blocks any epic.** That has been true since term-game#24, and it is what
[term-game#24](`term-game#24`) bought. termforge
[#27](https://github.com/gobha-me/termforge/issues/27) (install/export),
[#58](https://github.com/gobha-me/termforge/issues/58) (frame pacing),
[#59](https://github.com/gobha-me/termforge/issues/59) (`on_tick`) and
[#61](https://github.com/gobha-me/termforge/issues/61) (F5–F12) are all closed,
and we pin **v0.57.14** to get them, plus
[#71](https://github.com/gobha-me/termforge/issues/71) (terminal restore on the
exception path), [#72](https://github.com/gobha-me/termforge/issues/72)
(`ListWidget` marks its own selection) and
[#73](https://github.com/gobha-me/termforge/issues/73) (`App::running()`). cpp-template CT-15 was fixed upstream at
`8f62930`; the build-tree `export(EXPORT ...)` block no longer exists, so there
was nothing for us to delete. See
[docs/cpp-template-audit.md](docs/cpp-template-audit.md) for what those cost us.

---

## Upstream framework work (termforge, GitHub)

| Issue | Gap | State |
|---|---|---|
| [#27](https://github.com/gobha-me/termforge/issues/27) | CMake consumption (install/export, `PROJECT_IS_TOP_LEVEL` gating) | **closed — shipped in v0.1.7** |
| [#58](https://github.com/gobha-me/termforge/issues/58) | Frame pacing: idle loop capped ~7.5fps | **closed — fixed** |
| [#59](https://github.com/gobha-me/termforge/issues/59) | No `on_tick(dt)` hook | **closed — shipped in v0.1.8** |
| [#60](https://github.com/gobha-me/termforge/issues/60) | No key release (Kitty keyboard protocol) | **closed — shipped in v0.2.2, and we are pinned to it.** `KeyEvent` gained `action` (`Press`/`Repeat`/`Release`) and `App::set_keyboard_mode` picks the tier. Additive and opt-in: the default, `KeyboardMode::Legacy`, is byte-for-byte what every earlier tag emitted, so nothing calls it yet. **Epic 6 (Tetris) is what wants `Enhanced`**, and term-game#32 built the seam that gets there — and it must still degrade knowingly, because a terminal without the protocol never delivers `Release`. ⚠ The `ErrorEvent{Info}` upstream emits from `setup()` does **not** cover us: the mode is set at game entry, long after that call answered about `Legacy`, so the Shell raises its own |
| [#61](https://github.com/gobha-me/termforge/issues/61) | `Key` enum stops at F4 | **closed — shipped in v0.1.9** |
| [#62](https://github.com/gobha-me/termforge/issues/62) | `Cell` has no text attributes | **closed — shipped as `Attr` in v0.1.16, and we are pinned to it — but nothing uses it.** Still costs Minesweeper a column per cell (63 vs 33 for Hard). Spending it is its own issue; see "the payoff we did not spend" above, including the correction that `FallbackDriver` **does** emit `Reverse` |
| [#63](https://github.com/gobha-me/termforge/issues/63) | `Image` has no blit/alpha compositing | **closed — shipped in v0.1.18, and we are pinned to it.** Unblocks Epic 8 |
| [#127](https://github.com/gobha-me/termforge/issues/127) | `MapWidget::set_map_size()` wipes every layer while claiming to preserve the corner | **closed — fixed in v0.6.2.** The widget now preserves the overlapping corner; glyphcade's size-first/populate-second load order remains valid |
| [#128](https://github.com/gobha-me/termforge/issues/128) | `MapWidget` has no `tile_at()`, so hit-testing re-derives the widget's private viewport arithmetic | **closed — shipped in v0.6.1 and taken at v0.57.14.** `Sokoban::handle_mouse` delegates to `tile_at()` and the workaround is deleted |
| [#64](https://github.com/gobha-me/termforge/issues/64) | MapWidget (Epic 3.6) | **closed — glyph tier shipped in v0.1.19 and was spent by Epic 7; the persistent atlas-backed sprite tier shipped in v0.14.0.** Sokoban remains a glyph-only consumer until it has art |
| [#75](https://github.com/gobha-me/termforge/issues/75) | Mouse tracking mode hardcoded to `?1002h`; no `?1003h`, no way to disable | **closed — shipped as `Terminal::set_mouse_mode` in v0.1.15, and the pin has been past it since — but nothing calls it.** The default, `MouseMode::Drag`, is byte-for-byte what we already emitted, so taking the tag changed nothing. `MouseMode::Motion` is what Minesweeper wants for buttonless hover; deferred to [#4](https://github.com/gobha-me/glyphcade/issues/4) because it is a *feel* change and the dev container cannot verify feel |
| [#71](https://github.com/gobha-me/termforge/issues/71) | `App::run()` skips `teardown()` on a throw | **closed — shipped in v0.1.10, and we are on it** (`term-game#16`). The terminal-restore workaround is gone; our boundary survives as a diagnostic. `test/21exception` asserts the upstream guarantee via `test_winch_hooked()`, `pty-restore` asserts the escape bytes. |
| [#72](https://github.com/gobha-me/termforge/issues/72) | `ListWidget` selection invisible at the fallback tier | **closed — shipped in v0.1.11, and we are on it** (`term-game#17`). Our gutter marker is gone and the two columns went back to the list. The marker is `ListWidget`'s now, on by default and **inside `rect()`**, so a click on it selects — which the workaround could not do. `test/11selector` asserts the glyph in cell text at the ASCII tier, coverage the workaround never had. |
| [#73](https://github.com/gobha-me/termforge/issues/73) | No way to observe `quit()`; `test_run_frames` re-arms `m_running` | **closed — shipped in v0.1.14, and we are on it** (`term-game#17`). ⚠ `App::running()` is **not** a drop-in for the accessor it replaced: it is not sticky, and `test_run_frames` still re-arms `m_running` on entry. Assert it *before* the `step()` you needed for a state transition, or it is vacuous. |

Check state with `gh` rather than trusting this table if it looks stale.

### The pin is v0.57.14, and the version request is patch-level

`cmake/deps/termforge.cmake` asks `find_package(termforge 0.57.14 …)`, not
`0.57`. termforge's package version file is `SameMinorVersion`, so `0.57`
accepts *any* installed 0.57.x. The worked examples below are all 0.1.x because that is where
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
  all** on the fallback tier. Since term-game#17 the Shell does not draw one — it
  relies on `ListWidget` doing it, which 0.1.10's cannot. The suite stays green,
  because no test can see another package's glyphs.

The last two are why this matters more than it looks: three times now we have
depended on API introduced in a *patch* release, and twice missing it is
**silent**. A floor at minor granularity would not have caught any of the three.

⚠ From 0.1.11 this is also an **ABI** floor. That release added members to
`ListWidget`, and `Shell` holds one *by value* in `arcade/shell.hpp`, which we
install. A consumer resolving an older 0.2.x compiles our public header against
a different object layout than `glyphcade_lib` was built with — no link error,
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

**Fixed in v0.1.10**, and we are on it (term-game#16). `run()` now guards `setup()`
and delegates the loop to `run_loop()`, both `catch (...) { teardown(); throw; }`.

**What we kept, and why it diverges from #16's own instructions.** The issue said
delete the file. Upstream *rethrows* rather than converting to an exit code — on
the stated grounds that "an int has no room for it, and the library will not
decide that your exception was meaningless" — and points the consumer at exactly
this: "Catch it around `run()` if you want a diagnostic of your own." Deleting
outright would move a thrown frame from `1` + `glyphcade: fatal: <what>` to `134`
+ SIGABRT + silence. So the file survives as
[`run_or_report`](include/glyphcade/arcade/exception_boundary.hpp), stripped of
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

**Fixed in v0.1.11**, and we are on it (
`term-game#17`). Upstream says the
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
  read back through `Screen::text_at()` under the fallback driver, and the mark
  moves with the arrow keys.
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

**Fixed in v0.1.14** (`App::running()`), and we are on it (
`term-game#17`).

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

## CI

[.github/workflows/ci.yml](.github/workflows/ci.yml) — GitHub Actions, green on
every push and pull request. Eight jobs:

- **`toolchain`** — asserts cmake >= 3.28, g++ >= 13, clang++ >= 19, and fails
  loudly on the right line if the base image ever moves under us.
- **`build`** — five arms: gcc, clang, ASan, UBSan, TSan. All with `-Werror`
  and `-DGLYPHCADE_WITH_AUDIO=OFF`.
- **`package`** — builds the release artifacts, gated on the whole `build`
  matrix. The **only** job that builds `-DGLYPHCADE_WITH_AUDIO=ON`, and the
  split is by purpose: the five arms above exist to find bugs, this one exists
  to ship the binary people download. On a `v*` tag it attaches the artifacts to
  the GitHub release.
- **`version-selftest`** — no compiler needed; unit-tests the git-describe parser.

⚠ Because the two sets of arms disagree about audio on purpose, **every push
exercises both branches** of the audio assertion in `cmake/check_package.cmake`
— the OFF arms prove the dependency is absent when it should be, the package job
proves it is present. A negative check whose positive counterpart never runs is
the failure mode that rule exists to avoid.

**Every job runs in a pinned `debian:trixie` container**, not on the runner
image, and that is not tidiness. The floor is clang 19+ (termforge needs C++23,
and clang 18 cannot compile `std::expected` against libstdc++); GitHub's
`ubuntu-24.04` ships clang 18. Pinning the image in the workflow means a
runner-image refresh cannot silently move the toolchain.

### Three lines that look like clutter and are load-bearing

All three fail *silently or confusingly*, which is why they are commented at
their sites as well as here.

| Line | What breaks without it |
|---|---|
| `git` installed **before** the checkout | `actions/checkout` falls back to a tarball, leaving no `.git`; `git describe --tags` finds nothing and the build silently reports `0.0.0` |
| `fetch-depth: 0` | same `0.0.0`, second cause — a shallow clone has no tags |
| `git config --global --add safe.directory "$PWD"` | same `0.0.0`, third cause — checkout leaves the workspace owned by another uid and git refuses to read it |

### History worth keeping

CI was red for three epics before it first went green, and **the cause was never
in this repo**. Every job died in 2–3 seconds, including the compiler-free
`version-selftest` — and a job that is nothing but a checkout and `cmake -P`
cannot fail that fast for a code reason. The self-hosted runner image had no
CMake and no Clang. The `toolchain` job diagnosed it correctly on every single
run; nobody could read what it said, because that forge did not expose Actions
logs to its API at all.

The technique that finally worked, and would again on any forge with unreadable
logs: push a temporary matrix of one-line jobs, each **named for the question it
answers** (`probe: host has cmake`, `probe: container support`), and read the
answers off the job statuses. Nine facts in one cycle instead of N blind
guesses. ⚠ A `container:` probe must assert the image's *own* identity — a
runner that ignores `container:` silently runs on the host, passes, and reports
the opposite of the truth.

## Divergences from cpp-template

Three, all deliberate, all with a deletion condition. Do not "re-sync" these files
from the template without reading this.

| File | Divergence | Delete when |
|---|---|---|
| [CMakeLists.txt](CMakeLists.txt) | `project()` takes a **literal** name instead of `get_filename_component(... NAME)`. Upstream's derivation makes every target, every `-D<name>_*` option, the install prefix and `PROGRAM_NAME` a function of the checkout directory. FetchContent unpacks us into `_deps/<name>-src`, so a consumer's targets would be `<name>-src_lib` and `find_package` would resolve nothing; a renamed clone or fork reddens `test/00bootstrap` with a message naming the directory. Our own dependency hardcodes `project(termforge ...)` for this exact reason. Rule B6 keeps the two sides in agreement. | never — upstream is a template, this is a shipped library |
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
  1. `GLYPHCADE_WITH_AUDIO` **auto-detects to ON here.** The OFF arm — the
     configuration CI runs and the one this repo promises always works — has to
     be built explicitly (`-DGLYPHCADE_WITH_AUDIO=OFF`) or it rots unnoticed.
  2. Nothing audio can ever be *device*-verified from this container. Report
     audio work as "builds clean, offline sink tests pass, needs device
     verification" — never as verified.
- **Debian ships no `RtAudioConfig.cmake`**, only a `.pc` file, so pkg-config is
  the load-bearing detection path in `cmake/audio.cmake`. A `find_package(RtAudio
  CONFIG)`-only design would look right and never fire on any apt-based box.
- **Two repos, one forge.** glyphcade, termforge and cpp-template are all on
  GitHub and all use `gh`. The split is by subject, not by tool — framework
  gaps go to termforge. See [AGENTS.md](AGENTS.md).
- **The termforge FetchContent URL is HTTPS, not SSH.** There is no GitHub SSH
  key on the dev container or on CI runners.
