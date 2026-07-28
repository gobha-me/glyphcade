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
#   3. Nothing at all in cmake/install.cmake — it stays verbatim.
#
# No FetchContent SOURCE_DIR pin needed here, unlike a cpp-template-derived
# dependency: termforge hardcodes project(termforge ...) rather than deriving it
# from the directory name, precisely so _deps/termforge-src cannot rename it.
#
# HTTPS, not SSH: github.com has no key on the dev container or on CI runners.
# git.gobha.me is the SSH-only forge; github.com is not.

# 0.1.9, not 0.1 — and the patch level here is load-bearing, not pedantry.
#
# termforge's package version file is SameMinorVersion: it accepts a candidate
# whose major.minor match AND whose version is >= the one requested. Asking for
# "0.1" therefore accepts *any* installed 0.1.x, including 0.1.7 — which has no
# App::set_tick_hz. The Shell calls it, so that acceptance would turn a clean
# "your installed package is too old, falling back to FetchContent" into a wall
# of compiler errors in shell.cpp, on whichever machine happens to have a stale
# system copy and nowhere else.
#
# This is the first time we depend on API introduced in a *patch* release. A
# floor has to be expressed at the granularity the dependency actually moves at,
# or it is not a floor.
find_package(termforge 0.1.9 QUIET CONFIG)

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
  #
  # Pin a tag, not a SHA: the find_package path above is version-gated, so both
  # acquisition paths should describe the same thing in the same vocabulary.
  if (NOT TERMFORGE_TAG)
    set(TERMFORGE_TAG v0.1.9)
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
