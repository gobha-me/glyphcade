add_executable(${PROJECT_NAME}
  main.cpp
)

# term-game IS the library plus a main(), so -Dterm-game_BUILD_LIB=OFF with the
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
target_link_libraries(${PROJECT_NAME}
  PRIVATE ${PROJECT_NAME}::lib
)
