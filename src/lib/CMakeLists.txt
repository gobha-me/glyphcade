# ── Library targets ───────────────────────────────────────────────────────────
# The shared arcade library: everything glyphcade is, minus a main(). Disable
# with:
#   cmake -B build -Dglyphcade_BUILD_LIB=OFF
#
# OFF removes all four targets outright, so nothing can link them — including the
# auto-discovered tests, which pick ${PROJECT_NAME}::lib up automatically when it
# exists (see test/CMakeLists.txt), and src/bin, which links it too. Pair OFF with
# -Dglyphcade_TESTS=OFF and -Dglyphcade_BUILD_BIN=OFF; src/bin/CMakeLists.txt
# says so at configure time if you forget.
#
# (cpp-template offers a header-only INTERFACE variant here as a commented
# alternative. Deleted rather than carried: this library has translation units.
# ${PROJECT_NAME}_lib must also stay STATIC for a second reason now — see the
# note on ARCH_INDEPENDENT in cmake/install.cmake.)
#
# ── Four targets, and the ORDER IS THE DESIGN ─────────────────────────────────
# DESIGN.md's "one static library per game" landed here. The layering is not a
# taste call: it is the only one that resolves in a single left-to-right linker
# pass, which is what makes it behave identically under GCC's ld and Clang's lld
# with no --start-group, no --whole-archive, and no LINK_INTERFACE_MULTIPLICITY.
#
#   ${PROJECT_NAME}_lib             arcade/shell.cpp, arcade/exception_boundary.cpp
#     │                             the arcade application layer, the ALIAS, and
#     │                             the one target anything outside src/lib links
#     ▼
#   ${PROJECT_NAME}_roster          arcade/all_games.cpp — THE registry table
#     │
#     ▼
#   ${PROJECT_NAME}_game_<name>     one per game, in games/<name>/
#     │
#     ▼
#   ${PROJECT_NAME}_core            build_info.cpp, audio/*.cpp — the shared
#                                   services a game is ALLOWED to use. The
#                                   include dir, cxx_std_23 and termforge::lib
#                                   are PUBLIC here, so every layer above
#                                   inherits them transitively.
#
# Read it as a chain of undefined symbols, because that is what a static archive
# resolves by. These were measured with nm, not assumed:
#
#   shell.cpp        → glyphcade::all_games()                    → the roster
#   all_games.cpp    → glyphcade::Minesweeper::Minesweeper()      → the game
#   minesweeper.cpp  → glyphcade::audio::Engine::play(SfxId)      → core
#
# ⚠ Do NOT tidy arcade/shell.cpp down into ${PROJECT_NAME}_core. It is the only
# translation unit in the repo that references all_games() (three sites), so its
# archive must be scanned BEFORE the roster's — while core's must be scanned
# LAST, because the games call into it. Put the Shell in core and the chain
# closes into a cycle, core → roster → game → core, which a single-pass linker
# cannot resolve. There is no warning; there is
#
#   undefined reference to `glyphcade::all_games()'
#
# on every test binary and on src/bin, in every configuration.
#
# ⚠ And you cannot rescue the wrong shape by reordering a link line. CMake emits
# the topological order, not the declared one: game depends on core, so core is
# pinned last no matter what order target_link_libraries() names it in. Measured
# both ways — with the Shell in core, `PUBLIC roster core` and `PUBLIC core
# roster` produce the identical line
#
#   lib roster game core        (core ONCE, no duplication)
#
# and both fail with `undefined reference to glyphcade::all_games()`. That is the
# good news: the wrong layering has no configuration in which it silently works,
# so this is a mistake you cannot ship. It is also why the fix is to move the
# FILE rather than to reorder anything.
#
# ⚠ Do not fold audio/ up into ${PROJECT_NAME}_lib either. minesweeper.cpp calls
# audio::Engine::play, so audio has to sit below the games. That single edge is
# the whole reason core reads "build_info + audio" instead of "everything that is
# not a game".
#
# The rule for a new file, so nobody has to re-derive this: if a game may call
# it, it belongs in core; if it drives the roster or owns the terminal, it
# belongs in ${PROJECT_NAME}_lib.
#
# ── Where the sources live ────────────────────────────────────────────────────
# Sources are listed explicitly, never globbed — a glob is not re-evaluated until
# something else forces a re-configure, so a new file silently is not built.
# Public headers live under include/glyphcade/.
#
# ⚠ games/<name>/ is the game's NAMESPACE, which is not always its slug. A slug
# is a user-visible id and may start with a digit; a namespace may not.
#
# ⚠ DESIGN.md used to say this split would collapse src/lib/games/<name>/ and
# include/glyphcade/games/<name>/ into a single top-level games/<name>/. It does
# not, and DESIGN.md now records why: cmake/install.cmake ships public headers
# with one install(DIRECTORY include/ FILES_MATCHING PATTERN "*.hpp"), so a
# header that leaves include/ leaves the package, and .clangd plus every
# #include <glyphcade/games/...> in the tree would have to move with it.

option(${PROJECT_NAME}_BUILD_LIB "Build the compiled library targets" ON)

if (${PROJECT_NAME}_BUILD_LIB)
  # ── core: the shared services, and the bottom of the link chain ─────────────
  # ⚠ arcade/scores.cpp is in CORE, not in ${PROJECT_NAME}_lib, and the arcade/
  # prefix is not what decides that — the rule above at "The rule for a new file"
  # is. Multiple games call ctx.scores().record(), so scores has to sit below the
  # games exactly as audio does. Its two siblings in arcade/ land in two other
  # targets: all_games.cpp in _roster, shell.cpp in _lib.
  # ⚠ arcade/hud.cpp and arcade/options_screen.cpp are here for the same reason
  # scores is: every game may call them (term-game#38's pre-start screen is drawn
  # BY the game, not by the Shell), so they must sit below the games. They name
  # Screen and Rgb but no Shell type, so the chain is unchanged.
  add_library(${PROJECT_NAME}_core STATIC
    build_info.cpp
    assets/png.cpp
    arcade/hud.cpp
    arcade/options_screen.cpp
    arcade/scores.cpp
    audio/engine.cpp
    audio/sink.cpp
    audio/synth.cpp
  )

  # ⚠ The audio sources above link NO rtaudio, and that is the term-game#13
  # decision rather than an omission. Only the abstract AudioSink, NullSink and
  # WavFileSink live here; the RtAudio backend is a separate, never-installed
  # target in src/audio_backend/. Linking PkgConfig::RTAUDIO into this library
  # would write that imported target's name into glyphcadeTargets.cmake — a
  # PRIVATE link still counts, as $<LINK_ONLY:...> — and the installed package
  # would then be unresolvable anywhere pkg_check_modules had not run. See the
  # rule spelled out in cmake/project-config.cmake.in.
  #
  # ⚠ Since the split, THIS is the target whose name says "audio", which makes it
  # the newly-tempting place for that one plausible wrong line. It is also the
  # only new name that could carry it. cmake/check_export.cmake is what catches
  # it, and the ctest is called audio-export-clean.

  # PUBLIC on core rather than on ${PROJECT_NAME}_lib, and that is deliberate:
  # core is the bottom of the chain, so every target above inherits these through
  # the PUBLIC link closure — including the game libraries, which need include/
  # and termforge's headers to compile at all.
  #
  # The two generator expressions are not decoration. PROJECT_SOURCE_DIR is this
  # project's root; CMAKE_SOURCE_DIR would be the *consumer's* root the moment
  # someone add_subdirectory()s us, silently pointing the include path at the
  # wrong tree. And a bare absolute source path in a PUBLIC include directory
  # makes install(TARGETS ... EXPORT ...) fail outright — an installed package
  # must describe where its headers landed, not where they were built from.
  target_include_directories(${PROJECT_NAME}_core
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  target_include_directories(${PROJECT_NAME}_core SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/vendor
  )

  # The C++ standard is a property of this library's public headers, not of the
  # build that happens to be compiling it. cmake/toolchain/default.cmake sets
  # CMAKE_CXX_STANDARD for *our* builds, but a consumer uses their own toolchain.
  #
  # Stated once, here, on purpose: compile features propagate transitively
  # through the PUBLIC link closure, so the games, the roster and
  # ${PROJECT_NAME}_lib all compile at C++23 because they reach this target. Do
  # not copy this line onto the other three, and do not delete it — without it a
  # consumer's add_subdirectory() build compiles OUR translation units at THEIR
  # default standard.
  target_compile_features(${PROJECT_NAME}_core PUBLIC cxx_std_23)

  # PUBLIC, and on core for the same transitivity reason:
  # include/glyphcade/audio/sink.hpp names termforge types, arcade/game_meta.hpp
  # uses termforge's width helpers, and arcade/shell.hpp derives from
  # termforge::App and holds termforge widgets by value. Every layer above needs
  # termforge's headers and its archive, and so does anything downstream — which
  # is why cmake/project-config.cmake.in carries a find_dependency(termforge).
  target_link_libraries(${PROJECT_NAME}_core PUBLIC termforge::lib)

  # PRIVATE: how this library was compiled is not part of its public interface.
  # Nothing in include/glyphcade/ is #ifdef'd on this — the build's audio config
  # is reported at runtime through glyphcade::build_has_audio() instead, which is
  # what makes it assertable from a test (see test/00bootstrap).
  #
  # On core because build_info.cpp is here, and build_info.cpp is the ONE
  # translation unit in the repo that reads this macro (grep it). Before the split
  # the definition reached nine TUs and was read by one; it now reaches four and
  # is still read by exactly that one.
  #
  # ⚠ Do not "promote" it to ${PROJECT_NAME}_lib to look tidier. It would stop
  # reaching build_info.cpp, build_has_audio() would answer false in an audio-ON
  # build, and test/00bootstrap would go red — which is precisely the tripwire
  # this definition exists to arm.
  if (GLYPHCADE_WITH_AUDIO)
    target_compile_definitions(${PROJECT_NAME}_core PRIVATE GLYPHCADE_WITH_AUDIO)
  endif ()

  # ── the games: one static library each ──────────────────────────────────────
  # One add_subdirectory line and one list entry per game, both explicit and both
  # greppable. Not a glob over games/*, for the same reason sources are not
  # globbed: a new directory would not be picked up until something else forced a
  # re-configure.
  #
  # The list feeds two places that must never disagree — the roster's link line
  # below, and the export set in cmake/install.cmake — so it is written once and
  # handed up.
  add_subdirectory(games/minesweeper)
  add_subdirectory(games/twenty48)
  add_subdirectory(games/snake)
  add_subdirectory(games/tetris)
  add_subdirectory(games/sokoban)
  add_subdirectory(games/solitaire)

  set(_game_targets
    ${PROJECT_NAME}_game_minesweeper
    ${PROJECT_NAME}_game_twenty48
    ${PROJECT_NAME}_game_snake
    ${PROJECT_NAME}_game_tetris
    ${PROJECT_NAME}_game_sokoban
    ${PROJECT_NAME}_game_solitaire
  )

  # ── the roster: the registry table, and nothing else ────────────────────────
  # arcade/all_games.cpp names every game type, so this archive holds an
  # undefined reference to each game's constructor. That is what makes a dropped
  # game object a LINK ERROR rather than a game that quietly vanishes from the
  # menu — the exact failure mode all_games.cpp's own header comment refuses, and
  # the reason self-registering statics are banned. Mutation-tested: commenting
  # minesweeper.cpp out of the add_library in games/minesweeper/CMakeLists.txt
  # fails the link of every binary with
  # `undefined reference to glyphcade::Minesweeper::Minesweeper()`, plus the vtable
  # and typeinfo for the type.
  add_library(${PROJECT_NAME}_roster STATIC
    arcade/all_games.cpp
  )

  # PUBLIC, and the games only: core's usage requirements arrive through them,
  # since each game links core PUBLIC, which is how all_games.cpp finds
  # <glyphcade/arcade/registry.hpp>. Keeping this line "the games, and nothing
  # else" is what makes adding a game one entry in the list above.
  target_link_libraries(${PROJECT_NAME}_roster PUBLIC ${_game_targets})

  # ── lib: the arcade application layer, and the top of the chain ─────────────
  # arcade/shell.cpp is here because it is the only TU that calls all_games();
  # arcade/exception_boundary.cpp is here because it is main()-facing and no game
  # may reach it. Neither is a shared service, so neither belongs in core.
  add_library(${PROJECT_NAME}_lib STATIC
    arcade/shell.cpp
    arcade/exception_boundary.cpp
  )

  # Written in dependency order for the reader, not for the linker: CMake emits
  # the topological order regardless, so spelling it `core roster` produces the
  # identical link line and stays green (measured). Do not read this order as
  # load-bearing — the load-bearing part is which FILE is in which target, above.
  #
  # core is named explicitly even though the roster already carries it
  # transitively: shell.cpp calls audio::Engine and version_string() directly,
  # and a target should name what it uses.
  target_link_libraries(${PROJECT_NAME}_lib
    PUBLIC
      ${PROJECT_NAME}_roster
      ${PROJECT_NAME}_core
  )

  # ${PROJECT_NAME}::lib is unchanged and remains the ONLY spelling anything
  # outside src/lib uses — src/bin, every auto-discovered test dir, and
  # downstream via find_package(). The three targets underneath get aliases too,
  # because per src/audio_backend/CMakeLists.txt an alias is what an exported
  # target looks like in-tree, and all four are exported.
  add_library(${PROJECT_NAME}::lib    ALIAS ${PROJECT_NAME}_lib)
  add_library(${PROJECT_NAME}::roster ALIAS ${PROJECT_NAME}_roster)
  add_library(${PROJECT_NAME}::core   ALIAS ${PROJECT_NAME}_core)
  add_library(${PROJECT_NAME}::game_minesweeper
                                      ALIAS ${PROJECT_NAME}_game_minesweeper)
  add_library(${PROJECT_NAME}::game_twenty48
                                      ALIAS ${PROJECT_NAME}_game_twenty48)
  add_library(${PROJECT_NAME}::game_snake
                                      ALIAS ${PROJECT_NAME}_game_snake)
  add_library(${PROJECT_NAME}::game_tetris
                                      ALIAS ${PROJECT_NAME}_game_tetris)
  add_library(${PROJECT_NAME}::game_sokoban
                                      ALIAS ${PROJECT_NAME}_game_sokoban)

  # Hand the game list up to the top-level scope, where cmake/install.cmake is
  # included, so the export set cannot drift from the build. Every target above
  # must be in that set — see the argument there.
  set(${PROJECT_NAME}_GAME_TARGETS ${_game_targets} PARENT_SCOPE)
endif ()
