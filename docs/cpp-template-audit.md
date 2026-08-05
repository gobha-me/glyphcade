# cpp-template audit — the record of why our fork diverges

Originally written 2026-07-28 against cpp-template `main` @ `3ed1d97`, *before*
forking it, to find what would break [Epic 0](`term-game#1`).
Rewritten after Epic 0 landed, against `8f62930`.

Its job now is to be the standing record of **where our copy of the template
differs from upstream and why**, so nobody silently re-syncs a file and undoes a
fix. The live divergences table is in [STATUS.md](STATUS.md); this is the
reasoning behind it.

---

## CT-15 — the landmine that was defused before we stepped on it

[CT-15](https://github.com/gobha-me/cpp-template/issues/29). `cmake/install.cmake`
used to emit a **build-tree** export alongside the install one:

```cmake
export(EXPORT ${PROJECT_NAME}Targets
  FILE      ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake
  NAMESPACE ${PROJECT_NAME}::)
```

`export(EXPORT)` requires every non-imported dependency to be in a **build**
export set. glyphcade links termforge **publicly** — the `Shell` derives from
`termforge::App` and exposes it through our headers, so `PRIVATE` was never
available — and at audit time termforge registered *no* export sets at all. The
predicted failure, at CMake *generate* time:

```
CMake Error in CMakeLists.txt:
  export called with target "glyphcade_lib" which requires target
  "termforge_lib" that is not in any export set.
```

Our case was worse than the one CT-15 documents: there, only the build-tree path
failed, because `HandleMissingTarget` found the dependency in *its* install
export set. termforge had neither, so both paths failed with no rescue.

**It never fired.** Upstream fixed it at `8f62930` ("Drop the build-tree export
so install.cmake survives a fork with a public dep"), and the block is now a
48-line comment at `cmake/install.cmake:51-98` explaining why it must not come
back — including the trap of reaching for `export(TARGETS ... APPEND)` to silence
the error, which writes a `<project>::<dep>` reference that nothing defines.

**Consequence for us: `cmake/install.cmake` is copied verbatim and needs no
edit.** The earlier advice in this document — "delete the `export(EXPORT ...)`
block from our copy" — is a no-op and has been removed. The analysis is kept
because it is *why* the upstream fix exists, and because the same reasoning is
what makes `set(termforge_INSTALL ${${PROJECT_NAME}_INSTALL})` load-bearing in
[cmake/deps/termforge.cmake](../cmake/deps/termforge.cmake).

> The original audit also concluded that `find_package(termforge CONFIG)` would
> stay unusable and that FetchContent was the only path. **That is now false.**
> termforge v0.1.7 ships `install(EXPORT)` and `termforgeConfig.cmake`, both
> paths work, and both give `termforge::lib` — which is why our recipe is
> find_package-first with a FetchContent fallback.

---

## Our two divergences from the template

Both are fixes carried into our copy, both filed upstream, both listed with a
deletion condition in [STATUS.md](STATUS.md#divergences-from-cpp-template).

### 1. `cmake/toolchain/default.cmake` clobbered `CMAKE_CXX_FLAGS`

Upstream line 14 is a plain replace:

```cmake
set(CMAKE_CXX_FLAGS "-Wall -Wnarrowing -Wextra -pedantic")
```

A toolchain file's normal variable shadows the cache, so
`cmake -B build -DCMAKE_CXX_FLAGS=-Werror` is **silently discarded**. Every
cpp-template fork that puts `-Werror` in CI is therefore enforcing nothing while
reporting green. termforge already carries the fix (`include_guard(GLOBAL)` plus
an appending `set()`); we carry the same one.

Because the fix is invisible when it regresses, CI asserts it separately:
`grep -q -- '-Werror' build/compile_commands.json`, in
[.github/workflows/ci.yml](../.github/workflows/ci.yml).

### 2. `check_artifacts.cmake` rule A1 scanned every tracked file

A1 is labelled *"upstream repo slug (README CI badge)"* but is implemented as
`_scan("gobha-me/cpp-template" ".*" ...)` — every tracked file, with only
`check_artifacts.cmake` and `NEW_PROJECT.md` excluded by content. A badge can
only live in the README, so the `.*` makes it structurally impossible for a fork
to document its own provenance — which NEW_PROJECT.md Step 1 explicitly
instructs you to do ("Scaffolded from cpp-template").

glyphcade names the slug legitimately eight times across `AGENTS.md`,
`DESIGN.md`, `STATUS.md` and this file. Our copy narrows the path filter to
`^README\.md$`. Accepted cost: a stale slug outside the README is no longer
caught — outside the README it is prose, not wiring.

---

## Traps that were real and are still worth knowing

- **`include/version.hpp` is generated and gitignored.** A `cp -r` carries a
  stale one; a `git clone` does not (CT-11,
  [#24](https://github.com/gobha-me/cpp-template/issues/24)). We scaffolded with
  `git archive HEAD <paths> | tar -x`, which emits only tracked content and so
  cannot carry it — and which also preserves the `100755` mode on
  `cmake/startup.sh` / `cmake/shutdown.sh` that a plain `cp` drops, failing
  artifact rule B5.
- **`cmake/version.cmake` runs `git describe` from inside `cmake/`**, and git
  walks *upward*. A tree without its own `.git` inherits an enclosing repo's
  tags with no warning. Not an issue here — glyphcade has its own history — but
  it is why `fetch-depth: 0` in CI is load-bearing.
- **`check_artifacts` reads `git ls-files`.** Mid-bootstrap, before anything is
  staged, rules B2 and B3 fail for the mechanical reason that the files they
  correlate are untracked. `git add` first, then judge the output.
- **CT-12** ([#25](https://github.com/gobha-me/cpp-template/issues/25)) — headers
  install flat. We sidestep it by putting ours under `include/glyphcade/`, but we
  still hit its tail: `install.cmake` lands the generated `version.hpp`, which
  declares unprefixed globals (`PROGRAM_NAME`, `VERSION_MAJOR`, …), flat in
  `${prefix}/include`. termforge deliberately never installs its own. Left alone
  here — `install.cmake` stays verbatim — and reported upstream as a datapoint.

## The rest

**CT-13** ([#26](https://github.com/gobha-me/cpp-template/issues/26)) and
**CT-10** ([#11](https://github.com/gobha-me/cpp-template/issues/11)) are
template hygiene and drift docs. No impact on us.

`.github/workflows/ci.yml` is **not** carried over despite NEW_PROJECT.md's
"copy verbatim, zero edits" — that instruction assumes a GitHub fork, and
glyphcade is on GitHub. See [.github/workflows/ci.yml](../.github/workflows/ci.yml).
