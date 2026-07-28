# cpp-template audit — read before Epic 0

Audited 2026-07-28 against cpp-template `main` @ `3ed1d97`, before forking it for
term-game. Findings feed [Epic 0](https://git.gobha.me/xcaliber/term-game/issues/1).

**One landmine, and it is on our exact path.**

### CT-15 will fail Epic 0 at CMake *generate* time

[cpp-template CT-15](https://github.com/gobha-me/cpp-template/issues/29).
`cmake/install.cmake` (~line 54) emits a **build-tree** export alongside the
install one:

```cmake
export(EXPORT ${PROJECT_NAME}Targets
  FILE      ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake
  NAMESPACE ${PROJECT_NAME}::)
```

`export(EXPORT)` requires every non-imported dependency to be in a **build**
export set. term-game links termforge **publicly** — the Shell derives from
`termforge::App` and exposes it through our headers, so `PRIVATE` is not
available — and **termforge registers no export sets at all** (no `install()`,
no `export()`, no `install.cmake`; its root still says `# TODO Install Template`,
tracked as [termforge#27](https://github.com/gobha-me/termforge/issues/27)).

Expected failure, by analogy with the venice-cpp reproducer:

```
CMake Error in CMakeLists.txt:
  export called with target "term-game_lib" which requires target
  "termforge_lib" that is not in any export set.
```

**Our case is worse than the one CT-15 documents.** There, only the build-tree
path failed — `install(EXPORT)` survived because `HandleMissingTarget` found the
dependency in *its* install export set and rewrote the reference. termforge has
no install export set either, so **both** paths fail. There is no knob in this
project that fixes it.

**Action for Epic 0:**
1. **Delete the `export(EXPORT ...)` block** from our copy of
   `cmake/install.cmake`. This is what the venice-cpp port did; we are the second
   fork to do it. Leave a comment saying why, pointing at CT-15.
2. Treat `add_subdirectory` as the supported side-by-side development path
   (that block's only purpose was letting a consumer point `CMAKE_PREFIX_PATH`
   at a build dir).
3. `find_package(termforge CONFIG)` stays unusable until termforge#27 adds
   `install(EXPORT)`. Until then, **FetchContent/add_subdirectory is the only
   path** — plan the CMake around that rather than writing a find_package-first
   block that cannot work yet.

### Other open cpp-template issues — none blocking

- **[CT-12](https://github.com/gobha-me/cpp-template/issues/25)** — headers
  install flat, should be `include/<project>/`. We already put ours under
  `include/termgame/` per DESIGN.md, so we sidestep it. **Note the deliberate
  spelling mismatch: the project/repo is `term-game`, the include dir and C++
  namespace are `termgame`** — a hyphen is illegal in a namespace. Document it so
  nobody "fixes" it later.
- **[CT-11](https://github.com/gobha-me/cpp-template/issues/24)** —
  `version.hpp` generates into the *source* tree. This is the same trap
  `NEW_PROJECT.md` Step 0 warns about (it is gitignored, so a `cp -r` carries a
  stale one while a `git clone` does not). Use `git clone`, and `rm -f
  include/version.hpp` before the first configure.
- **[CT-13](https://github.com/gobha-me/cpp-template/issues/26)** /
  **[CT-10](https://github.com/gobha-me/cpp-template/issues/11)** — template
  hygiene and drift docs. No impact on us.

Template `main` is otherwise clean and current (`3ed1d97`), with
`PROJECT_IS_TOP_LEVEL` guards and install/export already landed (CT-04, #23) and
CI enforcing the dual-compiler rule (CT-08, #18).
