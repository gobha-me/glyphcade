# GLYPHCADE_WITH_AUDIO — auto-detected, defaulting to whether rtaudio was found.
#
# Not a cmake/deps/ recipe on purpose: those are find_package-then-clone, and
# rtaudio is a system library that needs ALSA/Pulse/JACK dev headers of its own.
# See the note beside ${PROJECT_NAME}_DEPS in the root CMakeLists.txt.
#
# Detection order, and both paths are load-bearing:
#   1. RtAudio's own CMake package — upstream >= 5.1 installs RtAudioConfig.cmake.
#   2. pkg-config `rtaudio` — what Debian/Ubuntu's librtaudio-dev ships. That
#      package installs NO CMake config file, so step 1 misses on every
#      apt-based box, including this project's dev container and any Ubuntu CI
#      runner. A CONFIG-only design would look correct and never fire.

set(_glyphcade_audio_found FALSE)
set(_glyphcade_audio_how "")

find_package(RtAudio QUIET CONFIG)

if (RtAudio_FOUND)
  set(_glyphcade_audio_found TRUE)
  set(_glyphcade_audio_how "find_package(RtAudio CONFIG) ${RtAudio_VERSION}")
  set(GLYPHCADE_RTAUDIO_TARGET RtAudio::rtaudio)
else ()
  find_package(PkgConfig QUIET)

  if (PkgConfig_FOUND)
    pkg_check_modules(RTAUDIO QUIET IMPORTED_TARGET rtaudio)

    if (RTAUDIO_FOUND)
      set(_glyphcade_audio_found TRUE)
      set(_glyphcade_audio_how "pkg-config rtaudio ${RTAUDIO_VERSION}")
      set(GLYPHCADE_RTAUDIO_TARGET PkgConfig::RTAUDIO)
    endif ()
  endif ()
endif ()

# A real option(), so both overrides are honoured and both are meaningful:
#
#   -DGLYPHCADE_WITH_AUDIO=OFF on a box that HAS rtaudio — the configuration CI
#      runs and the one this repo promises always works. Not hypothetical: the
#      dev container has librtaudio-dev installed but no /dev/snd, so detection
#      says ON there and the OFF arm would rot without an explicit build.
#
#   -DGLYPHCADE_WITH_AUDIO=ON on a box that does NOT — a loud FATAL_ERROR below,
#      rather than a binary that builds fine and is silently mute.
option(GLYPHCADE_WITH_AUDIO
  "Build the RtAudio backend (auto-detected; OFF when rtaudio is absent)"
  ${_glyphcade_audio_found})

if (GLYPHCADE_WITH_AUDIO AND NOT _glyphcade_audio_found)
  message(FATAL_ERROR
    "GLYPHCADE_WITH_AUDIO=ON but rtaudio was not found. Install librtaudio-dev "
    "(Debian/Ubuntu) or point CMAKE_PREFIX_PATH at an RtAudio install — or drop "
    "the flag and let detection decide.")
endif ()

if (GLYPHCADE_WITH_AUDIO)
  message(STATUS "GLYPHCADE_WITH_AUDIO=ON (${_glyphcade_audio_how})")
else ()
  message(STATUS "GLYPHCADE_WITH_AUDIO=OFF — NullSink / WavFileSink only")
endif ()

# ── Where rtaudio is linked, and where it must never be (gitea #13) ─────────
#
# DECIDED, and the decision is load-bearing rather than stylistic. rtaudio is
# linked by exactly one target — ${PROJECT_NAME}_audio_backend, in
# src/audio_backend/ — which is never installed and never exported.
#
# The trap being avoided: linking ${GLYPHCADE_RTAUDIO_TARGET} into the exported
# glyphcade_lib writes that imported target's name into glyphcadeTargets.cmake,
# and an installed package then cannot resolve it, because PkgConfig::RTAUDIO
# only exists in a build tree that ran pkg_check_modules. ⚠ A PRIVATE link still
# counts — CMake records it on the exported target as $<LINK_ONLY:...>, because a
# consumer still has to link it. Judge by what links it, not by the keyword; the
# rule is spelled out at length in cmake/project-config.cmake.in.
#
# The two options not taken, and why:
#
#   * find_dependency(PkgConfig) plus a pkg_check_modules re-run inside
#     project-config.cmake.in. Works, but puts a pkg-config requirement in every
#     consumer's path, and makes the exported package's contents depend on how
#     the build box was provisioned.
#   * gating install(EXPORT) off entirely. Simplest and arguably honest, since
#     nothing is expected to find_package(glyphcade) — but the export currently
#     works end to end and is this repo's cheapest build-health signal.
#
# Keeping the backend out preserves both: install(EXPORT) is untouched by Epic 2,
# and cmake/project-config.cmake.in still names termforge and nothing else.
#
# The OFF arm differs by exactly one thing: which source file
# src/audio_backend/ compiles. Same target name, same link line from src/bin,
# same main.cpp, same installed headers, same exported package.
#
# What Epic 0 shipped and this keeps: GLYPHCADE_WITH_AUDIO is a PRIVATE compile
# definition on **glyphcade_core** and the library reports it at runtime through
# glyphcade::build_has_audio(), which test/00bootstrap asserts against CMake's own
# belief. ⚠ That definition looks unused, because no audio source is #ifdef'd on
# it any more — it is not. It is what makes "the option exists but never reached
# the compiler" a red test rather than a silent lie.
#
# ⚠ On core specifically, since the per-game library split, because
# src/lib/build_info.cpp lives there and is the ONE translation unit in the repo
# that reads the macro (grep it). Moving the definition up to glyphcade_lib looks
# tidier — glyphcade_lib is "the library", after all — and stops it reaching that
# TU, at which point build_has_audio() answers false in an audio-ON build and
# test/00bootstrap goes red. Which is the tripwire working, but the message points
# at the option rather than at the target that lost the define.
