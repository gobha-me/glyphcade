# ── CPACK_PROJECT_CONFIG_FILE: the one hook that runs PER GENERATOR ───────────
# Pointed at by cmake/packaging.cmake. cpack reads CPackConfig.cmake once, then
# includes this file again for each generator it was asked to run, with
# CPACK_GENERATOR set to that single generator. It is the only place a setting
# can differ between the .deb and the .tar.gz.
#
# ⚠ This runs inside cpack, NOT during the CMake configure. There is no project
# here — no PROJECT_NAME, no targets, no CMAKE_CURRENT_LIST_DIR pointing at the
# build. Only CPACK_* variables exist. Anything else read here is empty.

# ── The binary packages carry the runtime component only ──────────────────────
# The install tree is 116 files and one of them is the game. The others are the
# exported CMake package and licence notices, which have no runtime role at all:
# a static archive is consumed at link time, and bin/glyphcade already contains
# that code.
#
# So a .deb or .rpm is the binary and its licence notices, full stop. The
# exported package still gets built, still gets proven by the consumer-resolves
# ctest, and still ships — in the tarball, which takes every component
# unfiltered. See the header of cmake/packaging.cmake for why there is no
# separate -dev / -devel package.
#
# ⚠ CPACK_COMPONENTS_ALL is global, which is exactly why this narrowing cannot
# live in cmake/packaging.cmake: a plain set() there would apply to the archive
# generator too and reduce the tarball to a single file.
if (CPACK_GENERATOR MATCHES "^(DEB|RPM)$")
  set(CPACK_COMPONENTS_ALL runtime)
endif ()
