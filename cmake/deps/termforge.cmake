# TermForge — the TUI framework term-game is built on (gobha-me/termforge).
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
# HTTPS, not SSH: github.com has no key on the dev container or on CI runners.
# git.gobha.me is the SSH-only forge; github.com is not.

# 0.1.15, not 0.1 — and the patch level here is load-bearing, not pedantry.
#
# termforge's package version file is SameMinorVersion: it accepts a candidate
# whose major.minor match AND whose version is >= the one requested. Asking for
# "0.1" therefore accepts *any* installed 0.1.x. Three ways that bites, in
# increasing order of how hard they are to notice:
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
#     selection marker TWICE. Since gitea #17 the Shell no longer draws its own
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
# include/termgame/arcade/shell.hpp, which we install. A consumer that resolves
# an older 0.1.x compiles our public header against a different object layout
# than term-game_lib was built with. That is not a link error; it is a silent
# one. cmake/project-config.cmake.in carries the same floor for that reason.
find_package(termforge 0.1.15 QUIET CONFIG)

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
  #            terminal-restore job; gitea #16.
  #  v0.1.11 — ListWidget marks its own selection (#72): set_style, set_marker,
  #            set_marker_enabled, gutter_cols, and the colour setters that
  #            never existed. The marker is a glyph in a two-column gutter the
  #            widget reserves on every row, on by default, and — unlike the
  #            workaround it replaced — INSIDE rect(), so a click on it selects.
  #            This is the tag that retired the selector's hand-drawn "> ";
  #            gitea #17. Also the tag that changed sizeof(ListWidget); see the
  #            ABI note above.
  #  v0.1.12 — the same fix for Select/MenuBar dropdowns, and
  #  v0.1.13 — for TableWidget (#76). We use neither; taken because they are on
  #            the way to v0.1.14 and splitting the pin buys nothing.
  #  v0.1.14 — App::running() (#73). This is what retired Shell::quit_requested,
  #            which existed only because App::m_running was unreadable; gitea
  #            #17. ⚠ It is not the same observable: test_run_frames re-arms
  #            m_running on entry, so running() answers "did a quit happen
  #            during the last run", not "ever". test/11selector's trap list
  #            says what that costs a test that asserts in the wrong order.
  #  v0.1.15 — Terminal::set_mouse_mode (#75). Not used yet; taken because it is
  #            the tip. Its default, MouseMode::Drag, is byte-for-byte the
  #            ?1006h?1002h we were already emitting, so taking the tag changes
  #            nothing until something calls it. MouseMode::Motion is what
  #            Minesweeper wants for buttonless hover — gitea #18.
  #
  # Pin a tag, not a SHA: the find_package path above is version-gated, so both
  # acquisition paths should describe the same thing in the same vocabulary.
  if (NOT TERMFORGE_TAG)
    set(TERMFORGE_TAG v0.1.15)
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
