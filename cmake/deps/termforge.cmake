# TermForge — the TUI framework glyphcade is built on (gobha-me/termforge).
#
# This is the "linked into the library" case from the annotated recipe in
# cmake/deps/catch2.cmake, which is three things and not one:
#
#   1. termforge_INSTALL tracks ${PROJECT_NAME}_INSTALL (below) — NOT a fixed
#      OFF. A fixed OFF leaves termforge_lib in no install export set, and our
#      own install(EXPORT) then fails during *generation*, before a single
#      translation unit is compiled.
#   2. find_dependency(termforge) in cmake/project-config.cmake.in.
#      ⚠ This is now the second reason a target can be missing from an export
#      set, and both produce the same generate-time error. If it names one of
#      OUR targets rather than termforge_lib, the cause is src/lib's game list,
#      not this file — see cmake/install.cmake.
#   3. cmake/install.cmake names every target in src/lib's chain in the export
#      set. It used to say "nothing at all — it stays verbatim", which stopped
#      being true when src/lib became four targets: install(EXPORT) will not
#      write a Targets file that references a target it cannot resolve.
#
# No FetchContent SOURCE_DIR pin needed here, unlike a cpp-template-derived
# dependency: termforge hardcodes project(termforge ...) rather than deriving it
# from the directory name, precisely so _deps/termforge-src cannot rename it.
#
# HTTPS, not SSH: there is no GitHub SSH key on the dev container or on CI
# runners, and an SSH URL here would fail in exactly the environments that most
# need the fallback to work.

# 0.6.0, not 0.6 — and the patch level here is load-bearing, not pedantry.
#
# termforge's package version file is SameMinorVersion: it accepts a candidate
# whose major.minor match AND whose version is >= the one requested. Asking for
# "0.2" therefore accepts *any* installed 0.2.x. The history below is 0.1.x
# because that is where the floor was learned, and every lesson in it survives
# the move to 0.2 — the dependency still ships load-bearing API in patch
# releases. Three ways minor granularity bit, in increasing order of how hard
# they are to notice:
#
#   - 0.1.7 has no App::set_tick_hz. The Shell calls it, so that acceptance
#     turns a clean "your installed package is too old, falling back to
#     FetchContent" into a wall of compiler errors in shell.cpp, on whichever
#     machine happens to have a stale system copy and nowhere else. Loud, and
#     it happens at build time.
#   - 0.1.9 compiles clean and links clean, and hands you an App whose run()
#     does NOT restore the terminal when a frame throws (#71). Nothing fails.
#     The only symptom is a wedged terminal, on one developer's machine, on the
#     day something happens to throw. There is no build-time signal at all.
#   - 0.1.10 compiles clean, links clean, runs clean — and draws the selector's
#     selection marker TWICE. Since term-game#17 the Shell no longer draws its own
#     (ListWidget marks its own selection from 0.1.11, in a gutter it reserves
#     on every row), so a stale 0.1.10 reserves nothing and paints nothing, and
#     what you get is a menu with no visible selection at all on the fallback
#     tier. The suite stays green: no test can see another package's glyphs.
#
# So: third time we depend on API introduced in a *patch* release, and the
# second time missing it is silent. A floor has to be expressed at the
# granularity the dependency actually moves at, or it is not a floor — and a
# floor at minor granularity would not have caught any of the three.
#
# ⚠ From 0.1.11 this is also an ABI floor, not only an API one. That release
# added members to ListWidget (a style, a marker-enabled flag and a std::string
# marker), so sizeof(ListWidget) changed — and Shell holds one BY VALUE in
# include/glyphcade/arcade/shell.hpp, which we install. A consumer that resolves
# an older 0.2.x compiles our public header against a different object layout
# than glyphcade_lib was built with. That is not a link error; it is a silent
# one. cmake/project-config.cmake.in carries the same floor for that reason.
#
# v0.2.1 did it again — ListWidget gained m_track_fg and m_thumb_fg for #21's
# scrollbar — so this is now the SECOND time that class changed size under us.
# Treat "ListWidget grew a member" as the expected case, not the surprise.
#
# ⚠ v0.4.0 is the THIRD time, and it is the one that would not have been found
# by looking at the widgets we name. Button lost a bool and gained two
# std::chrono::duration<double>, so sizeof(Button) grew — and we do not hold a
# Button. We hold ConfirmDialog m_pause, which holds two of them, so
# sizeof(Shell) changed through a class our source never mentions. Grepping for
# the widget types in shell.hpp is NOT sufficient to audit this floor; the
# transitive members count, and they are only visible in upstream's headers.
#
# ⚠ And v0.4.0/v0.5.0 changed the vtable by INSERTION, not append. Widget's new
# on_tick and reset_transient land at slots 3 and 4, ahead of pixel_regions,
# draw_pixels, hit_test, hit_test_tree, set_focused and focusable — six slots
# shift. A mixed-version consumer does not get a missing symbol it could
# diagnose; it gets hit_test dispatched into draw_pixels. "Two virtuals were
# added" reads as benign and is not.
#
# ⚠ Crossing a minor makes SameMinorVersion cut the other way, and it is worth
# saying out loud because it is the reason all three files move in ONE commit:
# asking for 0.6.0 no longer accepts any 0.2.x at all (which is what we want —
# the ABI differs, see above), but by the same rule anything still asking for
# 0.2.2 silently stops matching a 0.6.x install. The three places are
# cmake/deps/termforge.cmake (here), cmake/project-config.cmake.in, and
# STATUS.md. Two of them are consumer-visible; a half-done bump is a package
# that resolves on the developer's machine and nowhere else. This bump crosses
# four minors at once (0.2 → 0.6), so the rule applies four times over.
find_package(termforge 0.6.0 QUIET CONFIG)

if (termforge_FOUND)
  message(STATUS "termforge: ${termforge_VERSION} via find_package")
else ()
  if (NOT TERMFORGE_URI)
    set(TERMFORGE_URI https://github.com/gobha-me/termforge.git)
  endif ()

  # Why this tag, in the order the floor was raised:
  #   v0.1.7 — first tag with install/export (#27); everything before is
  #            unconsumable, which is the floor Epic 0 shipped against.
  #   v0.1.8 — on_tick(dt), set_tick_hz(n), set_max_tick_dt(dt) (#59). This is
  #            the one we cannot do without: the Shell configures termforge's
  #            fixed-timestep accumulator and implements none of it. DESIGN.md
  #            used to describe hand-rolling one; that paragraph is gone.
  #   v0.1.9 — Key::F5–F12 (#61). Not used yet; taken because it is the tip and
  #            splitting the pin across two tags buys nothing.
  #  v0.1.10 — App::run() restores the terminal on the exception path (#71). It
  #            wraps setup() and the loop in catch(...) { teardown(); throw; },
  #            so teardown happens before the exception leaves run() and no
  #            longer depends on the App being destroyed by unwinding. Also
  #            Terminal::leave_raw(), and the App::test_run_guarded /
  #            test_winch_hooked probes — which is what lets test/21exception
  #            assert the guarantee rather than take the release note's word
  #            for it. This is the tag that retired run_or_report's
  #            terminal-restore job; term-game#16.
  #  v0.1.11 — ListWidget marks its own selection (#72): set_style, set_marker,
  #            set_marker_enabled, gutter_cols, and the colour setters that
  #            never existed. The marker is a glyph in a two-column gutter the
  #            widget reserves on every row, on by default, and — unlike the
  #            workaround it replaced — INSIDE rect(), so a click on it selects.
  #            This is the tag that retired the selector's hand-drawn "> ";
  #            term-game#17. Also the tag that changed sizeof(ListWidget); see the
  #            ABI note above.
  #  v0.1.12 — the same fix for Select/MenuBar dropdowns, and
  #  v0.1.13 — for TableWidget (#76). We use neither; taken because they are on
  #            the way to v0.1.14 and splitting the pin buys nothing.
  #  v0.1.14 — App::running() (#73). This is what retired Shell::quit_requested,
  #            which existed only because App::m_running was unreadable;
  #            #17. ⚠ It is not the same observable: test_run_frames re-arms
  #            m_running on entry, so running() answers "did a quit happen
  #            during the last run", not "ever". test/11selector's trap list
  #            says what that costs a test that asserts in the wrong order.
  #  v0.1.15 — Terminal::set_mouse_mode (#75). Not used yet; taken because it is
  #            the tip. Its default, MouseMode::Drag, is byte-for-byte the
  #            ?1006h?1002h we were already emitting, so taking the tag changes
  #            nothing until something calls it. MouseMode::Motion is what
  #            Minesweeper wants for buttonless hover — #4.
  #  v0.1.16 — Cell::attrs (#62): bold/dim/italic/underline/reverse/strike as a
  #            bitmask on every cell. The one tag here with a payoff we can
  #            spend: Minesweeper's cursor is a PAIR OF BRACKETS costing a
  #            column per cell (Hard needs 63), because colour does not survive
  #            FallbackDriver. Attr::Reverse does — the fallback driver emits
  #            Reverse and Bold and drops only the other four — so the brackets
  #            may be replaceable at BOTH tiers, not just the colour one. That
  #            is a board-geometry rewrite with its own tests, so it is its own
  #            issue, not this bump's cargo.
  #  v0.1.17 — dropdown scroll for Select/MenuBar (#85). We use neither. Inert.
  #  v0.1.18 — Image sub-rect blit, alpha compositing, sprite-sheet slicing
  #            (#63). Unblocks Epic 8 (Solitaire) and the back half of Epic 7.
  #            Also moved Rect out of widgets/widget.hpp into core/types.hpp —
  #            a header reshuffle, not a break; termforge::Rect still resolves.
  #  v0.1.19 — MapWidget v1, glyph tier (#64/#86). Unblocks Epic 7 (Sokoban).
  #            Its TileSet requires a glyph, so the degradation contract is
  #            type-enforced rather than documented — which is this repo's
  #            bottom-tier rule expressed in someone else's type system.
  #  v0.2.0 — ⚠ THE BREAKING ONE (#35). Two halves, and only the undeclared
  #            half reaches us. The DECLARED break is TableWidget's arrow keys
  #            moving the selection instead of scrolling; we do not use
  #            TableWidget, so it misses entirely. What reaches us is that the
  #            WHEEL NOW SCROLLS THE VIEW EVERYWHERE. ListWidget::on_event used
  #            to answer a wheel event with set_selected(selected ± 3); it now
  #            moves a view offset and leaves the selection alone. Our selector
  #            inherited the old behaviour and never asked for it. We adopted
  #            upstream's convention rather than rebuilding the old one here —
  #            see the comment at the mouse branch in src/lib/arcade/shell.cpp,
  #            and the two cases in test/11selector that hold us to it.
  #  v0.2.1 — shared scrollbar for List/Table/TextBox (#21). The selector paints
  #            one the moment the roster outgrows its pane, which it does not
  #            yet at two entries. Note the strip is keyed off BorderStyle, so
  #            it is |/# at the ASCII tier and │/█ above it: m_list.set_style()
  #            in draw_selector is what keeps the bottom tier 7-bit, and that is
  #            now a SECOND reason that line is load-bearing.
  #  v0.2.2 — kitty keyboard protocol: KeyAction::{Press,Repeat,Release} and
  #            KeyboardMode (#60). Additive and opt-in — the default, Legacy, is
  #            byte-for-byte what every earlier tag emitted, so taking this
  #            changes nothing until something calls set_keyboard_mode. Taken
  #            because it is the tip, and because it is the upstream issue
  #            STATUS.md lists as Epic 6's (Tetris) blocker: hold-to-move stops
  #            being OS auto-repeat guesswork.
  #
  #  ── term-game#36 moves the pin v0.2.2 → v0.6.0. Six tags, not the four the
  #     issue describes; it was written when upstream was at v0.5.1. ──
  #
  #   v0.3.0 — draw_image(Rect cells, …), preferred_pixel_extent(), draw_pixels
  #            returns a borrowed const Image* and takes an Extent, and a new
  #            struct Extent separates pixels from cells (#83/#84). Breaking
  #            ONLY for a TerminalDriver implementor or a draw_pixels override.
  #            We are neither — nothing in src/, include/ or test/ derives from
  #            a termforge type except Shell : App — so it misses entirely.
  #            ⚠ It is also the tag #8 (art pipeline) is blocked on: at
  #            v0.2.2 draw_image was handed an Image's PIXEL dims and used them
  #            as a CELL count, so an atlas rendered as one flat colour per
  #            cell. Generating art before this tag was generating art we could
  #            not draw. That is what this bump buys, and it is the only thing
  #            it buys.
  #
  #  v0.4.0 + v0.5.0 — ⚠ READ THESE AS ONE. Apart, one deepens a bug we already
  #            ship and the other looks like an unrelated feature. Together they
  #            FIX it, which is why the pin crosses both at once.
  #            The bug first: activating a ConfirmDialog's button arms a press
  #            flash and closes the dialog in the SAME dispatch, so the flash
  #            never renders in the showing that armed it. What clears it
  #            afterwards is what moved across these tags — measured, not read
  #            off the release notes:
  #
  #              pin      1st paint of the next showing   2nd paint
  #              v0.2.2   LIT                             clear
  #              v0.4.0   LIT                             LIT
  #              v0.6.0   clear                           clear
  #
  #            ⚠ So v0.4.0 did NOT introduce this. At v0.2.2 — the pin we are
  #            moving off — Button::draw() cleared the flag AFTER painting with
  #            it, so every re-opening of the pause dialog shows one frame of a
  #            wrongly-lit Resume button. Confirmed in a pty: the pressed
  #            background 48;2;128;64;255 appears once on the old binary and
  #            zero times on the new one, same keystrokes, same controls.
  #            v0.4.0 (#69) made the flash a WALL-CLOCK countdown in
  #            Widget::on_tick. App keeps no widget registry and Shell::on_tick
  #            forwards only to m_game, so nothing ticks m_pause and the
  #            countdown never runs: one frame becomes permanent, for the life
  #            of the process. No compile error, no failing test.
  #            v0.5.0 (#122) added Widget::reset_transient(), which Dialog::draw
  #            calls at its per-showing boundary BEFORE anything paints, and
  #            Button implements by zeroing the flash. That cures both.
  #            ⚠ So term-game#36's table is wrong on BOTH rows — it says v0.4.0
  #            bites only "if we ever hold" a ProgressBar or Button, and that
  #            v0.5.0 does not reach us. And the audit it prescribes — grep for
  #            those two type names — comes back CLEAN, because the Buttons are
  #            inside ConfirmDialog and our source never names them. See the ABI
  #            note above: the same blind spot, twice. This is also the one
  #            place the bump is not merely inert: it fixes a live defect.
  #            ⚠ We do NOT forward ticks to m_pause. See the comment in
  #            src/lib/arcade/shell.cpp at the on_tick pause gate for why, and
  #            test/11selector for the case that pins the behaviour we are
  #            relying on instead of relying on upstream's doc comment.
  #
  #   v0.5.1 — container overloads for route_mouse/tick_widgets, and route_mouse
  #            now skips null entries (#123). Our one call site,
  #            route_mouse(*mouse, {&m_list}) in shell.cpp, passes a braced list
  #            of one non-null pointer: the initializer_list overload still
  #            exists and still wins, so this is additive and inert here.
  #   v0.5.2 — Screen::fill_rect clips through Rect::intersect instead of
  #            computing x + w in int, which wrapped for a rect starting near
  #            INT_MAX and silently dropped a rect that covered the screen
  #            (#102). Our three call sites (twenty48.cpp, snake.cpp) pass small
  #            in-bounds values, so the new arithmetic is identical for every
  #            input we produce. A latent-overflow fix we cannot reach.
  #   v0.6.0 — TabBar (#22), which we do not use, and MarkGlyphs grew
  #            arrow_left/arrow_right (‹ ›) so all()'s extent went 9 → 11 and
  #            sizeof(MarkGlyphs) changed. We call mark_glyphs() in
  #            options_screen.cpp and read .selector BY NAME, never all() and
  #            never an aggregate initialiser, so the growth is source- and
  #            behaviour-compatible. ⚠ The options screen's cycler hardcodes
  #            "<" and ">", which is exactly what the new fields are for —
  #            correct at the ASCII tier and wrong above it. Its own issue, not
  #            this bump's cargo, on the v0.1.16 precedent above.
  #
  # Pin a tag, not a SHA: the find_package path above is version-gated, so both
  # acquisition paths should describe the same thing in the same vocabulary.
  #
  # ⚠ This variable is also the red-verify seam, and #36 wanted TWO arms rather
  # than the one #24 needed. Run both:
  #
  #   cmake -B build-oldpin  -DTERMFORGE_TAG=v0.4.0 -DCMAKE_CXX_FLAGS=-Werror
  #   cmake -B build-prevpin -DTERMFORGE_TAG=v0.2.2 -DCMAKE_CXX_FLAGS=-Werror
  #
  # find_package(0.6.0) misses any 0.2.x or 0.4.x, so FetchContent takes the
  # override in both. TERMFORGE_URI above accepts a LOCAL PATH, so neither arm
  # needs the network — point it at an existing _deps/termforge-src clone.
  #
  # ⚠ And what the second arm found is why it is not optional. The prediction —
  # written into this file before the arms were run — was that v0.2.2 would be
  # GREEN, on the reasoning that v0.4.0 introduced the stale press flash. It is
  # RED. At v0.2.2 draw() cleared the flag AFTER painting with it, so the flash
  # is stale for exactly one frame; v0.4.0 made it permanent; v0.5.0 cures both.
  # The defect is one we SHIP TODAY, not one the bump would have introduced.
  #
  #   pin      next-showing   outlive-one-paint   (test/11selector)
  #   v0.2.2   RED            GREEN
  #   v0.4.0   RED            RED
  #   v0.6.0   GREEN          GREEN
  #
  # A single arm cannot separate those three columns, and a single test case
  # cannot either — a REQUIRE aborts its case, so the two claims are two cases
  # on purpose. Anyone re-deriving this later: run both arms before believing a
  # per-tag table, including this one.
  if (NOT TERMFORGE_TAG)
    set(TERMFORGE_TAG v0.6.0)
  endif ()

  # termforge's own options already default to PROJECT_IS_TOP_LEVEL, so as a
  # subproject it builds the library and nothing else — no demo binary, no
  # examples, and no Catch2 clone (its FetchContent lives inside the _TESTS
  # branch). Set them anyway: this is the contract we depend on, and an upstream
  # default flip should not silently add ~400 test cases to our build.
  #
  # These normal variables win over termforge's option() calls because CMP0077
  # is NEW under its policy stack (its floor is 3.28). A dependency declaring a
  # floor below 3.13 would ignore all four lines — see the policy note in
  # cmake/deps/catch2.cmake.
  set(termforge_TESTS    OFF)
  set(termforge_EXAMPLES OFF)
  set(termforge_BIN      OFF)
  set(termforge_INSTALL  ${${PROJECT_NAME}_INSTALL})

  include(FetchContent)
  FetchContent_Declare(
    termforge
    GIT_REPOSITORY ${TERMFORGE_URI}
    GIT_TAG        ${TERMFORGE_TAG}
  )

  FetchContent_MakeAvailable(termforge)
endif ()
