# ── The audio backend: the one target that links rtaudio ────────────────────
#
# ⚠ NEVER INSTALLED, NEVER EXPORTED, NEVER IN AN install(EXPORT) SET. This is
# gitea #13's decision, and it is the whole reason this directory exists instead
# of two more lines in src/lib/CMakeLists.txt.
#
# Linking rtaudio into the exported glyphcade_lib would write the imported
# target's name into glyphcadeTargets.cmake, and an installed package could then
# not resolve it — PkgConfig::RTAUDIO only exists in a build tree that ran
# pkg_check_modules. A PRIVATE link still counts, because CMake records it as
# $<LINK_ONLY:...>. The full argument, and the two options not taken, are at the
# bottom of cmake/audio.cmake.
#
# ⚠ The two source files below are ALTERNATIVES, selected here rather than by
# preprocessor branches. Both define glyphcade::audio::make_device_sink(), so
# src/bin/main.cpp contains no #ifdef and is byte-identical in both arms — the
# same trick build_has_audio() plays for the library. The OFF arm differs by
# exactly one thing: which .cpp is compiled.

if (GLYPHCADE_WITH_AUDIO)
  add_library(${PROJECT_NAME}_audio_backend STATIC rtaudio_sink.cpp)

  target_link_libraries(${PROJECT_NAME}_audio_backend
    PRIVATE ${GLYPHCADE_RTAUDIO_TARGET}
  )

  # PRIVATE, exactly as in src/lib: how this was compiled is not part of its
  # interface. It reaches rtaudio_sink.cpp's own #if, nothing else.
  target_compile_definitions(${PROJECT_NAME}_audio_backend
    PRIVATE GLYPHCADE_WITH_AUDIO
  )
else ()
  add_library(${PROJECT_NAME}_audio_backend STATIC silent_sink.cpp)
endif ()

target_link_libraries(${PROJECT_NAME}_audio_backend
  PUBLIC ${PROJECT_NAME}::lib
)

# ⚠ A bare absolute source path in a PUBLIC include directory makes
# install(TARGETS ... EXPORT ...) fail OUTRIGHT — src/lib/CMakeLists.txt goes to
# some trouble with generator expressions to avoid exactly this.
#
# Here it is the point, not an oversight. It is a tripwire: if anyone ever adds
# this target to an export set, CMake stops at generate time with a clear error
# instead of shipping a package that cannot be resolved. The property that would
# be a bug in an installed library is a guard rail in one that must never be.
target_include_directories(${PROJECT_NAME}_audio_backend
  PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
)

# ⚠ NO ${PROJECT_NAME}::audio_backend alias, deliberately. The alias idiom exists
# to make an in-tree target look like the exported one a consumer would use —
# and this target has no exported form and must never acquire one. Adding the
# alias for symmetry with src/lib would be tidying away the signal.
