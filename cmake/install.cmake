# ── Install, export, and package config ───────────────────────────────────────
# This is the answer to the "TODO Install Template" the root CMakeLists carried
# for years, and the reason it matters: a project consuming this one should be
# able to write
#
#   find_package(<project> CONFIG REQUIRED)
#   target_link_libraries(app PRIVATE <project>::lib)
#
# and have it work against an installed prefix, with the *same* target spelling
# it would use via add_subdirectory() or FetchContent. One spelling, three
# acquisition modes.
#
# ⚠ This used to claim "See example/consumer/ for all three, exercised." There is
# no example/ directory in this repo and there never has been, and for a long
# while nothing automated proved the installed package actually resolves — the
# closest thing was the audio-export-clean ctest, which only greps the Targets
# file for rtaudio.
#
# term-game#46 closed that. The **consumer-resolves** ctest
# (cmake/check_consumer.cmake) installs to a scratch prefix and CONFIGURES a
# generated throwaway consumer against it, so `find_package(<project> REQUIRED)`
# and the find_dependency line in cmake/project-config.cmake.in are executed on
# every run rather than merely written. The INSTALL acquisition mode above is
# now covered; add_subdirectory() and FetchContent are covered by the in-tree
# build itself.
#
# What is still unexercised is narrower and worth naming: nothing COMPILES or
# LINKS against the installed package — the consumer configures only. So a
# broken INTERFACE include path or a missing archive would survive. The residual
# risk stays bounded for the reasons it always was: the "target not in any
# export set" case fails loudly at generate time, and termforge::lib is the only
# external name any exported target references.
#
# Included from the root CMakeLists behind ${PROJECT_NAME}_INSTALL, which
# defaults to PROJECT_IS_TOP_LEVEL: an embedded copy of this project must not
# inject rules into its consumer's `cmake --install`.
#
# Nothing here names the project literally — every rule below spells it
# ${PROJECT_NAME} — so a fork copies this file verbatim.
#
# ⚠ That used to be justified as "the package name is the project name, which is
# derived from the directory". The derivation is gone: the root CMakeLists calls
# project(glyphcade ...) with a literal, because deriving it meant a FetchContent
# consumer unpacking into _deps/glyphcade-src got targets called
# glyphcade-src_lib. check_artifacts rule B6 keeps project() and test/00bootstrap
# agreeing on the literal. The conclusion survives the change; the reason did not.
#
# ── Components: `runtime` and `dev` ───────────────────────────────────────────
# Every install() below names one, and that is enforced rather than trusted —
# cmake/packaging.cmake derives the component list from the global COMPONENTS
# property and FATAL_ERRORs unless it is exactly `dev;runtime`, so a rule added
# here without a COMPONENT registers as `Unspecified` and stops the configure.
#
# ⚠ There is deliberately no check_artifacts rule for this, and the omission is
# considered. A Class-B text rule ("every install( in this file is followed by a
# COMPONENT") would be strictly weaker than the assertion above: that one
# observes what CMake actually recorded rather than what the file appears to
# say, it covers termforge's install() calls too — which no grep of this file
# could see — and it runs on every top-level configure rather than only under
# ctest. Choosing the wrong component rather than none is caught downstream by
# cmake/check_package.cmake, which asserts the runtime package's contents as an
# exact set. Two mechanisms already cover both halves; a third would be ceremony.
#
#   runtime — the executable, and nothing else. What a player installs.
#   dev     — static archives, headers, the exported Targets/Config files, and
#             everything termforge installs into our prefix (see the scoped
#             CMAKE_INSTALL_DEFAULT_COMPONENT_NAME in the root CMakeLists).

include(CMakePackageConfigHelpers)

set(_cfg_install_dir ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

# ── The library ───────────────────────────────────────────────────────────────
if (TARGET ${PROJECT_NAME}_lib)
  # ── Every target in src/lib's chain joins the export set, because it must ───
  # src/lib is four static libraries since the per-game split, and
  # ${PROJECT_NAME}_lib links the other three PUBLIC. install(EXPORT) refuses to
  # write a Targets file that references a target it cannot name:
  #
  #   install(EXPORT "glyphcadeTargets" ...) includes target "glyphcade_lib"
  #   which requires target "glyphcade_roster" that is not in any export set.
  #
  # So this is not a decision about how much of the internals to publish — it is
  # the only way the package generates at all. It is also the guard: add a game,
  # forget to add it to src/lib's game list, and generation stops with that error
  # naming your target. A game cannot be silently absent from the package.
  #
  # The game targets arrive as ${PROJECT_NAME}_GAME_TARGETS, set PARENT_SCOPE by
  # src/lib/CMakeLists.txt, so this file needs no edit when a game is added.
  set(_export_targets
    ${PROJECT_NAME}_lib
    ${PROJECT_NAME}_roster
    ${${PROJECT_NAME}_GAME_TARGETS}
    ${PROJECT_NAME}_core
  )

  # EXPORT_NAME is what makes each imported target read ${PROJECT_NAME}::lib
  # rather than ${PROJECT_NAME}::${PROJECT_NAME}_lib. Paired with NAMESPACE on
  # the install(EXPORT) below, a downstream `find_package` gets targets spelled
  # identically to the in-tree ALIASes in src/lib/CMakeLists.txt — so a consumer
  # can switch acquisition modes without touching its link lines.
  #
  # Derived by dropping the "${PROJECT_NAME}_" prefix rather than listed one by
  # one, so a new game needs no line here either: glyphcade_game_2048 exports as
  # glyphcade::game_2048. A target without the prefix would export under its own
  # name, which is a visible oddity rather than a silent one.
  foreach (_t IN LISTS _export_targets)
    string(REGEX REPLACE "^${PROJECT_NAME}_" "" _export_name "${_t}")
    set_target_properties(${_t} PROPERTIES EXPORT_NAME ${_export_name})
  endforeach ()

  # One call covers every library variant. For a header-only (INTERFACE) target
  # the ARCHIVE/LIBRARY/RUNTIME destinations simply go unused — there is no
  # artifact to place — so switching a target's type in src/lib/CMakeLists.txt
  # needs no edit here.
  # ⚠ COMPONENT is scoped to the artifact kind it follows, so each of the three
  # needs its own — one COMPONENT at the end of the call would apply to RUNTIME
  # alone and leave the archives Unspecified.
  #
  # ARCHIVE is the only kind that fires today, because every target here is
  # STATIC. LIBRARY and its NAMELINK_COMPONENT are written anyway, and they are
  # not decoration: under BUILD_SHARED_LIBS=ON the .so is a runtime artifact and
  # only the .so.N symlink a linker follows is a dev one — without those two
  # words a shared build would file libglyphcade_lib.so in the dev package,
  # where no player would find it.
  #
  # ⚠ This is where the file's "switching a target's type needs no edit here"
  # claim stops being the whole story, and it is worth being precise rather than
  # letting the two statements sit next to each other. Nothing in THIS file
  # needs editing. But a shared build would put the .so in the runtime package,
  # and cmake/check_package.cmake asserts the runtime package's contents as an
  # exact set — so that switch does need one edit, in that list, and the check
  # says so when it fires.
  install(TARGETS ${_export_targets}
    EXPORT ${PROJECT_NAME}Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime
                                                NAMELINK_COMPONENT dev
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  )

  # COMPONENT dev covers the generated Targets-<config>.cmake files too — they
  # are written by this one rule, not by a second install() we could forget.
  install(EXPORT ${PROJECT_NAME}Targets
    FILE      ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION ${_cfg_install_dir}
    COMPONENT dev
  )

  # ── Why there is no export(EXPORT ...) here ─────────────────────────────────
  # There used to be one, describing this same target set against the build tree
  # so a consumer could point CMAKE_PREFIX_PATH at a build directory. It was
  # removed (#29), and it must not come back, because it cannot survive being
  # copied into a project whose library links a dependency it did not build.
  #
  # export() enforces the same "every referenced target must be in an export
  # set" rule as install(EXPORT), but against *build* export sets — the ones
  # registered by export() itself. Plenty of dependencies register none: they
  # ship install(EXPORT) rules and no export() call anywhere. Link one into this
  # library and generation stops with
  #
  #   export called with target "<project>_lib" which requires target "<dep>"
  #   that is not in any export set.
  #
  # The install side above does not fail on the same target, which is why this
  # surprises: CMake finds <dep> in the install export set its own rules
  # registered and quietly rewrites the reference to <dep>::<dep>. Only the
  # build-tree path has nothing to find.
  #
  # It cannot be guarded, either — and that is worth stating, because "just skip
  # the export when a dependency is missing" is the obvious next idea. The
  # condition is not merely unqueryable (no property or command reports build
  # export set membership); it is unexpressible at the point the decision has to
  # be made. INTERFACE_LINK_LIBRARIES holds unevaluated generator expressions
  # — $<LINK_ONLY:...>, $<BUILD_INTERFACE:...>, $<TARGET_NAME_IF_EXISTS:...> —
  # that resolve during generation, after every line of CMake language has run.
  #
  # A fork that genuinely wants a build-tree package can put the missing
  # dependency in a build export set itself, per dependency:
  #
  #   export(TARGETS <dep> NAMESPACE <dep>:: FILE ${CMAKE_BINARY_DIR}/<dep>Targets.cmake)
  #
  # That is boilerplate this file cannot write on anyone's behalf, and it is
  # only half the job — a build-tree package also wants its own
  # configure_package_config_file() call, because the one below computes
  # PACKAGE_PREFIX_DIR for lib/cmake/<project>, three directories above a build
  # dir. Sharing it is harmless only for as long as the config resolves nothing
  # relative to itself.
  #
  # ⚠ Do not reach for export(TARGETS ... APPEND) to silence the error. APPEND
  # mode does not complain about the missing target; it writes the reference as
  # <project>::<dep> — a target in *this* project's namespace that nothing
  # anywhere defines. A loud generate-time failure becomes a Targets file that
  # is quietly wrong.
  #
  # For developing two projects side by side, use add_subdirectory() — same
  # target spelling, no packaging involved. (Nothing in this repo exercises that
  # path either; see the note at the top of this file.)

  # ── Headers ─────────────────────────────────────────────────────────────────
  # *.hpp only, which picks up the public header and the generated version
  # header while leaving version.hpp.in.cmake behind (it does not end in .hpp).
  #
  # Installing the generated header is deliberate, not incidental: the
  # header-only variant inlines lib.hpp's function bodies, and those read
  # VERSION_MAJOR / PROGRAM_NAME from it — leave it out and that variant cannot
  # be consumed at all.
  #
  # ⚠ The cost, which a real project should weigh: the generated header declares
  # unprefixed globals (PROGRAM_NAME, VERSION_MAJOR, ...) and lands directly in
  # the consumer's include path, where it can collide with theirs. A project that
  # expects to be widely consumed should move its headers under
  # include/<project>/ and generate the version header there too.
  #
  # ⚠ COMPONENT goes BEFORE FILES_MATCHING. install(DIRECTORY) treats every
  # argument after FILES_MATCHING as part of the pattern list, so a COMPONENT
  # trailing it is not a syntax error — it is silently absorbed, and the headers
  # land in Unspecified.
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT dev
    FILES_MATCHING PATTERN "*.hpp"
  )
endif ()

# ── The application ───────────────────────────────────────────────────────────
# Installed when it is built, but deliberately not exported: an executable is
# something you run, not something another project links.
#
# The binary and the licence notices below are the whole `runtime` component,
# and cmake/check_package.cmake asserts that literally — the runtime .deb must
# contain those files and no other. If a man page, a .desktop entry or an icon
# ever arrives, it belongs here with COMPONENT runtime, and that assertion is
# what will say so.
if (TARGET ${PROJECT_NAME} AND ${PROJECT_NAME}_BUILD_BIN)
  install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  )
endif ()

# ── Licences, and why they are runtime rather than dev ────────────────────────
# Nothing installed this project's own LICENSE.md until packaging needed it: an
# install tree is something a developer already has the source for, so the
# omission never showed. A .deb is not — it is a binary handed to somebody who
# has nothing else — and MIT's condition is that the notice travel with it.
#
# ⚠ termforge's notice belongs here too, and that is the non-obvious half. It
# installs its own copy to share/licenses/termforge/, but that rule is upstream's
# and lands in `dev` with the rest of what it ships (see the scoped
# CMAKE_INSTALL_DEFAULT_COMPONENT_NAME in the root CMakeLists) — while termforge
# is linked STATICALLY, so its code is inside bin/glyphcade. The binary carries
# the code; the binary's package must carry the notice. Publishing only the
# runtime packages (the decision behind #15) is what turns that from a tidiness
# point into a licence-compliance one.
#
# Installed to a DIFFERENT path than upstream's copy on purpose: same-path rules
# in two components make two packages own one file, which dpkg rejects. Both
# copies coexist in the tarball, which is the monolithic whole tree by design.
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE.md
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/${PROJECT_NAME}
  COMPONENT runtime
)

# ⚠ Only reachable on the FetchContent path — termforge_SOURCE_DIR is set by
# FetchContent_MakeAvailable and does not exist when find_package found a system
# copy, where the notice is already installed at that prefix and is not ours to
# re-ship. Rather than guess, cmake/check_package.cmake asserts the file IS in
# the runtime package: a package built against a system termforge goes red and
# says so, instead of shipping a binary with a missing notice.
if (DEFINED termforge_SOURCE_DIR AND EXISTS ${termforge_SOURCE_DIR}/LICENSE.md)
  install(FILES ${termforge_SOURCE_DIR}/LICENSE.md
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/${PROJECT_NAME}
    RENAME LICENSE.termforge.md
    COMPONENT runtime
  )
endif ()

# stb_image is compiled into glyphcade_core rather than exposed as a dependency,
# and future game binaries pull that object in the moment they call decode_png.
# Ship its chosen MIT notice now with the decoder instead of leaving Solitaire
# a licence-compliance trap one issue later. The notice is harmless in the
# current runtime package even though only the proof test calls the decoder.
install(FILES ${PROJECT_SOURCE_DIR}/vendor/LICENSE.stb.md
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/${PROJECT_NAME}
  COMPONENT runtime
)

# ── Package config ────────────────────────────────────────────────────────────
# <project>Config.cmake is what find_package(<project> CONFIG) loads; it exists
# to pull in the Targets file (and, in a real project, to re-find the public
# dependencies those targets need — see the commented block in the .in file).
configure_package_config_file(
  ${CMAKE_CURRENT_LIST_DIR}/project-config.cmake.in
  ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
  INSTALL_DESTINATION ${_cfg_install_dir}
)

# ARCH_INDEPENDENT only for the header-only variant: a config that ships no
# compiled artifact is usable from a build of any word size, and saying so keeps
# the package from being rejected on a 32/64-bit mismatch that cannot apply.
# Detected rather than configured, so swapping variants needs no edit here.
set(_version_file_args "")
if (TARGET ${PROJECT_NAME}_lib)
  get_target_property(_lib_type ${PROJECT_NAME}_lib TYPE)
  if (_lib_type STREQUAL "INTERFACE_LIBRARY")
    set(_version_file_args ARCH_INDEPENDENT)
  endif ()
endif ()

# SameMajorVersion is the conventional read of semver: 1.4.0 satisfies a request
# for 1.2.0, 2.0.0 does not. Swap to SameMinorVersion or ExactVersion if your
# project's compatibility promise is narrower.
#
# ⚠ The version comes from `git describe` at configure time (cmake/version.cmake).
# A build with no reachable tags reports 0.0.0, and a consumer asking for a real
# version then gets a documented refusal from this file rather than a mystery. If
# that happens in CI, the cause is almost always a shallow clone — keep
# fetch-depth: 0.
write_basic_package_version_file(
  ${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
  ${_version_file_args}
)

install(FILES
    ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    ${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
  DESTINATION ${_cfg_install_dir}
  COMPONENT dev
)
