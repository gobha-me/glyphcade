  # A test dir with its own CMakeLists.txt opts OUT of the discovery loop's
  # wiring — the loop takes the add_subdirectory() branch and never reaches its
  # own link lines. Whatever that loop does for you, this file does for itself.
  # It inherits TEST_NAME and SRCS from the parent scope and must define a
  # target named exactly ${TEST_NAME}.
  #
  # This dir owns its wiring for the compile definition at the bottom, exactly as
  # test/00bootstrap does for the audio option.

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

  # The other half of owning your wiring: the automatic ${PROJECT_NAME}::lib link
  # does not reach here either. Same guard as the loop, same reason — the target
  # does not exist under -Dglyphcade_BUILD_LIB=OFF.
  if (TARGET ${PROJECT_NAME}::lib)
    target_link_libraries(${TEST_NAME}
      PRIVATE ${PROJECT_NAME}::lib
    )
  endif ()

  # ── How many games does the BUILD think there are? ─────────────────────────
  # Hand the test CMake's own count, so it can assert the roster agrees.
  #
  # This is the check that became necessary the moment a second game existed. A
  # by-name case ("minesweeper is registered") cannot see a game that was
  # compiled and linked and then left out of the kGames table — and since the
  # per-game library split, adding a game touches src/lib's game list AND
  # arcade/all_games.cpp, so the two CAN disagree. The link would succeed: the
  # roster simply would not mention it, and the menu would be short by one.
  #
  # ${PROJECT_NAME}_GAME_TARGETS is the same list that builds the game libraries,
  # handed up from src/lib/CMakeLists.txt by PARENT_SCOPE. So the number here and
  # the table over there come from independent places, which is the only reason
  # comparing them means anything — the same argument test/00bootstrap makes for
  # GLYPHCADE_EXPECT_AUDIO.
  #
  # ⚠ If this goes red after you add a game, the likely cause is a missing entry
  # in kGames rather than a wrong count here.
  list(LENGTH ${PROJECT_NAME}_GAME_TARGETS _game_count)
  target_compile_definitions(${TEST_NAME}
    PRIVATE GLYPHCADE_EXPECT_GAMES=${_game_count}
  )
