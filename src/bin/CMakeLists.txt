add_executable(${PROJECT_NAME}
  main.cpp
)

# glyphcade IS the library plus a main(), so -Dglyphcade_BUILD_LIB=OFF with the
# binary still ON is not a supported configuration. Say so at configure time
# rather than letting it surface as an undefined-symbol wall at link time.
#
# (The template's bin links nothing, which is why it has no such guard — and why
# cpp-template's "library disabled" CI job passes only _BUILD_LIB=OFF _TESTS=OFF.
# That job does not carry over; ours would need _BUILD_BIN=OFF as well.)
if (NOT TARGET ${PROJECT_NAME}::lib)
  message(FATAL_ERROR
    "${PROJECT_NAME}_BUILD_BIN=ON requires ${PROJECT_NAME}_BUILD_LIB=ON. "
    "Pair -D${PROJECT_NAME}_BUILD_LIB=OFF with -D${PROJECT_NAME}_BUILD_BIN=OFF.")
endif ()

# PROJECT_SOURCE_DIR, not CMAKE_SOURCE_DIR: the latter is the top-level build's
# root, which belongs to someone else as soon as this project is consumed.
target_include_directories(${PROJECT_NAME}
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

# The library carries termforge::lib PUBLIC, so this one line brings the
# framework with it.
#
# ⚠ The audio backend is linked SEPARATELY and by name, not through
# ${PROJECT_NAME}::lib, and that separation is gitea #13's decision made
# structural: the library must never reference rtaudio, or the imported target's
# name lands in the exported glyphcadeTargets.cmake and an installed package
# cannot resolve it. See src/audio_backend/CMakeLists.txt.
#
# It carries ${PROJECT_NAME}::lib PUBLIC, so the line below would be enough on
# its own — both are kept because the binary genuinely depends on both, and
# because a reader should not have to know the backend's link type to see that
# the arcade library is linked.
#
# Unconditional: the target exists in both audio arms and only its source file
# differs, which is what keeps this file and main.cpp identical either way.
target_link_libraries(${PROJECT_NAME}
  PRIVATE ${PROJECT_NAME}::lib
  PRIVATE ${PROJECT_NAME}_audio_backend
)

# ⚠ Spelled without the :: alias on purpose — src/audio_backend deliberately has
# none, because an alias is what an exported target looks like and that one must
# never be exported (gitea #13). It carries ${PROJECT_NAME}::lib PUBLIC, so the
# line above is redundant with this one; both are kept so that removing the
# backend does not silently take the library link with it.
#
# This is where make_device_sink() comes from, and it is the ONLY path by which
# rtaudio reaches the binary. The library knows nothing about it.
target_link_libraries(${PROJECT_NAME}
  PRIVATE ${PROJECT_NAME}_audio_backend
)
