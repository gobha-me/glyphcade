# ── Library target ──────────────────────────────────────────────────────────
# The shared arcade library: everything term-game is, minus a main(). Disable
# with:
#   cmake -B build -Dterm-game_BUILD_LIB=OFF
#
# OFF removes the target outright, so nothing can link it — including the
# auto-discovered tests, which pick it up automatically when it exists (see
# test/CMakeLists.txt), and src/bin, which now links it too. Pair OFF with
# -Dterm-game_TESTS=OFF and -Dterm-game_BUILD_BIN=OFF; src/bin/CMakeLists.txt
# says so at configure time if you forget.
#
# (cpp-template offers a header-only INTERFACE variant here as a commented
# alternative. Deleted rather than carried: this library has translation units.)

option(${PROJECT_NAME}_BUILD_LIB "Build the compiled library target" ON)

if (${PROJECT_NAME}_BUILD_LIB)
  # Sources are listed explicitly, never globbed — a glob is not re-evaluated
  # until something else forces a re-configure, so a new file silently is not
  # built. Public headers live under include/termgame/.
  #
  # Note the spelling split, and do not "fix" it: the project, the repo and the
  # CMake target are term-game; the include dir and the C++ namespace are
  # termgame, because a hyphen is illegal in a namespace.
  #
  # One target, not one per game. DESIGN.md's destination is a static library
  # per game; that is deferred until a second real game exists and there is
  # something to isolate. Today games live in their own directories — which is
  # the part that actually buys self-containment — and compile in here. When the
  # second game lands, these lines move to games/<slug>/CMakeLists.txt.
  add_library(${PROJECT_NAME}_lib STATIC
    build_info.cpp
    arcade/run_guard.cpp
    arcade/shell.cpp
    arcade/all_games.cpp
    games/stub/stub_game.cpp
  )

  # Compiled libs get the project's include dir PUBLIC so consumers (bin,
  # tests, downstream) inherit it transitively.
  #
  # The two generator expressions are not decoration. PROJECT_SOURCE_DIR is this
  # project's root; CMAKE_SOURCE_DIR would be the *consumer's* root the moment
  # someone add_subdirectory()s us, silently pointing the include path at the
  # wrong tree. And a bare absolute source path in a PUBLIC include directory
  # makes install(TARGETS ... EXPORT ...) fail outright — an installed package
  # must describe where its headers landed, not where they were built from.
  target_include_directories(${PROJECT_NAME}_lib
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  # The C++ standard is a property of this library's public header, not of the
  # build that happens to be compiling it. cmake/toolchain/default.cmake sets
  # CMAKE_CXX_STANDARD for *our* builds, but a consumer uses their own toolchain
  # — without this, they compile include/lib.hpp at whatever their default is.
  target_compile_features(${PROJECT_NAME}_lib PUBLIC cxx_std_23)

  # PUBLIC, not PRIVATE: include/termgame/arcade/shell.hpp derives from
  # termforge::App and holds termforge widgets by value, and arcade/game.hpp
  # names termforge::Screen and termforge::Event in its interface, so every
  # consumer
  # — src/bin, every test, anything downstream — needs termforge's headers and
  # its archive. This is also why cmake/project-config.cmake.in carries a
  # find_dependency(termforge) line.
  target_link_libraries(${PROJECT_NAME}_lib PUBLIC termforge::lib)

  # PRIVATE: how this library was compiled is not part of its public interface.
  # Nothing in include/termgame/ is #ifdef'd on this — the build's audio config
  # is reported at runtime through termgame::build_has_audio() instead, which is
  # what makes it assertable from a test (see test/00bootstrap).
  if (TERMGAME_WITH_AUDIO)
    target_compile_definitions(${PROJECT_NAME}_lib PRIVATE TERMGAME_WITH_AUDIO)
  endif ()

  add_library(${PROJECT_NAME}::lib ALIAS ${PROJECT_NAME}_lib)
endif ()
