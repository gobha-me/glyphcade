  # A test dir with its own CMakeLists.txt opts OUT of the discovery loop's
  # wiring — the loop takes the add_subdirectory() branch and never reaches its
  # own link lines. Whatever that loop does for you, this file does for itself.
  # It inherits TEST_NAME and SRCS from the parent scope and must define a
  # target named exactly ${TEST_NAME}.
  #
  # The reason this dir owns its wiring is the compile definition at the bottom.

  message(STATUS "Configuring test setup ${TEST_NAME}")

  add_executable(${TEST_NAME}
    ${PROJECT_SOURCE_DIR}/test/main.cpp
    ${SRCS}
  )

  target_include_directories(${TEST_NAME}
    PUBLIC ${PROJECT_SOURCE_DIR}/include
  )

  target_link_libraries(${TEST_NAME}
    PRIVATE Catch2::Catch2
  )

  # The other half of owning your wiring: the automatic ${PROJECT_NAME}::lib
  # link does not reach here either. Same guard as the loop, same reason — the
  # target does not exist under -Dterm-game_BUILD_LIB=OFF.
  if (TARGET ${PROJECT_NAME}::lib)
    target_link_libraries(${TEST_NAME}
      PRIVATE ${PROJECT_NAME}::lib
    )
  endif ()

  # Hand the test what CMake decided, so it can assert the library's runtime
  # answer agrees. This is the only thing that catches "the option exists but
  # never reached the compiler" — flip -DTERMGAME_WITH_AUDIO and both sides have
  # to move together or this test goes red.
  #
  # Note this definition is on the TEST target, not the library: it carries
  # CMake's belief. build_has_audio() carries the library's. Two independent
  # paths from the same option is what makes the comparison meaningful.
  target_compile_definitions(${TEST_NAME}
    PRIVATE TERMGAME_EXPECT_AUDIO=$<BOOL:${TERMGAME_WITH_AUDIO}>
  )
