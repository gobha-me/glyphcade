# Contributing

Patches welcome. This file is the short version; [AGENTS.md](AGENTS.md) is the
long one and is worth reading before a non-trivial change — it records the rules
where the *correct* code looks wrong, and there are a few.

## Build and test

```bash
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

CMake 3.28+, C++23 (GCC 13+ / Clang 19+). TermForge and Catch2 are fetched
automatically. `librtaudio-dev` is optional.

## Before you open a pull request

Four configurations, minimum. They are not redundant with each other, and CI
runs all four plus ASan:

```bash
# 1. GCC, audio auto-detected
cmake -B build -DCMAKE_CXX_FLAGS=-Werror && cmake --build build \
  && ctest --test-dir build --output-on-failure

# 2. GCC, audio explicitly OFF — the configuration this repo promises always
#    works. If your box has librtaudio-dev, arm 1 does NOT cover it.
cmake -B build-noaudio -DGLYPHCADE_WITH_AUDIO=OFF -DCMAKE_CXX_FLAGS=-Werror \
  && cmake --build build-noaudio && ctest --test-dir build-noaudio --output-on-failure

# 3. Clang
cmake -B build-clang -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/clang.cmake \
  -DCMAKE_CXX_FLAGS=-Werror && cmake --build build-clang \
  && ctest --test-dir build-clang --output-on-failure

# 4. Thread sanitizer — the arm that judges the audio command ring
cmake -B build-tsan -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/thread.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-Werror \
  && CMAKE_BUILD_PARALLEL_LEVEL=2 cmake --build build-tsan \
  && ctest --test-dir build-tsan --output-on-failure
```

⚠ **Do not add a bare `--parallel`.** `cmake --build` consults
`CMAKE_BUILD_PARALLEL_LEVEL` *only when `--parallel` is absent*, and this project
uses the Unix Makefiles generator — so a bare `--parallel` is `make -j` with no
limit, and whatever cap you set is silently discarded. AGENTS.md explains why
that matters on a container-limited machine.

⚠ **`git add` your changes before judging `artifact-check`.** It reads
`git ls-files`, so unstaged files produce spurious failures.

## What a good change looks like

**A test that cannot fail is the recurring defect here**, and most of the rules
below exist because one got shipped once.

- **Say what would have gone red.** A change with no test that fails before and
  passes after is untested, even when the suite is green. Dependency bumps are
  the canonical case: every test can pass on the new version *before the new
  tests exist*.
- **A rendering check needs a control.** Grep the *stripped* text for words and
  the *raw* capture for escape sequences — the renderer positions every cell, so
  screen adjacency is not byte adjacency, and a naive grep for a visible word
  returns zero at every version. A colour assertion that also proves the widget
  never rendered proves nothing.
- **Two claims need two test cases, not two `REQUIRE`s.** A failing `REQUIRE`
  aborts its case, so one case can only ever report the first failure.
- **Explain the non-obvious at its site.** This codebase comments *why*, not
  *what*, and several one-line changes are load-bearing in ways that read like
  clutter. If you delete a line that looks redundant, check the trap lists in
  `test/11selector` first.

## Filing an issue

Bugs, feel reports and framework gaps are all welcome — but note that gaps in
the *rendering framework* belong upstream at
[gobha-me/termforge](https://github.com/gobha-me/termforge), not here. A
workaround buried in game code is the thing this project exists to avoid.

Feel reports ("the ghost piece looks wrong in kitty", "DAS is too slow") are
especially useful, because nothing in CI can judge them.

## Style

`.clang-format` and `.clangd` are in the repo and configured; match the
surrounding code. Commit messages explain *why* — the log is used as
documentation here, so a subject line that only restates the diff is a missed
opportunity.

Agent-authored commits carry a `Co-Authored-By:` trailer.
