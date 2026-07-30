# Asserts that the installed package does not reference rtaudio (gitea #13).
#
# Run as a ctest:
#   cmake -DBUILD_DIR=<build> -DPROJECT=term-game -P cmake/check_export.cmake
#
# ── Why this exists ─────────────────────────────────────────────────────────
#
# Epic 2's export decision is enforced by nothing except where files happen to
# live. One plausible-looking line —
#
#   target_link_libraries(term-game_core PRIVATE PkgConfig::RTAUDIO)
#
# — undoes it, and the damage is INVISIBLE from inside this repo: the build
# stays green, the tests stay green, and the only symptom is that somebody
# else's `find_package(term-game)` fails on a machine that never ran
# pkg_check_modules. Nobody here would see it.
#
# ⚠ And PRIVATE is not a defence. CMake records a private link into a static
# library on the exported target as $<LINK_ONLY:...>, because a consumer still
# has to link it. Judge by what links it, not by the keyword — the same warning
# cpp-template's own catch2 recipe gives, and the reason cpp-template CT-15
# happened.
#
# ⚠ Since the per-game library split there are FOUR exported targets, and the
# example above deliberately names term-game_core rather than term-game_lib: core
# is the one whose name says "audio" and holds audio/*.cpp, so it is where that
# line would most plausibly be written. This check reads the whole Targets file,
# so it covers all four — but a reader looking for "where would this go wrong"
# should look at core first, then at any new game library.
#
# So the guard is: install to a scratch prefix and read the Targets file. That
# is the artefact a consumer actually resolves, which makes this the only check
# that tests the thing that breaks rather than a proxy for it.
#
# It is most meaningful in the arm where audio is ON — the default in the dev
# container — because that is the only configuration where there is an rtaudio
# target available to leak in the first place.

cmake_minimum_required(VERSION 3.28)

if (NOT DEFINED BUILD_DIR OR NOT DEFINED PROJECT)
  message(FATAL_ERROR "check_export.cmake needs -DBUILD_DIR= and -DPROJECT=")
endif ()

set(_prefix "${BUILD_DIR}/export-check-prefix")
file(REMOVE_RECURSE "${_prefix}")

execute_process(
  COMMAND ${CMAKE_COMMAND} --install "${BUILD_DIR}" --prefix "${_prefix}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)

if (NOT _rc EQUAL 0)
  message(FATAL_ERROR "install failed (${_rc}):\n${_out}\n${_err}")
endif ()

file(GLOB_RECURSE _targets "${_prefix}/**/${PROJECT}Targets*.cmake")

if (NOT _targets)
  message(FATAL_ERROR
    "no ${PROJECT}Targets.cmake under ${_prefix} — the package installed "
    "nothing to export. Either the library was switched off, or install(EXPORT) "
    "stopped running; both make this check vacuous rather than passing.")
endif ()

# The forbidden tokens. `audio_backend` is here as well as rtaudio itself
# because the backend target is the only thing that links rtaudio — if its NAME
# reaches the export, the decision has already been undone even if the imported
# target's name happens not to appear.
set(_forbidden "rtaudio" "RTAUDIO" "RtAudio" "audio_backend")

foreach (_file IN LISTS _targets)
  file(READ "${_file}" _text)
  foreach (_tok IN LISTS _forbidden)
    string(FIND "${_text}" "${_tok}" _at)
    if (NOT _at EQUAL -1)
      message(FATAL_ERROR
        "gitea #13 VIOLATED: '${_tok}' appears in ${_file}.\n"
        "The RtAudio backend has leaked into the exported package. An installed "
        "term-game can no longer be resolved on a machine that has not run "
        "pkg_check_modules.\n"
        "Remember a PRIVATE link still counts — CMake records it as "
        "$<LINK_ONLY:...>. rtaudio belongs to src/audio_backend/ only, which is "
        "never installed and never exported. See cmake/audio.cmake.")
    endif ()
  endforeach ()
endforeach ()

# And the backend's private header must not have been installed either.
file(GLOB_RECURSE _leaked "${_prefix}/**/device_sink.hpp")
if (_leaked)
  message(FATAL_ERROR
    "gitea #13 VIOLATED: ${_leaked} was installed. src/audio_backend/include/ "
    "is a private include root and must never appear in an install(DIRECTORY).")
endif ()

file(REMOVE_RECURSE "${_prefix}")
message(STATUS "export is rtaudio-free: CLEAN")
