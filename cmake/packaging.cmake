# ── CPack: the release artifacts ──────────────────────────────────────────────
# There was no way to install glyphcade except by building it (#15). This turns
# the install rules in cmake/install.cmake — already proven resolvable by the
# consumer-resolves ctest — into a .deb, an .rpm and a .tar.gz that CI builds,
# inspects and attaches to a release.
#
# ── What ships, and what does NOT ─────────────────────────────────────────────
# The install tree is 99 files, and only ONE of them is what a player wants.
# The rest is the exported CMake package: nine static archives, 79 headers, two
# lib/cmake/ trees. Those exist for `find_package(glyphcade)`, they are a real
# and tested feature, and they have no runtime role whatsoever — a static
# archive is consumed at link time, and bin/glyphcade already contains the code.
#
#   .deb / .rpm   the `runtime` component ONLY: the binary and its licence
#                 notices. What you install to play the game.
#   .tar.gz       the WHOLE install tree, every component, unfiltered. Where
#                 the exported package lives for anyone who wants to link it.
#
# ⚠ So there is deliberately no glyphcade-dev / glyphcade-devel package. The
# convention exists (and #15 offered it) but a -dev package is a promise to keep
# static archives ABI-stable for third parties, and nothing here has made that
# promise. Shipping them inside the tarball says the same thing honestly: they
# are available, they are not a supported interface. Revisit if somebody
# actually consumes the export.
#
# ── Where this is included from ───────────────────────────────────────────────
# The root CMakeLists, behind `PROJECT_IS_TOP_LEVEL AND ${PROJECT_NAME}_INSTALL`,
# after cmake/install.cmake — every install() must have registered its component
# before the COMPONENTS assertion below can read them.
#
# ⚠ The gate needs BOTH conditions, and _INSTALL alone is not enough even though
# it defaults to PROJECT_IS_TOP_LEVEL. A consumer that genuinely ships us turns
# it back ON (exactly what cmake/deps/termforge.cmake does to termforge), and in
# that configuration include(CPack) would write CPackConfig.cmake into THEIR
# binary directory, on top of theirs. CPACK_* variables are global and that path
# is not project-scoped, so this is the only place the collision can be stopped.

include(GNUInstallDirs)
# cpack_add_component() lives here. CPack.cmake includes this module itself, but
# only when it runs — and it runs LAST in this file, long after the components
# below are declared. Without this line those calls are an unknown-command error.
include(CPackComponent)

# ── Identity ──────────────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME    ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR  "gobha-me")
# Mandatory for DEB — CPackDeb fails without a maintainer.
set(CPACK_PACKAGE_CONTACT "gobha-me <noreply@github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gobha-me/${PROJECT_NAME}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A TUI arcade suite for the terminal")
set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE.md)

# ── Version ───────────────────────────────────────────────────────────────────
# PROJECT_VERSION comes from `git describe --tags` (cmake/version.cmake).
set(CPACK_PACKAGE_VERSION       ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})

# ⚠ A package built with no reachable tags is versioned 0.0.0 and must never be
# published. This is a WARNING and not a FATAL_ERROR on purpose: a 0.0.0 tree is
# a legitimate thing to BUILD (a source zip, a fresh shallow clone), and
# cmake/version.cmake treats the fallback as legitimate too. The refusal belongs
# where a package actually exists — cmake/check_package.cmake fails on it
# unconditionally, and nothing uploads before that check passes.
if (PROJECT_VERSION VERSION_EQUAL 0.0.0)
  message(WARNING
    "packaging is configured at version 0.0.0 — `git describe --tags` found "
    "nothing, so any package built from this tree is NOT publishable. Causes, "
    "in the order they happen: no .git at all (a source tarball, or "
    "actions/checkout falling back to a tarball download because git was not "
    "installed first); a shallow clone (CI needs fetch-depth: 0); or a "
    "workspace git refuses to read (CI needs `git config --global --add "
    "safe.directory`). See cmake/version.cmake.")
endif ()

# PROJECT_VERSION keeps only MAJOR.MINOR.PATCH — `git describe`'s -<N>-g<hash>
# is parsed out into VERSION_TWEAK, so every build between two tags claims the
# older tag's version and two different trees would produce identically-named
# packages. The commit distance goes in the packaging RELEASE field, which both
# formats have: 7 commits past v0.19.0 is 0.19.0-7, which sorts above 0.19.0-1
# and below 0.20.0-1. A tagged build has TWEAK 0 and gets the conventional -1.
#
# ⚠ Debian's release field is ^[A-Za-z0-9.+~]+$ and RPM forbids '-' in both
# Version and Release, so a dirty tree appends `.dirty` — never `-dirty`, which
# would be rejected by one and silently split the field in the other.
set(_pkg_release "${VERSION_TWEAK}")
if (NOT _pkg_release OR _pkg_release STREQUAL "0")
  set(_pkg_release "1")
endif ()
if (VERSION_DIRTY)
  string(APPEND _pkg_release ".dirty")
endif ()
set(CPACK_DEBIAN_PACKAGE_RELEASE ${_pkg_release})
set(CPACK_RPM_PACKAGE_RELEASE    ${_pkg_release})

# ── Generators ────────────────────────────────────────────────────────────────
# Detected, not assumed — the same stance cmake/audio.cmake takes: what is
# present here works, and CI asks for all three EXPLICITLY (`cpack -G
# "DEB;RPM;TGZ"`) so a missing tool is a red job rather than a quietly smaller
# release. debian:trixie ships dpkg-deb but neither rpm nor file.
#
# ⚠ `file` is not optional for DEB. CPACK_DEBIAN_PACKAGE_SHLIBDEPS below runs
# dpkg-shlibdeps, and CPackDeb raises a FATAL_ERROR when the file utility is
# absent — so a box with dpkg-deb and no file must not claim the DEB generator.
set(CPACK_GENERATOR "TGZ")
find_program(DPKG_DEB_EXECUTABLE dpkg-deb)
find_program(FILE_EXECUTABLE     file)
find_program(RPMBUILD_EXECUTABLE rpmbuild)
if (DPKG_DEB_EXECUTABLE AND FILE_EXECUTABLE)
  list(APPEND CPACK_GENERATOR "DEB")
endif ()
if (RPMBUILD_EXECUTABLE)
  list(APPEND CPACK_GENERATOR "RPM")
endif ()
message(STATUS
  "CPack generators: ${CPACK_GENERATOR} (dpkg-deb=${DPKG_DEB_EXECUTABLE} "
  "file=${FILE_EXECUTABLE} rpmbuild=${RPMBUILD_EXECUTABLE})")

# ⚠ Do NOT configure this project with -DCMAKE_INSTALL_PREFIX=/usr to "make the
# packages come out right". GNUInstallDirs switches CMAKE_INSTALL_LIBDIR to
# lib/<arch-triplet> at exactly that prefix on a Debian-family box; the path is
# baked in at configure time and CPack cannot re-derive it per generator, so the
# RPM would ship its cmake config in a directory no Fedora find_package searches.
# Leave the default prefix and let each generator re-root to /usr itself.
if (CMAKE_INSTALL_LIBDIR MATCHES "/")
  message(WARNING
    "CMAKE_INSTALL_LIBDIR is '${CMAKE_INSTALL_LIBDIR}' — a multiarch path, which "
    "GNUInstallDirs derives from CMAKE_INSTALL_PREFIX=/usr. Packages built from "
    "this configuration are Debian-specific; the RPM will be wrong. Configure "
    "with the default prefix instead.")
endif ()

# ── Components ────────────────────────────────────────────────────────────────
# Derived from the build, then ASSERTED against what this file expects.
#
# Neither half is sufficient alone. Hardcoding the list would silently drop an
# install() rule that named no COMPONENT — those register as `Unspecified`, and
# their files would simply vanish from every package. Deriving it without the
# assertion would silently ship whatever turned up, including a third package
# nobody meant to publish. Together they turn either drift into a named
# configure-time failure, which is what cmake/install.cmake's header promises.
get_cmake_property(_components COMPONENTS)
list(SORT _components)
set(_expected_components dev runtime)
if (NOT _components STREQUAL _expected_components)
  message(FATAL_ERROR
    "install components are '${_components}', expected '${_expected_components}'.\n"
    "`Unspecified` in that list means an install() call in this project names no "
    "COMPONENT. Every one of them needs `COMPONENT runtime` (the binary and its "
    "licence notices) or `COMPONENT dev` (everything else) — see "
    "cmake/install.cmake, where the two are defined and the reason each rule "
    "picks one is written at its site.\n"
    "termforge's own install() calls name no component either; they are covered "
    "by the scoped CMAKE_INSTALL_DEFAULT_COMPONENT_NAME around the "
    "cmake/dependencies.cmake include in the root CMakeLists.")
endif ()

set(CPACK_COMPONENTS_ALL ${_components})
# One package per component, no grouping. Only matters for the archive
# generator, which has component install OFF and takes them all in one file.
set(CPACK_COMPONENTS_GROUPING IGNORE)

cpack_add_component(runtime
  DISPLAY_NAME "glyphcade"
  DESCRIPTION  "The glyphcade arcade binary and its licence notices.")
cpack_add_component(dev
  DISPLAY_NAME "glyphcade development files"
  DESCRIPTION  "Static libraries, headers and the CMake package config, plus the
bundled TermForge they link against. Shipped in the source tarball only.")

# ⚠ Deliberately no `DEPENDS runtime` on the dev component: CPack turns a
# component dependency into a package Depends, and a static archive does not
# need the game binary installed in order to be linkable.

# ── The DEB and RPM carry the runtime component only ──────────────────────────
# CPACK_COMPONENTS_ALL is global, so the narrowing has to happen per generator —
# and CPACK_PROJECT_CONFIG_FILE is the only hook cpack evaluates once per
# generator, with CPACK_GENERATOR set to the one it is currently running. A plain
# set() here would apply to the archive too and gut the tarball.
set(CPACK_PROJECT_CONFIG_FILE ${CMAKE_CURRENT_LIST_DIR}/packaging-per-generator.cmake)

# ── DEB ───────────────────────────────────────────────────────────────────────
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)   # <name>_<version>-<release>_<arch>.deb
set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_DEBIAN_PACKAGE_SECTION  "games")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")

# The runtime dependency, DERIVED rather than written down. dpkg-shlibdeps reads
# the binary's DT_NEEDED entries and resolves each to the package that owns it on
# the build box, so the audio dependency comes out with whatever name the target
# distribution actually uses — which is the whole reason not to hardcode it
# (librtaudio6 on Debian, rtaudio on Fedora, and RPM derives its own below).
#
# ⚠ Expect FIVE names, not the twenty `ldd` prints: readelf -d shows
# librtaudio.so.6, libstdc++.so.6, libm.so.6, libgcc_s.so.1, libc.so.6.
# dpkg-shlibdeps reads DT_NEEDED, not the transitive closure — jack, pulse,
# alsa, dbus and db arrive through librtaudio6's own Depends. An assertion
# written against the `ldd` output would be red on a correct package.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_DEBIAN_RUNTIME_DESCRIPTION
    "A TUI arcade suite for the terminal
 Five games — Minesweeper, 2048, Snake, Tetris and Sokoban — behind one
 selector, playable with mouse or keyboard. Renders on anything from a 7-bit
 no-colour terminal upward, using colour, Unicode and rounded borders when the
 terminal reports it can.")

# ── RPM ───────────────────────────────────────────────────────────────────────
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)      # <name>-<version>-<release>.<arch>.rpm
# Without this the runtime rpm would be named glyphcade-runtime; MAIN_COMPONENT
# is what lets one component keep the bare project name.
set(CPACK_RPM_MAIN_COMPONENT "runtime")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")      # matches LICENSE.md
set(CPACK_RPM_PACKAGE_GROUP   "Amusements/Games")
set(CPACK_RPM_PACKAGE_URL     "${CPACK_PACKAGE_HOMEPAGE_URL}")

# The RPM half of the same decision: rpmbuild derives Requires from the ELF, so
# the audio dependency reads librtaudio.so.6()(64bit) rather than a distro
# package name we guessed. Both are rpmbuild's defaults and are set explicitly
# anyway — a default is not a decision anyone reviewing this file can see.
set(CPACK_RPM_PACKAGE_AUTOREQ  ON)
set(CPACK_RPM_PACKAGE_AUTOPROV ON)

# ── TGZ ───────────────────────────────────────────────────────────────────────
# CPACK_ARCHIVE_COMPONENT_INSTALL stays OFF: the tarball is the whole install
# tree in one file. That is what makes it the control for the component split —
# cmake/check_package.cmake asserts the .deb's contents are a strict subset of
# the tarball's, so a file that fell out of every component shows up as a
# difference rather than as an absence nothing measures.
#
# ⚠ The release field belongs in this name too. The archive generator reads
# neither CPACK_DEBIAN_PACKAGE_RELEASE nor CPACK_RPM_PACKAGE_RELEASE, so a name
# built from CPACK_PACKAGE_VERSION alone would be identical for every build
# between two tags — which is the exact collision the release field was
# introduced above to prevent, left unfixed for the one artifact that is not a
# distro package. Two different trees would produce two files called
# glyphcade-0.19.0-Linux-x86_64.tar.gz, and the second would overwrite the
# first in the output directory with nothing reporting it.
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${_pkg_release}-Linux-${CMAKE_SYSTEM_PROCESSOR}")

# Safe for the archives: CMake emits a strip step only for executables and shared
# libraries (grep CMAKE_INSTALL_DO_STRIP in the generated cmake_install.cmake —
# it wraps bin/glyphcade and nothing else), so the static libraries in the
# tarball keep their symbols and stay linkable.
set(CPACK_STRIP_FILES ON)

# ⚠ CPACK_PACKAGING_INSTALL_PREFIX is deliberately NOT set. Each generator has
# its own default — /usr for DEB and RPM, empty for archives — and setting it
# globally would bake a usr/ prefix into the tarball, which should unpack
# relative to wherever it lands.

# ⚠ include(CPack) MUST BE THE LAST LINE OF THIS FILE. It configures
# CPackConfig.cmake at the moment it runs, so every CPACK_* set and every
# cpack_add_component() call placed after it is silently ignored — the change
# looks made, the packages come out unchanged, and nothing fails.
include(CPack)
