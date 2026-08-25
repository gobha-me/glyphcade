# Asserts that the release artifacts CPack produces are the ones we meant to
# publish (#15).
#
# Run as a ctest, or by hand, or from CI before an upload:
#   cmake -DBUILD_DIR=<build> [-DPKG_DIR=<dir>] [-DGENERATORS=DEB;TGZ] \
#         [-DEXPECT_VERSION=<ver>] [-DREQUIRE_AUDIO=ON] -P cmake/check_package.cmake
#
# With no PKG_DIR it builds the packages itself into <build>/package-check. With
# one it inspects artifacts already built — the mode CI uses, so that the bytes
# that get uploaded are the bytes that were judged, rather than a second set
# built by the checker and assumed identical.
#
# ── What this is for ──────────────────────────────────────────────────────────
#
# A package is the one artifact nobody looks inside before it reaches a stranger.
# `cpack` exits 0 on a .deb containing the wrong files, the wrong version, or no
# dependency at all; the failure surfaces at `apt install` on somebody else's
# machine, weeks later. Every assertion below is one of those.
#
# Its sibling is cmake/check_consumer.cmake, which judges whether the INSTALL
# TREE resolves. This one judges whether the PACKAGES built from that tree are
# publishable. Neither subsumes the other: consumer-resolves stayed green
# throughout the development of this file, including while the .deb was empty.

cmake_minimum_required(VERSION 3.28)

if (NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "check_package.cmake needs -DBUILD_DIR=")
endif ()
if (NOT DEFINED GENERATORS)
  set(GENERATORS "DEB;TGZ")
endif ()

# ── 1. Get the artifacts ──────────────────────────────────────────────────────

if (DEFINED PKG_DIR)
  set(_pkg "${PKG_DIR}")
else ()
  set(_pkg "${BUILD_DIR}/package-check")
  # Remove first. A leftover .deb from a previous run is indistinguishable from
  # one this run produced, and would let a cpack failure read as a pass.
  file(REMOVE_RECURSE "${_pkg}")

  get_filename_component(_cmake_bin "${CMAKE_COMMAND}" DIRECTORY)
  execute_process(
    COMMAND "${_cmake_bin}/cpack" --config "${BUILD_DIR}/CPackConfig.cmake"
            -G "${GENERATORS}" -B "${_pkg}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err
  )
  if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "cpack -G ${GENERATORS} failed (${_rc}):\n${_out}\n${_err}")
  endif ()
endif ()

file(GLOB _debs "${_pkg}/*.deb")
file(GLOB _rpms "${_pkg}/*.rpm")
file(GLOB _tgzs "${_pkg}/*.tar.gz")

# ⚠ THE anti-vacuity gate, and it comes before every other assertion on purpose.
# Every check below is a loop or a "does this string appear" over the artifact
# list — all of which pass trivially when that list is empty. A checker that
# inspected nothing must never print CLEAN. Same stance as check_export.cmake's
# "no Targets file" branch and check_consumer.cmake's missing-marker branch.
if (NOT _debs AND NOT _rpms AND NOT _tgzs)
  message(FATAL_ERROR
    "no packages found in ${_pkg}. Nothing was inspected, so nothing is "
    "verified — this is a failure, not an empty pass.\n"
    "If PKG_DIR was passed, cpack was expected to have run already and to have "
    "written there.")
endif ()

# And that each generator ASKED for actually produced something. Without this,
# dropping RPM from a CI job's -G list would quietly stop testing rpm forever,
# and the run would still say CLEAN.
foreach (_g IN LISTS GENERATORS)
  if (_g STREQUAL "DEB" AND NOT _debs)
    message(FATAL_ERROR "generator DEB was requested but no .deb exists in ${_pkg}")
  elseif (_g STREQUAL "RPM" AND NOT _rpms)
    message(FATAL_ERROR "generator RPM was requested but no .rpm exists in ${_pkg}")
  elseif (_g STREQUAL "TGZ" AND NOT _tgzs)
    message(FATAL_ERROR "generator TGZ was requested but no .tar.gz exists in ${_pkg}")
  endif ()
endforeach ()

# ── 2. Exactly one binary package per generator ───────────────────────────────
#
# Not a tidiness check. The whole shape of #15's answer is that the .deb and the
# .rpm carry the `runtime` component ONLY — the static archives, headers and
# cmake config go in the tarball, because they have no runtime role and a -dev
# package would be a promise of ABI stability nothing here has made. Two debs
# means CPACK_COMPONENTS_ALL was not narrowed and cmake/packaging-per-generator.cmake
# stopped being consulted.
list(LENGTH _debs _n_deb)
list(LENGTH _rpms _n_rpm)
list(LENGTH _tgzs _n_tgz)
if (_debs AND NOT _n_deb EQUAL 1)
  message(FATAL_ERROR
    "expected exactly one .deb, found ${_n_deb}: ${_debs}\n"
    "More than one means the runtime narrowing in "
    "cmake/packaging-per-generator.cmake did not apply and a dev package was "
    "built. See the header of cmake/packaging.cmake for why there is not one.")
endif ()
if (_rpms AND NOT _n_rpm EQUAL 1)
  message(FATAL_ERROR "expected exactly one .rpm, found ${_n_rpm}: ${_rpms}")
endif ()
if (_tgzs AND NOT _n_tgz EQUAL 1)
  message(FATAL_ERROR "expected exactly one .tar.gz, found ${_n_tgz}: ${_tgzs}")
endif ()

# ── 3. The version, which is the trap #15 named ───────────────────────────────
#
# PROJECT_VERSION comes from `git describe --tags`, and a build with no reachable
# tags reports 0.0.0 without failing anything. glyphcade-0.0.0-1.x86_64.rpm is
# the kind of artifact nobody notices until it is published, so the refusal is
# unconditional and lives here rather than at configure time — a 0.0.0 tree is a
# legitimate thing to BUILD (see the WARNING in cmake/packaging.cmake); it is
# not a legitimate thing to package.

set(_versions "")

if (_debs)
  find_program(DPKG_DEB_EXECUTABLE dpkg-deb REQUIRED)
  list(GET _debs 0 _deb)
  execute_process(COMMAND "${DPKG_DEB_EXECUTABLE}" -f "${_deb}" Version
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _deb_version OUTPUT_STRIP_TRAILING_WHITESPACE)
  # ⚠ Both halves are load-bearing, and the second is the subtle one. If
  # dpkg-deb fails — a truncated .deb, a stray non-deb file caught by the glob,
  # a dpkg-deb version skew — _deb_version is the empty string, and an empty
  # string does NOT match the 0.0.0 pattern below. The unconditional version
  # refusal would then be satisfied by a version that was never read, which is
  # the vacuous pass this whole file exists to be incapable of.
  if (NOT _rc EQUAL 0 OR _deb_version STREQUAL "")
    message(FATAL_ERROR
      "could not read the Version field of ${_deb} (exit ${_rc}, got "
      "'${_deb_version}'). The version gate below cannot run, so this is a "
      "failure rather than a version that passes by being absent.")
  endif ()
  list(APPEND _versions "${_deb_version}")
endif ()
if (_rpms)
  list(GET _rpms 0 _rpm)
  get_filename_component(_rpm_name "${_rpm}" NAME)
  # <name>-<version>-<release>.<arch>.rpm
  if (NOT _rpm_name MATCHES "^glyphcade-([0-9][^-]*)-")
    message(FATAL_ERROR "cannot read a version out of the rpm name '${_rpm_name}'")
  endif ()
  list(APPEND _versions "${CMAKE_MATCH_1}")
endif ()
if (_tgzs)
  list(GET _tgzs 0 _tgz)
  get_filename_component(_tgz_name "${_tgz}" NAME)
  # <name>-<version>-<release>-Linux-<arch>.tar.gz. The release field sits
  # between the version and -Linux-, so the version group must not be anchored
  # directly against it — see CPACK_PACKAGE_FILE_NAME in cmake/packaging.cmake.
  if (NOT _tgz_name MATCHES "^glyphcade-([0-9][0-9.]*)-.+-Linux-")
    message(FATAL_ERROR "cannot read a version out of the tarball name '${_tgz_name}'")
  endif ()
  list(APPEND _versions "${CMAKE_MATCH_1}")
endif ()

foreach (_v IN LISTS _versions)
  if (_v MATCHES "^0\\.0\\.0")
    message(FATAL_ERROR
      "a package is versioned '${_v}'. `git describe --tags` found no tag, so "
      "this artifact is NOT publishable.\n"
      "Causes, in the order they happen: no .git at all (a source tarball, or "
      "actions/checkout falling back to a tarball download because git was not "
      "installed before the checkout step); a shallow clone (CI needs "
      "fetch-depth: 0); or a workspace git refuses to read (CI needs `git config "
      "--global --add safe.directory \"$PWD\"`). See cmake/version.cmake.")
  endif ()
endforeach ()

# On the release path CI passes the tag, and there "merely stale" is as bad as
# 0.0.0 — a v0.20.0 release carrying 0.19.0 artifacts is a worse failure than one
# carrying none, because it looks fine.
if (DEFINED EXPECT_VERSION)
  # Reject the shape before comparing with it. VERSION_EQUAL on a non-numeric
  # string does not error — it coerces — so `-DEXPECT_VERSION=v0.20.0` (the tag
  # with its prefix left on, the obvious hand-invocation mistake) would compare
  # as 0 and fail every artifact with a message blaming the packages.
  if (NOT EXPECT_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
    message(FATAL_ERROR
      "EXPECT_VERSION is '${EXPECT_VERSION}', which is not a numeric version. "
      "Pass the version without a leading 'v' — CI derives it as "
      "\${GITHUB_REF#refs/tags/v}.")
  endif ()

  foreach (_v IN LISTS _versions)
    # Strip the packaging release field ("0.19.0-7.dirty" → "0.19.0") and compare
    # as VERSIONS. Interpolating EXPECT_VERSION into a regex instead would let
    # its dots match any character, so a 0.20.0 release would accept a package
    # versioned 0x20y0 — unlikely, but a false pass on the release path is the
    # worst possible place for one.
    string(REGEX REPLACE "-.*$" "" _upstream_v "${_v}")
    if (NOT _upstream_v VERSION_EQUAL EXPECT_VERSION)
      message(FATAL_ERROR
        "a package is versioned '${_v}' but this release is '${EXPECT_VERSION}'. "
        "The tag was not what `git describe` saw — check that the tag is "
        "annotated and that the checkout fetched it (fetch-depth: 0).")
    endif ()
  endforeach ()
endif ()

# ── 4. What is actually inside ────────────────────────────────────────────────
#
# Contents are read from the artifacts, never from the install manifest: the
# manifest is what the build INTENDED and the package is what a stranger GETS,
# and the component split is exactly the step between them where they can differ.

# The tarball first, because it is the control. It takes every component
# unfiltered (CPACK_ARCHIVE_COMPONENT_INSTALL is off), so it is the evidence
# that the build produced a full tree at all — without it, a .deb containing
# only the binary is indistinguishable from a build where everything else failed
# to install.
if (_tgzs)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E tar tzf "${_tgz}"
    OUTPUT_VARIABLE _tgz_list RESULT_VARIABLE _rc)
  if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "could not list ${_tgz}")
  endif ()

  # ⚠ string(FIND), not `if (... MATCHES ...)`. MATCHES takes a REGEX, and every
  # path below contains a `.` — which matches any character, so
  # "libglyphcade_lib.a" would be satisfied by "libglyphcade_libXa". That is a
  # false PASS, the one direction this file must never fail in.
  foreach (_want
      "bin/glyphcade"
      "lib/libglyphcade_lib.a"
      "lib/libtermforge.a"
      "lib/cmake/glyphcade/glyphcadeConfig.cmake"
      "lib/cmake/termforge/termforgeConfig.cmake"
      "include/glyphcade/arcade/shell.hpp")
    string(FIND "${_tgz_list}" "${_want}" _found_at)
    if (_found_at EQUAL -1)
      message(FATAL_ERROR
        "the tarball does not contain ${_want}.\n"
        "It is supposed to be the WHOLE install tree — every component, "
        "unfiltered — and it is what carries the exported CMake package now that "
        "the .deb and .rpm carry the runtime component only. A file missing here "
        "fell out of the build, not out of a component.")
    endif ()
  endforeach ()

  # ⚠ The tarball must NOT be prefixed usr/. Each generator has its own default
  # packaging prefix — /usr for DEB and RPM, empty for archives — and setting
  # CPACK_PACKAGING_INSTALL_PREFIX globally to "fix" the binary packages would
  # bake usr/ in here, where it does not belong: a tarball unpacks relative to
  # wherever it lands.
  if (_tgz_list MATCHES "/usr/bin/glyphcade")
    message(FATAL_ERROR
      "the tarball has a usr/ prefix. CPACK_PACKAGING_INSTALL_PREFIX has been "
      "set globally; it must be left to each generator's default. See the note "
      "at the end of cmake/packaging.cmake.")
  endif ()
endif ()

# The runtime package: the binary and its licence notices, and NOTHING else.
#
# ⚠ This is the assertion that makes the split real, and it is written as an
# exact set rather than a list of things that must be present. A "must contain
# bin/glyphcade" check would stay green if the whole install tree came along —
# which is the failure being guarded against, since that is what a .deb looks
# like when the component narrowing silently stops applying.
#
# ⚠ This list is the one place a SHARED build would need an edit, and
# cmake/install.cmake's promise that switching a target's type "needs no edit
# here" is true of that file and not of this one. A shared libglyphcade_lib.so
# is correctly filed COMPONENT runtime, would correctly land in the .deb, and
# would fail this exact-set comparison. That is the check doing its job — a
# package gaining a file is exactly what it watches for — but the fix in that
# case is to add the library here, not to loosen the comparison.
set(_expect_runtime
  "usr/bin/glyphcade"
  "usr/share/licenses/glyphcade/LICENSE.md"
  "usr/share/licenses/glyphcade/LICENSE.termforge.md"
  "usr/share/licenses/glyphcade/LICENSE.stb.md")

if (_debs)
  execute_process(COMMAND "${DPKG_DEB_EXECUTABLE}" -c "${_deb}"
    OUTPUT_VARIABLE _deb_raw RESULT_VARIABLE _rc)
  if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "could not list ${_deb}")
  endif ()

  # `dpkg-deb -c` is ls -l shaped. Keep regular files only — directories are
  # layout, not payload, and a symlink (which would start `l`) is neither.
  #
  # ⚠ Guard on _m, NOT on CMAKE_MATCH_1. string(REGEX MATCH) sets its output
  # variable to the empty string when nothing matches, but it does NOT clear
  # CMAKE_MATCH_1 — that keeps whatever the previous successful match left in
  # it. Testing CMAKE_MATCH_1 would therefore append the PREVIOUS file's path a
  # second time for any regular-file line the regex failed to parse, which shows
  # up as a duplicate in a set comparison and reads like a packaging fault.
  #
  # `(.+)$` rather than `([^ ]+)$` so a path containing a space survives. No
  # file here has one, which is exactly why the narrower spelling would have
  # gone unnoticed until somebody added one.
  set(_deb_files "")
  string(REPLACE "\n" ";" _deb_lines "${_deb_raw}")
  foreach (_line IN LISTS _deb_lines)
    if (_line MATCHES "^-")                       # regular file
      string(REGEX MATCH "\\./(.+)$" _m "${_line}")
      if (_m)
        list(APPEND _deb_files "${CMAKE_MATCH_1}")
      endif ()
    endif ()
  endforeach ()
  list(SORT _deb_files)

  set(_want_sorted ${_expect_runtime})
  list(SORT _want_sorted)
  if (NOT _deb_files STREQUAL _want_sorted)
    # Report the DIFFERENCE, not both sets. The failure mode being caught is a
    # package carrying the whole install tree, and printing it makes the one
    # line that explains why unreadable. Truncate the extras for the same reason.
    set(_extra ${_deb_files})
    list(REMOVE_ITEM _extra ${_want_sorted})
    set(_missing ${_want_sorted})
    list(REMOVE_ITEM _missing ${_deb_files})
    list(LENGTH _extra _n_extra)
    if (_n_extra GREATER 8)
      list(SUBLIST _extra 0 8 _extra)
      list(APPEND _extra "... and ${_n_extra} in total")
    endif ()
    message(FATAL_ERROR
      "the .deb does not contain what the runtime component is supposed to be.\n"
      "  should not be there (${_n_extra}): ${_extra}\n"
      "  missing:                          ${_missing}\n"
      "Extra files mean the runtime narrowing in "
      "cmake/packaging-per-generator.cmake stopped applying, or an install() "
      "rule was given COMPONENT runtime when it meant dev. A missing licence "
      "notice means a statically linked dependency's install rule did not run; "
      "see the licence section in cmake/install.cmake. Those notices are not "
      "optional because their code is inside this binary.")
  endif ()

  # And the .deb's payload really is a subset of the tarball's, which is what
  # makes the two checks above one check rather than two unrelated ones. A file
  # that exists in the .deb and not in the full tree would mean the package was
  # assembled from something other than this build.
  #
  # ⚠ The two are not directly comparable and must be normalised first. DEB
  # re-roots to /usr, so its paths read usr/bin/glyphcade; the archive generator
  # has no packaging prefix and wraps everything in a version-stamped directory,
  # so the same file reads glyphcade-0.19.0-Linux-x86_64/bin/glyphcade. Comparing
  # them raw fails on every file — which is what this check did when first
  # written, so it is at least demonstrably not vacuous.
  if (_tgzs)
    foreach (_f IN LISTS _deb_files)
      string(REGEX REPLACE "^usr/" "" _rel "${_f}")
      string(FIND "${_tgz_list}" "/${_rel}" _found_at)     # literal, see above
      if (_found_at EQUAL -1)
        message(FATAL_ERROR
          "${_f} is in the .deb but not in the tarball, which is supposed to be "
          "a superset of every component. The two artifacts did not come from "
          "the same install tree.")
      endif ()
    endforeach ()
  endif ()
endif ()

# ── 5. The audio dependency, with its control ─────────────────────────────────
#
# Decision 2 of #15: the packaged binary is built with audio, and the runtime
# dependency is DERIVED per generator rather than written down — dpkg-shlibdeps
# from DT_NEEDED for the .deb, rpmbuild's AUTOREQ for the .rpm — so each names
# whatever the target distribution calls rtaudio.
#
# ⚠ Read from the build's own cache, NOT from a flag the caller passes. A caller
# that can declare which arm to expect can also declare the wrong one, and this
# check would then confirm it. The cache is what the compiler actually saw.
set(_cache "${BUILD_DIR}/CMakeCache.txt")
if (NOT EXISTS "${_cache}")
  message(FATAL_ERROR "no CMakeCache.txt in ${BUILD_DIR} — cannot tell which audio arm built these packages")
endif ()
file(READ "${_cache}" _cache_text)
# ⚠ Anchored to a whole cache line, matching the `grep -q
# '^GLYPHCADE_WITH_AUDIO:BOOL=ON$'` step in .github/workflows/ci.yml exactly.
# An unanchored substring search decides the same fact by weaker evidence and
# can disagree with CI by construction: any cache entry whose name merely ENDS
# in GLYPHCADE_WITH_AUDIO — a prefixed superbuild's EXT_GLYPHCADE_WITH_AUDIO —
# would satisfy it, and the file also contains `//` help text for the option.
if (_cache_text MATCHES "(^|\n)GLYPHCADE_WITH_AUDIO:BOOL=ON(\r?\n|$)")
  set(_audio ON)
else ()
  set(_audio OFF)
endif ()

# REQUIRE_AUDIO turns "audio was OFF" into a failure rather than a satisfied
# negative. Without it, a package job that silently lost -DGLYPHCADE_WITH_AUDIO=ON
# — a container missing pkgconf is enough, since that is the only way
# cmake/audio.cmake finds rtaudio on Debian — would assert the ABSENCE of
# librtaudio and pass, and ship a mute arcade.
if (REQUIRE_AUDIO AND NOT _audio)
  message(FATAL_ERROR
    "REQUIRE_AUDIO was set but ${BUILD_DIR} was configured with "
    "GLYPHCADE_WITH_AUDIO=OFF. These packages would ship a silent arcade.\n"
    "On Debian, rtaudio is reachable only through pkg-config (librtaudio-dev "
    "installs no CMake config), so a container without pkgconf detects nothing "
    "and the option quietly defaults OFF. See cmake/audio.cmake.")
endif ()

# Both arms are asserted, and the negative one is worth something only because
# the positive one runs too: CI's five build arms are audio-OFF and its package
# job is audio-ON, so every push exercises both branches of this check.
if (_debs)
  execute_process(COMMAND "${DPKG_DEB_EXECUTABLE}" -f "${_deb}" Depends
    OUTPUT_VARIABLE _depends OUTPUT_STRIP_TRAILING_WHITESPACE)

  # The control: a Depends field that is EMPTY would satisfy "does not contain
  # librtaudio" while proving only that dpkg-shlibdeps never ran. libc6 is there
  # in both arms and is the evidence that dependency derivation happened at all.
  if (NOT _depends MATCHES "libc6")
    message(FATAL_ERROR
      "the .deb's Depends is '${_depends}', which does not mention libc6. "
      "dpkg-shlibdeps did not run or found nothing — so any conclusion about "
      "the audio dependency below would be vacuous. Check that "
      "CPACK_DEBIAN_PACKAGE_SHLIBDEPS is ON and that the `file` utility is "
      "installed (CPackDeb needs it).")
  endif ()

  if (_audio AND NOT _depends MATCHES "librtaudio")
    message(FATAL_ERROR
      "the build has audio ON but the .deb does not depend on librtaudio:\n"
      "  Depends: ${_depends}\n"
      "The binary would fail to start on a machine without it.")
  endif ()
  if (NOT _audio AND _depends MATCHES "librtaudio")
    message(FATAL_ERROR
      "the build has audio OFF but the .deb depends on librtaudio:\n"
      "  Depends: ${_depends}")
  endif ()
endif ()

if (_rpms)
  find_program(RPM_EXECUTABLE rpm)
  if (RPM_EXECUTABLE)
    execute_process(COMMAND "${RPM_EXECUTABLE}" -qp --requires "${_rpm}"
      OUTPUT_VARIABLE _requires ERROR_QUIET)
    if (NOT _requires MATCHES "libc\\.so")
      message(FATAL_ERROR
        "the .rpm's Requires does not mention libc.so, so AUTOREQ produced "
        "nothing and the audio conclusion below would be vacuous:\n${_requires}")
    endif ()
    if (_audio AND NOT _requires MATCHES "librtaudio\\.so")
      message(FATAL_ERROR
        "the build has audio ON but the .rpm does not require librtaudio.so:\n${_requires}")
    endif ()
    if (NOT _audio AND _requires MATCHES "librtaudio\\.so")
      message(FATAL_ERROR
        "the build has audio OFF but the .rpm requires librtaudio.so:\n${_requires}")
    endif ()
  else ()
    message(STATUS
      "check_package: rpm(1) not found — the .rpm was BUILT but its Requires "
      "were NOT inspected. Absent, not passing.")
  endif ()
endif ()

message(STATUS
  "packages CLEAN: version=${_versions} audio=${_audio} "
  "deb=${_n_deb} rpm=${_n_rpm} tgz=${_n_tgz}")
