add_test(NAME startup COMMAND ${CMAKE_CURRENT_LIST_DIR}/../cmake/startup.sh)
add_test(NAME shutdown COMMAND ${CMAKE_CURRENT_LIST_DIR}/../cmake/shutdown.sh)

# Pure cmake -P unit test for the git-describe version parser (cmake/version_parse.cmake).
# Registered standalone — it is a script, not a `<dir>-test` executable, so it must
# not go through the auto-discovery foreach below. No fixtures: it has no runtime deps.
add_test(
  NAME version-parse-selftest
  COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/version_selftest.cmake
)

# Leftover-template-artifact check (cmake/check_artifacts.cmake), also standalone.
# The mode is keyed on NEW_PROJECT.md, whose last checklist step is "delete this
# file": while it exists we are the template, so the Class-A rules are inverted
# and assert each still MATCHES something (a rule that matches nothing has rotted
# and would wave every fork through). Once a bootstrapped project deletes it, the
# same script becomes a plain enforcement check with nothing to wire up.
if (EXISTS ${PROJECT_SOURCE_DIR}/NEW_PROJECT.md)
  add_test(
    NAME artifact-check-selftest
    COMMAND ${CMAKE_COMMAND} -DMODE=selftest -P ${PROJECT_SOURCE_DIR}/cmake/check_artifacts.cmake
  )
else ()
  add_test(
    NAME artifact-check
    COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/check_artifacts.cmake
  )
endif ()

# The only test in the suite that looks at real escape bytes: it drives a
# deliberately-throwing binary under a pty and asserts the terminal came back.
# See cmake/pty_restore.sh for what it judges and why it does not judge the
# exit status.
#
# Standalone, like the two above — it is a script driving a non-Catch2 binary,
# so it must not go through the auto-discovery foreach, which would demand a
# `<dir>-test` target. (The probe's .cpp sits loose in test/ next to main.cpp;
# the foreach skips it, since a file has neither CMakeLists.txt nor test.cpp
# inside it.)
#
# Gated on script(1) rather than depending on it. CI here is written but not yet
# proven green and the runner image's tooling is not known, so a hard dependency
# would be the wrong bet — but a silently-absent test is worse than a missing
# one, hence the STATUS message and the note in AGENTS.md that `ctest -N` should
# list pty-restore.
find_program(SCRIPT_EXECUTABLE script)

if (SCRIPT_EXECUTABLE AND TARGET ${PROJECT_NAME}::lib)
  message(STATUS "Configuring test setup pty-restore")

  add_executable(pty-restore-probe pty_restore_probe.cpp)
  target_link_libraries(pty-restore-probe PRIVATE ${PROJECT_NAME}::lib)

  add_test(
    NAME pty-restore
    COMMAND ${CMAKE_CURRENT_LIST_DIR}/../cmake/pty_restore.sh
            $<TARGET_FILE:pty-restore-probe>
  )
  set_tests_properties(pty-restore PROPERTIES FIXTURES_REQUIRED runners)
else ()
  message(STATUS
    "pty-restore not configured — "
    "script(1) found: ${SCRIPT_EXECUTABLE}. The throw-path terminal-restore "
    "check is skipped; test/21exception still covers the headless half."
  )
endif ()

# The term-game#13 guard: install to a scratch prefix and assert the exported
# Targets file names no rtaudio. Standalone, like the checks above — it is a
# cmake -P script, not a `<dir>-test` executable.
#
# ⚠ This is the ONLY thing standing between Epic 2's export decision and one
# plausible-looking target_link_libraries line. The damage that line would do is
# invisible from inside this repo — everything stays green and only a downstream
# find_package(glyphcade) breaks, on someone else's machine. See
# cmake/check_export.cmake for why a PRIVATE link is not a defence.
#
# Its sibling, the term-game#46 guard: install to a scratch prefix and CONFIGURE a
# generated throwaway consumer against it, so the floor in
# cmake/project-config.cmake.in is executed rather than merely written. Neither
# check subsumes the other — audio-export-clean judges the CONTENT of the
# exported targets, consumer-resolves judges whether the package RESOLVES at
# all. A stale floor makes the second red while the first still says CLEAN,
# which is exactly the hole #46 was filed about.
#
# Both are gated on _INSTALL because there is nothing to install, inspect or
# resolve without install rules. One block rather than two with the same
# condition: two near-identical else() branches drift apart.
if (${PROJECT_NAME}_INSTALL AND TARGET ${PROJECT_NAME}::lib)
  message(STATUS "Configuring test setup audio-export-clean")
  add_test(
    NAME audio-export-clean
    COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${PROJECT_BINARY_DIR}
            -DPROJECT=${PROJECT_NAME}
            -P ${PROJECT_SOURCE_DIR}/cmake/check_export.cmake
  )

  message(STATUS "Configuring test setup consumer-resolves")
  # EXPECT_VERSION pins the check to THIS build — without it a stray system
  # install could answer the consumer's find_package and the test would pass
  # forever. termforge_DIR is forwarded so the check still works on a machine
  # where termforge was found rather than fetched; it is legitimately
  # <name>-NOTFOUND here, and check_consumer.cmake ignores that spelling.
  add_test(
    NAME consumer-resolves
    COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${PROJECT_BINARY_DIR}
            -DPROJECT=${PROJECT_NAME}
            -DEXPECT_VERSION=${PROJECT_VERSION}
            -DTERMFORGE_DIR=${termforge_DIR}
            -P ${PROJECT_SOURCE_DIR}/cmake/check_consumer.cmake
  )

  # Both scripts run `cmake --install ${PROJECT_BINARY_DIR}`, which finishes by
  # writing ${CMAKE_BINARY_DIR}/install_manifest.txt. Their scratch PREFIXES do
  # not collide, but that one manifest is a shared write in the build tree, and
  # two concurrent installs race on it under `ctest -j`. It would rarely fail
  # outright — which is worse, because the failure mode is one unreproducible
  # red per quarter rather than something you can sit down and debug.
  #
  # RESOURCE_LOCK is plain mutual exclusion by label; the string names nothing
  # real. This is the only lock in the suite, and it serialises two tests.
  set_tests_properties(audio-export-clean consumer-resolves
    PROPERTIES RESOURCE_LOCK install-tree)
else ()
  message(STATUS
    "audio-export-clean and consumer-resolves not configured — "
    "${PROJECT_NAME}_INSTALL is off, so there is no exported package to inspect "
    "and nothing installed for a consumer to resolve. term-game#13's and term-game#46's "
    "guards are ABSENT in this configuration, not passing.")
endif ()

file(GLOB TEST_DIRS ${CMAKE_CURRENT_LIST_DIR}/*)

# Say this once, at configure time, instead of letting it surface as a link error
# buried in a build log. The guard below keeps *configuring* valid with the
# library switched off; it cannot conjure symbols for a test that calls into it.
if (NOT TARGET ${PROJECT_NAME}::lib)
  message(STATUS
    "${PROJECT_NAME}::lib is not built — discovered tests will not link against it. "
    "Pair -D${PROJECT_NAME}_BUILD_LIB=OFF with -D${PROJECT_NAME}_TESTS=OFF, or keep "
    "only tests that do not call into the library."
  )
endif ()

foreach(TEST_DIR ${TEST_DIRS})
  cmake_path(GET TEST_DIR FILENAME TEST_NAME_PATH)
  set(TEST_NAME ${TEST_NAME_PATH}-test)

  # Recompute the source list for THIS dir every iteration, before the branch
  # below. Both branches consume SRCS — the custom-CMakeLists branch inherits it
  # via add_subdirectory() — so setting it only inside the elseif would leak the
  # previous iteration's sources into a custom-CMakeLists test dir.
  file(GLOB SRCS ${TEST_DIR}/*.cpp)

  if(EXISTS ${TEST_DIR}/CMakeLists.txt)
    add_subdirectory(${TEST_DIR})
  elseif (EXISTS ${TEST_DIR}/test.cpp)
    message(STATUS "Configuring test setup ${TEST_NAME}")

    add_executable(${TEST_NAME}
      main.cpp
      ${SRCS}
    )

    # Kept even though ${PROJECT_NAME}::lib already propagates include/ below:
    # with -D<PROJECT>_BUILD_LIB=OFF there is no library to propagate anything,
    # and every test still includes the generated <version.hpp> from there. The
    # second path is a hook for shared test-only headers in test/include/.
    target_include_directories(${TEST_NAME}
      PUBLIC ${PROJECT_SOURCE_DIR}/include
      PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
    )

    target_link_libraries(${TEST_NAME}
      PRIVATE Catch2::Catch2
    )

    # Auto-discovered tests get the project library for free. Without this,
    # testing your own code means either a per-dir CMakeLists.txt or copying
    # library sources into the test dir — and the first test written against
    # src/lib/ dies at link time with `undefined reference`, which reads like a
    # broken build rather than a missing line.
    #
    # The guard is not optional: -D<PROJECT>_BUILD_LIB=OFF never creates the
    # target, and an unguarded link line would turn that supported configuration
    # into a hard configure error.
    #
    # The ALIAS makes the STATIC/INTERFACE choice in src/lib/CMakeLists.txt
    # invisible here — linking an ALIAS to an INTERFACE library propagates its
    # usage requirements and archives nothing, so this line is correct for both.
    #
    # This lives in the elseif branch on purpose: a test dir with its own
    # CMakeLists.txt owns all of its wiring, this included (see test/02example).
    if (TARGET ${PROJECT_NAME}::lib)
      target_link_libraries(${TEST_NAME}
        PRIVATE ${PROJECT_NAME}::lib
      )
    endif ()
  else()
    continue()
  endif()

  # No -s here: it makes Catch2 report every *successful* assertion too, which
  # buries the interesting output in a full-suite run. Ask for it per invocation
  # when you actually want it — `ctest -R <name> -V`, or `./build/test/<dir>-test -s`.
  add_test(
    NAME ${TEST_NAME}
    COMMAND ${TEST_NAME}
  )

  set_tests_properties(${TEST_NAME} PROPERTIES FIXTURES_REQUIRED runners)
endforeach()

# SET FIXTURE DEPENDENCIES
set_tests_properties(startup PROPERTIES FIXTURES_SETUP runners)
set_tests_properties(shutdown PROPERTIES FIXTURES_CLEANUP runners)

# The sanitizer smoke test (test/30sanitizer-smoke) deliberately trips the active
# sanitizer to prove it is actually engaged (CT-03 — the whole point is to catch a
# silently no-op sanitizer toolchain). ASan/TSan abort the process on the injected
# bug, i.e. a NONZERO exit, which ctest would otherwise read as failure. So when
# such a sanitizer is active we assert on its diagnostic instead: the test passes
# iff the sanitizer's own report appears in the output. This turns "sanitizer
# engaged" into a green, enforceable signal — and if the sanitizer were ever
# silenced, the report vanishes and this test goes red. UBSan is recoverable
# (exits 0) and the default build never runs the bug, so neither needs this.
if (TARGET 30sanitizer-smoke-test)
  if (CMAKE_CXX_FLAGS MATCHES "-fsanitize=address")
    set_tests_properties(30sanitizer-smoke-test PROPERTIES PASS_REGULAR_EXPRESSION "AddressSanitizer")
  elseif (CMAKE_CXX_FLAGS MATCHES "-fsanitize=thread")
    set_tests_properties(30sanitizer-smoke-test PROPERTIES PASS_REGULAR_EXPRESSION "ThreadSanitizer")
  endif ()
endif ()
