  # A test dir with its own CMakeLists.txt opts OUT of the discovery loop's
  # wiring — the loop takes the add_subdirectory() branch and never reaches its
  # own link lines. Whatever that loop does for you, this file does for itself.
  # It inherits TEST_NAME and SRCS from the parent scope and must define a
  # target named exactly ${TEST_NAME}. See test/00bootstrap for the same shape.
  #
  # The reason this dir owns its wiring is Threads::Threads at the bottom.

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

  # Same guard as the loop, same reason — the target does not exist under
  # -Dterm-game_BUILD_LIB=OFF. (ring.hpp is header-only, so this test would
  # actually link without it; kept so the dir behaves like every other one.)
  if (TARGET ${PROJECT_NAME}::lib)
    target_link_libraries(${TEST_NAME}
      PRIVATE ${PROJECT_NAME}::lib
    )
  endif ()

  # ⚠ The reason this file exists. This is the only test in the suite that
  # starts a thread of its own, and the discovery loop links no thread library.
  #
  # test/30sanitizer-smoke also uses std::thread and gets away without this,
  # because glibc >= 2.34 folded libpthread into libc and the link is a no-op
  # there. That is a property of this container, not a guarantee — and a ring
  # test that fails to link on the maintainer's box is a bad way to find out.
  find_package(Threads REQUIRED)
  target_link_libraries(${TEST_NAME}
    PRIVATE Threads::Threads
  )
