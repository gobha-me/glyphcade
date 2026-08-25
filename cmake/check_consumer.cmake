# Asserts both sides of the package-config contract: an installed package with
# exported targets resolves, while a config with no Targets file beside it is
# rejected with our own diagnosis rather than CMake's generic include failure
# (term-game#46, GitHub #12).
#
# Run as a ctest:
#   cmake -DBUILD_DIR=<build> -DSOURCE_DIR=<source> \
#         -DPROJECT=glyphcade -DEXPECT_VERSION=<ver> \
#         [-DTERMFORGE_DIR=<dir>] -P cmake/check_consumer.cmake
#
# ── Why this exists, and why check_export.cmake is not it ─────────────────────
#
# cmake/project-config.cmake.in carries the consumer-side termforge floor:
#
#   find_dependency(termforge 0.57.14)
#
# Until this file existed, nothing in ctest EXECUTED that line. Its sibling
# cmake/check_export.cmake installs to a scratch prefix and greps the generated
# glyphcadeTargets*.cmake for rtaudio tokens — as TEXT. It never calls
# find_package(glyphcade), so the generated glyphcadeConfig.cmake was written
# and inspected and never run.
#
# That made a wrong floor invisible from two directions at once. The in-tree
# build takes the FetchContent path in cmake/deps/termforge.cmake and never
# reads project-config.cmake.in at all, and CI only builds in-tree. The failure
# surfaces exclusively in a stranger's source tree — which is the failure
# cmake/deps/termforge.cmake names in its own words: a half-done bump "produces
# a package that resolves on the developer's machine and nowhere else".
#
# ⚠ The gap was demonstrated, not merely reasoned about. With the floor set to a
# stale 0.6.0 while the install carries termforge 0.57.14:
#
#   a real consumer  →  Could not find a configuration file for package
#                       "termforge" ... , exit 1
#   check_export     →  -- export is rtaudio-free: CLEAN
#
# A package no consumer on earth can resolve, reported CLEAN. This script is the
# check that goes red on that row. The two are siblings and neither subsumes the
# other: check_export judges the CONTENT of the exported targets, this one
# judges whether the package RESOLVES.
#
# The floor has moved five times
# (0.1 → 0.1.10 → 0.1.15 → 0.2.2 → 0.6.0 → 0.57.14), and
# every one of those times correctness rested on somebody remembering to edit a
# second file. That is the thing being automated here.
#
# ── Why a generated consumer rather than a committed one ──────────────────────
#
# The obvious shape is a three-line CMakeLists.txt committed under test/. It
# does not work: test/CMakeLists.txt globs test/* and add_subdirectory()s any
# directory holding a CMakeLists.txt, so a committed test/consumer/ would be
# pulled into OUR build and run find_package(glyphcade) at our own configure
# time. Generating it into the build tree keeps it unreachable by that glob, and
# keeps the whole check readable as one file.

cmake_minimum_required(VERSION 3.28)

if (NOT DEFINED BUILD_DIR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED PROJECT OR
    NOT DEFINED EXPECT_VERSION)
  message(FATAL_ERROR
    "check_consumer.cmake needs -DBUILD_DIR=, -DSOURCE_DIR=, -DPROJECT= and "
    "-DEXPECT_VERSION=")
endif ()

set(_prefix       "${BUILD_DIR}/consumer-check-prefix")
set(_src          "${BUILD_DIR}/consumer-check-src")
set(_bin          "${BUILD_DIR}/consumer-check-bin")
set(_build_bin    "${BUILD_DIR}/consumer-check-build-tree-bin")
set(_nolib_build  "${BUILD_DIR}/consumer-check-nolib-build")
set(_nolib_prefix "${BUILD_DIR}/consumer-check-nolib-prefix")
set(_nolib_bin    "${BUILD_DIR}/consumer-check-nolib-bin")

# All SEVEN, and before anything else. The prefixes are the obvious ones, but a
# leftover consumer cache from a previous green run still holds ${PROJECT}_DIR
# and termforge_DIR as resolved entries — which is the single most likely way a
# red arm of this check would come out green.
file(REMOVE_RECURSE
  "${_prefix}" "${_src}" "${_bin}" "${_build_bin}"
  "${_nolib_build}" "${_nolib_prefix}" "${_nolib_bin}")

# ── 1. Install the already-built tree to a scratch prefix ─────────────────────
#
# One install is enough for both packages: termforge_INSTALL follows
# ${PROJECT_NAME}_INSTALL (see cmake/deps/termforge.cmake), so when termforge
# came from FetchContent the prefix gets lib/cmake/termforge/ alongside
# lib/cmake/glyphcade/. No network, no second install step.

execute_process(
  COMMAND ${CMAKE_COMMAND} --install "${BUILD_DIR}" --prefix "${_prefix}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)

if (NOT _rc EQUAL 0)
  message(FATAL_ERROR "install failed (${_rc}):\n${_out}\n${_err}")
endif ()

# ── 2. Generate the throwaway consumer ────────────────────────────────────────
#
# A bracket argument: CMake substitutes NOTHING inside [==[ ]==], so every line
# below reaches the generated file byte for byte. The project name is not
# spelled here either — it arrives as -DCHECK_PROJECT= on the consumer's own
# command line. That is what lets this text need no escaping at all, which
# matters more than it looks: the alternative spelling is "\${${PROJECT}_DIR}",
# where one dropped backslash silently writes an empty marker instead of a
# missing one, and an empty marker is exactly the vacuous pass this check is
# supposed to be incapable of.
#
# ${${CHECK_PROJECT}_VERSION} is a double dereference, and the hyphen in
# `glyphcade` is fine in a variable name — test/CMakeLists.txt already relies on
# the same thing in `if (${PROJECT_NAME}_INSTALL)`.
#
# project(consume CXX), not NONE. NONE would skip compiler detection and shave a
# couple of seconds, and everything in glyphcadeTargets.cmake would still work —
# but a real consumer has a compiler, and fidelity to the thing being verified
# is the point. Keep NONE in your pocket only if a runner ever turns up without
# a default compiler.

file(WRITE "${_src}/CMakeLists.txt" [==[
cmake_minimum_required(VERSION 3.28)
project(consume CXX)

find_package(${CHECK_PROJECT} REQUIRED)

message(STATUS "CONSUMED-PROJECT-VERSION=${${CHECK_PROJECT}_VERSION}")
message(STATUS "CONSUMED-PROJECT-DIR=${${CHECK_PROJECT}_DIR}")
message(STATUS "CONSUMED-TERMFORGE-VERSION=${termforge_VERSION}")
message(STATUS "CONSUMED-TERMFORGE-DIR=${termforge_DIR}")
]==])

# ── 3. Configure it against the scratch prefix ────────────────────────────────
#
# Configure only. find_package IS the assertion; nothing needs compiling.
#
# Nothing harmful leaks in from our own build: CMAKE_TOOLCHAIN_FILE and the
# -Werror in CMAKE_CXX_FLAGS are cache entries, and `cmake -P` reads no cache.
# Deliberately NOT passed:
#
#   CMAKE_BUILD_TYPE — the generated glyphcadeTargets.cmake globs and includes
#   every glyphcadeTargets-<config>.cmake regardless of the consumer's build
#   type, so there is nothing to match.
#
#   CMAKE_CXX_COMPILER — forcing ours would couple this check to
#   cmake/toolchain/clang.cmake. A consumer uses whatever it has.
#
# The two find-registry switches ARE passed: find_package in CONFIG mode
# consults ~/.cmake/packages/<name>/, which any other project on the box can
# write. That is a route to a permanently-green check that has nothing to do
# with this build. Note this is deliberately narrower than switching off system
# prefixes wholesale — termforge may legitimately live in /usr/local, and case 4
# below is the guard that covers a stray system install of ourselves.

set(_consumer_args
  "-DCHECK_PROJECT=${PROJECT}"
  "-DCMAKE_PREFIX_PATH=${_prefix}"
  "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF"
  "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF"
)

# ⚠ Forward termforge's location when — and only when — it is real. On a machine
# where `find_package(termforge 0.57.14 QUIET CONFIG)` in cmake/deps/termforge.cmake
# SUCCEEDS, the FetchContent branch never runs, so termforge_INSTALL is never
# set and the scratch prefix contains no lib/cmake/termforge/ to find. Without
# this the consumer would go red for a reason that has nothing to do with the
# floor, and that is the first wrong diagnosis a future reader will reach for.
if (TERMFORGE_DIR AND NOT TERMFORGE_DIR MATCHES "NOTFOUND$")
  list(APPEND _consumer_args "-Dtermforge_DIR=${TERMFORGE_DIR}")
endif ()

execute_process(
  COMMAND ${CMAKE_COMMAND} -S "${_src}" -B "${_bin}" ${_consumer_args}
  RESULT_VARIABLE _crc
  OUTPUT_VARIABLE _cout
  ERROR_VARIABLE  _cerr
)

# message(STATUS) goes to stdout and FATAL_ERROR to stderr, so the markers and
# the diagnosis live in different variables. Search and report the pair.
set(_clog "${_cout}${_cerr}")

if (NOT _crc EQUAL 0)
  message(FATAL_ERROR
    "term-game#46: a consumer cannot resolve the installed package (exit ${_crc}).\n"
    "This is the failure cmake/deps/termforge.cmake warns about — a package that "
    "resolves in our tree and nowhere else. The usual cause is that the floor in "
    "cmake/project-config.cmake.in disagrees with the pin in "
    "cmake/deps/termforge.cmake; termforge's version file is SameMinorVersion, so "
    "a stale minor on either side stops matching entirely.\n"
    "If the message below is about termforge rather than ${PROJECT}, check "
    "TERMFORGE_DIR too: when termforge is found on the system, the FetchContent "
    "branch never runs and nothing installs it into the scratch prefix.\n"
    "Consumer output follows.\n"
    "──────── stdout ────────\n${_cout}\n"
    "──────── stderr ────────\n${_cerr}")
endif ()

# ── 4. Assert the configure that succeeded was the one we meant ───────────────
#
# Exit 0 alone is not enough, and the reasons are not hypothetical.

# A missing marker means the generated consumer did not run the line that emits
# it — a rotted check, not a pass. Same stance as check_export.cmake's "no
# Targets file" branch.
foreach (_marker
    "CONSUMED-PROJECT-VERSION" "CONSUMED-PROJECT-DIR"
    "CONSUMED-TERMFORGE-VERSION" "CONSUMED-TERMFORGE-DIR")
  if (NOT _clog MATCHES "${_marker}=")
    message(FATAL_ERROR
      "the consumer configured cleanly but never printed ${_marker}. This check "
      "has rotted: it is reporting success without having observed anything.\n"
      "${_clog}")
  endif ()
endforeach ()

string(REGEX MATCH  "CONSUMED-PROJECT-VERSION=([^\n\r]*)"   _m "${_clog}")
set(_got_version "${CMAKE_MATCH_1}")
string(REGEX MATCH  "CONSUMED-PROJECT-DIR=([^\n\r]*)"       _m "${_clog}")
set(_got_dir "${CMAKE_MATCH_1}")
string(REGEX MATCH  "CONSUMED-TERMFORGE-VERSION=([^\n\r]*)" _m "${_clog}")
set(_got_tf_version "${CMAKE_MATCH_1}")
string(REGEX MATCH  "CONSUMED-TERMFORGE-DIR=([^\n\r]*)"     _m "${_clog}")
set(_got_tf_dir "${CMAKE_MATCH_1}")

# ⚠ THE vacuity guard. Nothing stops somebody running `cmake --install build`
# into /usr/local once; from then on this test would pass forever no matter what
# the source tree says, because find_package would be answering from there. The
# only thing that distinguishes "resolved the package we just built" from
# "resolved a package" is where the config was found.
string(FIND "${_got_dir}" "${_prefix}" _at)
if (NOT _at EQUAL 0)
  message(FATAL_ERROR
    "the consumer resolved ${PROJECT} from ${_got_dir}, which is outside the "
    "scratch prefix ${_prefix}. It answered from a system install, an entry in "
    "CMAKE_PREFIX_PATH in the environment, or another prefix — so this run "
    "verified somebody else's package, not this build's. That is a vacuous pass "
    "and is treated as a failure.")
endif ()

# And that it is THIS build. Also catches the shallow-clone case cmake/install.cmake
# warns about, where the version degrades to 0.0.0 and the package is still
# perfectly resolvable.
if (NOT _got_version VERSION_EQUAL EXPECT_VERSION)
  message(FATAL_ERROR
    "the consumer resolved ${PROJECT} ${_got_version} but this build is "
    "${EXPECT_VERSION}. The config under test did not come from this build, or "
    "the version was derived differently at install time (a shallow clone makes "
    "`git describe` degrade — see cmake/install.cmake).")
endif ()

# ── 5. The floor line itself ──────────────────────────────────────────────────
#
# Read the INSTALLED config, not cmake/project-config.cmake.in. The .in is a
# template; the installed file is the artefact the consumer just executed. It
# also avoids a false red: configure_package_config_file() runs at CONFIGURE
# time, so editing the .in without re-running cmake leaves the old config in
# place, and a check reading the source would complain about a floor no consumer
# can see.
#
# ⚠ Be clear about what this buys, because it is easy to overrate. termforge's
# version file is SameMinorVersion, so a SUCCESSFUL find_dependency(termforge
# X.Y.Z) already implies the resolved version's major.minor is X.Y — the
# comparison below cannot fire while step 3 passes. Both half-bump directions
# are already caught by resolution alone: raise the floor only here and the
# request outruns the install; raise it only in cmake/deps/termforge.cmake and
# SameMinorVersion rejects the newer install against the stale request. Either
# way step 3 goes red first.
#
# What this step uniquely catches is the floor line being DELETED or stripped of
# its version — `find_dependency(termforge)` resolves against anything, forever,
# and no amount of consumer-configuring will ever notice. It is also the guard
# that would fire if termforge changed its COMPATIBILITY mode out from under us.
# It is not a second opinion on the floor's value; do not read it as one.
#
# For the same reason a text-vs-text comparison against the pin in
# cmake/deps/termforge.cmake would add nothing. Resolution covers it.

# Built from the directory the consumer REPORTED, not from a hand-assembled
# ${_prefix}/lib/cmake/${PROJECT}. CMAKE_INSTALL_LIBDIR is not always `lib` —
# GNUInstallDirs makes it arch-qualified on Debian derivatives — so guessing the
# path would turn a layout difference into a spurious failure. ${PROJECT}_DIR is
# by definition the directory holding the config that was just executed, and
# step 4 has already established it is inside the scratch prefix.
set(_installed_cfg "${_got_dir}/${PROJECT}Config.cmake")
if (NOT EXISTS "${_installed_cfg}")
  message(FATAL_ERROR
    "${_installed_cfg} does not exist, yet the consumer reported resolving "
    "${PROJECT} from ${_got_dir}. Either the package config is named something "
    "else or it was removed between the two steps.")
endif ()

file(READ "${_installed_cfg}" _cfg_text)
string(REGEX MATCHALL "find_dependency\\(termforge [0-9][0-9.]*\\)" _floors "${_cfg_text}")
list(LENGTH _floors _n)

if (NOT _n EQUAL 1)
  message(FATAL_ERROR
    "expected exactly one versioned `find_dependency(termforge <ver>)` in "
    "${_installed_cfg}, found ${_n}.\n"
    "Zero means the floor was deleted or stripped of its version — and a "
    "`find_dependency(termforge)` with no version resolves against ANY termforge, "
    "which no amount of consumer-configuring can detect. That is the one failure "
    "this branch exists to catch. See cmake/project-config.cmake.in.")
endif ()

list(GET _floors 0 _floor_line)
string(REGEX MATCH "([0-9][0-9.]*)" _m "${_floor_line}")
set(_floor "${CMAKE_MATCH_1}")

if (NOT _floor MATCHES "^([0-9]+)\\.([0-9]+)")
  message(FATAL_ERROR "unparseable termforge floor: '${_floor_line}'")
endif ()
set(_floor_mm "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}")

if (NOT _got_tf_version MATCHES "^([0-9]+)\\.([0-9]+)")
  message(FATAL_ERROR "unparseable resolved termforge version: '${_got_tf_version}'")
endif ()
set(_got_mm "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}")

if (NOT _floor_mm STREQUAL _got_mm)
  message(FATAL_ERROR
    "the consumer's floor asks for termforge ${_floor} but resolved "
    "${_got_tf_version}. Under SameMinorVersion that should have been impossible, "
    "so termforge's COMPATIBILITY mode has changed and every assumption in "
    "cmake/project-config.cmake.in about the ABI floor needs re-reading.")
endif ()

# ── 6. A config without targets is rejected on purpose ───────────────────────
#
# The guard at the top of project-config.cmake.in exists for two real paths.
# Exercise BOTH: the generated config in the build tree (this project exports
# only at install time), and a real install configured with BUILD_LIB=OFF. They
# reach the same branch from different build shapes, and the second prevents an
# install-rule change from making the first a convincing but incomplete proxy.

function(_expect_no_targets _label _package_dir _consumer_bin)
  file(REMOVE_RECURSE "${_consumer_bin}")

  execute_process(
    COMMAND ${CMAKE_COMMAND} -S "${_src}" -B "${_consumer_bin}"
            "-DCHECK_PROJECT=${PROJECT}"
            "-D${PROJECT}_DIR=${_package_dir}"
            "-Dtermforge_DIR=${_got_tf_dir}"
            "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF"
            "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF"
    RESULT_VARIABLE _nrc
    OUTPUT_VARIABLE _nout
    ERROR_VARIABLE  _nerr
  )
  set(_nlog "${_nout}${_nerr}")

  if (_nrc EQUAL 0)
    message(FATAL_ERROR
      "${_label}: a config with no exported targets resolved successfully.\n"
      "Consumer output follows.\n${_nlog}")
  endif ()

  # CMake paragraph-wraps a package's NOT_FOUND_MESSAGE to the output width.
  # Match the words, not whichever newline positions this CMake version chose.
  string(REGEX REPLACE "[ \t\r\n]+" " " _nlog_flat "${_nlog}")
  set(_guard_text "holds a package config but no exported targets beside it")
  string(FIND "${_nlog_flat}" "${_guard_text}" _guard_at)
  if (_guard_at EQUAL -1)
    message(FATAL_ERROR
      "${_label}: the consumer failed, but not through the no-targets guard. "
      "Expected '${_guard_text}'. An exit-code-only assertion would pass on "
      "CMake's generic missing-include error and is deliberately insufficient.\n"
      "Consumer output follows.\n${_nlog}")
  endif ()

  string(TOLOWER "${_nlog_flat}" _nlog_lower)
  string(FIND "${_nlog_lower}" "include could not find requested file" _include_at)
  if (NOT _include_at EQUAL -1)
    message(FATAL_ERROR
      "${_label}: the intended guard ran, but configuration also fell through "
      "to CMake's generic missing-include failure. The guard must return.\n"
      "Consumer output follows.\n${_nlog}")
  endif ()

  # The guard prints CMAKE_CURRENT_LIST_DIR in its own message. Requiring the
  # intended directory in the log prevents a stray installed package from
  # supplying the right words and turning this into a vacuous pass.
  string(FIND "${_nlog_flat}" "${_package_dir}" _dir_at)
  if (_dir_at EQUAL -1)
    message(FATAL_ERROR
      "${_label}: the expected guard text appeared, but it did not name the "
      "package directory under test (${_package_dir}).\n${_nlog}")
  endif ()

  message(STATUS "${_label}: rejected without exported targets: CLEAN")
endfunction()

# The generated build-tree config is present by design; the exported targets
# are absent by design. Assert both preconditions before treating its failure as
# evidence about the guard.
if (NOT EXISTS "${BUILD_DIR}/${PROJECT}Config.cmake")
  message(FATAL_ERROR
    "build-tree arm needs ${BUILD_DIR}/${PROJECT}Config.cmake, but it is absent")
endif ()
if (EXISTS "${BUILD_DIR}/${PROJECT}Targets.cmake")
  message(FATAL_ERROR
    "build-tree arm is invalid: ${BUILD_DIR}/${PROJECT}Targets.cmake exists")
endif ()
_expect_no_targets("build-tree config" "${BUILD_DIR}" "${_build_bin}")

# Configure a second copy with no library, executable or tests. It resolves
# termforge from the full scratch install made above (or the same explicit
# system termforge the parent build used), so this arm has no download and no
# dependency build. Configure + install is enough: with no glyphcade library
# there is no glyphcade target to compile and no Targets file to install.
set(_nolib_args
  "-D${PROJECT}_BUILD_LIB=OFF"
  "-D${PROJECT}_BUILD_BIN=OFF"
  "-D${PROJECT}_TESTS=OFF"
  "-D${PROJECT}_INSTALL=ON"
  "-DGLYPHCADE_WITH_AUDIO=OFF"
  "-DCMAKE_PREFIX_PATH=${_prefix}"
  "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF"
  "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF"
)
if (TERMFORGE_DIR AND NOT TERMFORGE_DIR MATCHES "NOTFOUND$")
  list(APPEND _nolib_args "-Dtermforge_DIR=${TERMFORGE_DIR}")
endif ()

execute_process(
  COMMAND ${CMAKE_COMMAND} -S "${SOURCE_DIR}" -B "${_nolib_build}" ${_nolib_args}
  RESULT_VARIABLE _ncrc
  OUTPUT_VARIABLE _ncout
  ERROR_VARIABLE  _ncerr
)
if (NOT _ncrc EQUAL 0)
  message(FATAL_ERROR
    "no-library configure failed (${_ncrc}):\n${_ncout}\n${_ncerr}")
endif ()

execute_process(
  COMMAND ${CMAKE_COMMAND} --install "${_nolib_build}" --prefix "${_nolib_prefix}"
  RESULT_VARIABLE _nirc
  OUTPUT_VARIABLE _niout
  ERROR_VARIABLE  _nierr
)
if (NOT _nirc EQUAL 0)
  message(FATAL_ERROR
    "no-library install failed (${_nirc}):\n${_niout}\n${_nierr}")
endif ()

file(GLOB_RECURSE _nolib_configs LIST_DIRECTORIES FALSE
  "${_nolib_prefix}/*/${PROJECT}Config.cmake")
list(LENGTH _nolib_configs _nolib_config_count)
if (NOT _nolib_config_count EQUAL 1)
  message(FATAL_ERROR
    "no-library install should contain exactly one ${PROJECT}Config.cmake, "
    "found ${_nolib_config_count}: ${_nolib_configs}")
endif ()
list(GET _nolib_configs 0 _nolib_config)
get_filename_component(_nolib_config_dir "${_nolib_config}" DIRECTORY)
if (EXISTS "${_nolib_config_dir}/${PROJECT}Targets.cmake")
  message(FATAL_ERROR
    "no-library arm is invalid: ${_nolib_config_dir}/${PROJECT}Targets.cmake exists")
endif ()
_expect_no_targets("no-library install" "${_nolib_config_dir}" "${_nolib_bin}")

# On success only. A failure leaves all seven directories behind on purpose —
# the generated consumers, their caches and the prefixes are the evidence, and
# check_export.cmake does the same. Keep this list paired with the initial clean.
file(REMOVE_RECURSE
  "${_prefix}" "${_src}" "${_bin}" "${_build_bin}"
  "${_nolib_build}" "${_nolib_prefix}" "${_nolib_bin}")

message(STATUS
  "consumer matrix resolves ${PROJECT} ${_got_version} with termforge "
  "${_got_tf_version} (floor ${_floor}) and rejects both targetless configs: CLEAN")
