# TERMGAME_WITH_AUDIO — auto-detected, defaulting to whether rtaudio was found.
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

set(_termgame_audio_found FALSE)
set(_termgame_audio_how "")

find_package(RtAudio QUIET CONFIG)

if (RtAudio_FOUND)
  set(_termgame_audio_found TRUE)
  set(_termgame_audio_how "find_package(RtAudio CONFIG) ${RtAudio_VERSION}")
else ()
  find_package(PkgConfig QUIET)

  if (PkgConfig_FOUND)
    pkg_check_modules(RTAUDIO QUIET IMPORTED_TARGET rtaudio)

    if (RTAUDIO_FOUND)
      set(_termgame_audio_found TRUE)
      set(_termgame_audio_how "pkg-config rtaudio ${RTAUDIO_VERSION}")
    endif ()
  endif ()
endif ()

# A real option(), so both overrides are honoured and both are meaningful:
#
#   -DTERMGAME_WITH_AUDIO=OFF on a box that HAS rtaudio — the configuration CI
#      runs and the one this repo promises always works. Not hypothetical: the
#      dev container has librtaudio-dev installed but no /dev/snd, so detection
#      says ON there and the OFF arm would rot without an explicit build.
#
#   -DTERMGAME_WITH_AUDIO=ON on a box that does NOT — a loud FATAL_ERROR below,
#      rather than a binary that builds fine and is silently mute.
option(TERMGAME_WITH_AUDIO
  "Build the RtAudio backend (auto-detected; OFF when rtaudio is absent)"
  ${_termgame_audio_found})

if (TERMGAME_WITH_AUDIO AND NOT _termgame_audio_found)
  message(FATAL_ERROR
    "TERMGAME_WITH_AUDIO=ON but rtaudio was not found. Install librtaudio-dev "
    "(Debian/Ubuntu) or point CMAKE_PREFIX_PATH at an RtAudio install — or drop "
    "the flag and let detection decide.")
endif ()

if (TERMGAME_WITH_AUDIO)
  message(STATUS "TERMGAME_WITH_AUDIO=ON (${_termgame_audio_how})")
else ()
  message(STATUS "TERMGAME_WITH_AUDIO=OFF — NullSink / WavFileSink only")
endif ()

# ⚠ Nothing is linked against rtaudio yet, and that is deliberate — Epic 2 owns
# the audio engine. It is also an export question, not laziness: linking
# PkgConfig::RTAUDIO into the exported term-game_lib writes that imported
# target's name into term-gameTargets.cmake, and the installed package then
# needs find_dependency(PkgConfig) plus a pkg_check_modules re-run inside
# cmake/project-config.cmake.in to resolve it. Real design work with no consumer
# yet; Epic 0 leaves the question open rather than guessing at it.
#
# What Epic 0 does ship is the decision made observable: src/lib gets
# TERMGAME_WITH_AUDIO as a PRIVATE compile definition, and the library reports
# it at runtime through termgame::build_has_audio(). test/00bootstrap asserts
# the two agree, which is what catches "the option exists but never reached the
# compiler".
