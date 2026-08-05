cmake_minimum_required(VERSION 3.28)

# Toolchain defaulting, top-level only. A toolchain file has to be set before the
# first project() call in the whole build, so this cannot use PROJECT_IS_TOP_LEVEL
# — that variable does not exist until project() runs, a dozen lines down. This is
# the same test, spelled the only way that works this early.
#
# When this project is consumed (add_subdirectory / FetchContent) the consumer
# owns the toolchain, and setting one here would be dead weight at best.
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain/default.cmake)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/version.cmake)

# The name is written literally, NOT derived from the directory name, and that is a
# deliberate divergence from cpp-template — see the divergences table in STATUS.md.
#
# Deriving it (get_filename_component(... NAME)) makes every target, every
# -D<name>_* option, the install prefix, <name>Config.cmake and PROGRAM_NAME a
# function of whatever the checkout happens to be called. Three ways that bites a
# published library, none of them hypothetical:
#
#   - FetchContent unpacks us into _deps/glyphcade-src, so targets would become
#     glyphcade-src_lib and find_package(glyphcade) would resolve nothing. Our own
#     dependency hardcodes project(termforge ...) for exactly this reason — see the
#     note in cmake/deps/termforge.cmake.
#   - `git clone <url> anything-else`, or a renamed fork, breaks
#     test/00bootstrap's PROGRAM_NAME assertion with a message that names the
#     directory and points nowhere near the cause. This already happened here once,
#     in a checkout called "src".
#   - CI runners choose their own workspace paths.
#
# check_artifacts rule B6 asserts this name and test/00bootstrap's still agree.
project(glyphcade
  VERSION ${VERSION}
  LANGUAGES CXX
)

# CMAKE_INSTALL_{INCLUDE,LIB,BIN}DIR, used by src/lib and cmake/install.cmake.
include(GNUInstallDirs)

# PROJECT_VERSION, not CMAKE_PROJECT_VERSION: the CMAKE_ prefixed variable holds
# the *top-level* project's version, which is someone else's when we are consumed.
message(STATUS "${PROJECT_NAME}:${PROJECT_VERSION} (tweak=${VERSION_TWEAK} dirty=${VERSION_DIRTY} top_level=${PROJECT_IS_TOP_LEVEL})")

set (VERSION_HEADER_IN "${CMAKE_CURRENT_LIST_DIR}/include/version.hpp.in.cmake")

if (EXISTS ${VERSION_HEADER_IN})
  string (REPLACE ".hpp.in.cmake" ".hpp" VERSION_HEADER ${VERSION_HEADER_IN})
  message(STATUS "${VERSION_HEADER_IN} ${VERSION_HEADER}")
  configure_file (${VERSION_HEADER_IN} ${VERSION_HEADER})
endif ()

# ── What gets built ─────────────────────────────────────────────────────────
# Everything except the library defaults to PROJECT_IS_TOP_LEVEL, so a project
# that consumes this one via add_subdirectory() or FetchContent gets the library
# and nothing else — no demo executable, no test suite, and no install rules
# landing in someone else's `cmake --install`. Building this repo directly is
# unchanged: all three are ON.
#
# The library itself defaults ON in src/lib/CMakeLists.txt — it is the thing
# worth consuming.
#
# Declared here, above the dependency list, because that list is gated on them.
option(${PROJECT_NAME}_BUILD_BIN "Build the executable in src/bin"    ${PROJECT_IS_TOP_LEVEL})
option(${PROJECT_NAME}_TESTS     "Build and register the test suite"  ${PROJECT_IS_TOP_LEVEL})
option(${PROJECT_NAME}_INSTALL   "Generate install / package-config rules" ${PROJECT_IS_TOP_LEVEL})

# Dependencies this project uses. Each name maps to cmake/deps/<name>.cmake
# (find_package first, FetchContent fallback). This is the prune point for a new
# project: drop a name to stop fetching that dep.
set(${PROJECT_NAME}_DEPS
  catch2     # Catch2 v3 — test framework (test/)
  termforge  # TermForge — TUI framework, linked PUBLIC into src/lib
)

# rtaudio is deliberately NOT on that list. cmake/deps/ recipes are
# find_package-then-clone-a-git-repo, and rtaudio is a system library that needs
# ALSA/Pulse/JACK dev headers of its own — fetching it only moves the problem.
# Keeping it off the list also keeps check_artifacts rule B2 honest, which would
# otherwise flag it "fetched but unused" on every GLYPHCADE_WITH_AUDIO=OFF build.
# Detection lives in cmake/audio.cmake, included below.

# Component gating: a dependency that exists only to serve a target we are not
# building does not get fetched. Without this, consuming the library alone still
# downloads a test framework and two application libraries.
#
# The list above stays the single prune point — these lines only ever REMOVE
# names from it, never add one. Removing a name that is already gone is a no-op,
# so a project that prunes the list can leave these alone.
if (NOT ${PROJECT_NAME}_TESTS)
  list(REMOVE_ITEM ${PROJECT_NAME}_DEPS catch2)
endif ()

# ⚠ No gate on termforge. The template gates fmtlib/argparse on _BUILD_BIN
# because only the demo binary used them; termforge is linked PUBLIC into the
# library itself, so it is needed in every configuration that builds anything.

include(${CMAKE_CURRENT_LIST_DIR}/cmake/dependencies.cmake)

# GLYPHCADE_WITH_AUDIO. After project() (it needs PkgConfig) and before src/lib
# (which reads the option to set a compile definition).
include(${CMAKE_CURRENT_LIST_DIR}/cmake/audio.cmake)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src/lib)

if (${PROJECT_NAME}_BUILD_BIN)
  # Gated on BUILD_BIN, not on GLYPHCADE_WITH_AUDIO: the target exists in both
  # arms and only its source file changes (see src/audio_backend/CMakeLists.txt).
  # The binary is its only consumer, so with no binary there is nothing to link
  # it — and it is never installed, so nothing else can want it.
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src/audio_backend)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src/bin)
endif ()

if (${PROJECT_NAME}_TESTS)
  include(CTest)
  enable_testing()

  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/test)
endif (${PROJECT_NAME}_TESTS)

# Install rules, package config and the exported target set. Replaces the
# long-standing "TODO Install Template": find_package(<project> CONFIG) is real
# now, and works against an install tree.
if (${PROJECT_NAME}_INSTALL)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/install.cmake)
endif ()
