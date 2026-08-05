# Changelog

All notable changes to this project.

Versions before `v0.20.0` were developed under the name **term-game** on a
self-hosted tracker. Their tag messages still say `gitea #NN`, which was true
when they were written; those issue numbers are not public. See
[docs/history.md](docs/history.md) for the full development narrative.

## [Unreleased]

### Changed — the project is now `glyphcade`, and open source

- **Renamed** `term-game` → `glyphcade` throughout: the CMake project and every
  target, the C++ namespace, `include/`, the `GLYPHCADE_*` macro and environment
  prefix, the exported package, and all user-visible strings. The old
  `term-game`/`termgame` spelling split is gone — it existed only because a
  hyphen is illegal in a C++ namespace, and `glyphcade` has none.
- **`project()` now takes a literal name** instead of deriving it from the
  checkout directory. Deriving it meant a FetchContent consumer unpacking into
  `_deps/glyphcade-src` would get targets called `glyphcade-src_lib` and a
  `find_package(glyphcade)` that resolves nothing. New `check_artifacts` rule
  **B6** keeps `project()` and `test/00bootstrap` in agreement.
- **CI moved to GitHub Actions.** Same five build arms in the same pinned
  `debian:trixie` container. The bare `--parallel` — unbounded `make -j` on a
  container-limited runner — is now bounded.

### Breaking

- **High scores do not carry over.** The score file moved from
  `~/.local/share/term-game/scores` to `~/.local/share/glyphcade/scores` and its
  header changed to `# glyphcade scores v1`. The format itself is unchanged, so
  the version stays at `v1`; the old file is simply never opened. Rename the
  directory by hand if you want your scores back.
- Consumers must use `find_package(glyphcade)` and link `glyphcade::lib`.
- Environment variables `TERMGAME_SCORES` and `TERMGAME_AUDIO_WAV` are now
  `GLYPHCADE_SCORES` and `GLYPHCADE_AUDIO_WAV`.

### Removed

- A 1.1 MB core dump was stripped from the git history before the first public
  push. It contained no credentials, but it was process memory and had no
  business being in a repository. `.gitignore` now covers `core`/`core.*`.

## [v0.19.0] — 2026-08-05

Tetris's next-up preview becomes the spawn stream. The panel was not stale — it
was showing pieces unrelated to the ones that actually arrived. First defect
here found by playing on hardware rather than by reading the code.

## [v0.18.0] — 2026-08-05

The selector learns what a game needs, and stops growing. Every game declares a
minimum terminal size *and what kind of claim that is*; the selector caps at 120
columns and centres.

## [v0.17.0] — 2026-07-31

The exported package is now *resolved* by a ctest rather than read as text —
`check_export` used to grep the targets file without ever calling
`find_package`, so a package no consumer could resolve still reported clean.

## [v0.16.0] — 2026-07-31

The options cycler joins the glyph tier.

## [v0.15.0] — 2026-07-31

The pause dialog joins the border tier — it had been painting box-drawing glyphs
on no-colour terminals for two releases.

## [v0.14.0] — 2026-07-31

TermForge pin moves v0.2.2 → v0.6.0. Expected to be inert; it was not — it fixed
a wrongly-lit Resume button painted for one frame at every pause re-opening.

## [v0.13.0] — 2026-07-31

The pre-start options screen: four of five games ask before starting.

## [v0.12.0] — 2026-07-30

**Epic 7: Sokoban.** First game whose playfield is a TermForge widget rather
than a loop writing cells.

## [v0.11.0] — 2026-07-30

**Epic 6: Tetris.** DAS/held-key feel, and the kitty keyboard protocol.

## [v0.10.0] — 2026-07-30

The keyboard seam: a game declares a `KeyboardMode` and the Shell sets it per
entry, restoring `Legacy` on exit.

## [v0.9.0] — 2026-07-30

**Epic 5: Snake.** The first real-time game, and the first thing in the suite a
wobbling frame rate breaks.

## [v0.8.0] — 2026-07-30

TermForge pin moves v0.1.15 → v0.2.2.

## [v0.7.0] — 2026-07-30

High scores persist, via an XDG-located score file with a versioned header.

## [v0.6.0] — 2026-07-30

**Epic 4: 2048**, with a designed tween over the tick path.

## [v0.5.0] — 2026-07-30

One static library per game — a pure refactor, and the layering is load-bearing:
it is the only order that resolves in a single left-to-right linker pass.

## [v0.4.1] — 2026-07-29

CI green for the first time. The cause of three epics of red was never in the
repo — the runner image had no CMake and no Clang.

## [v0.4.0] — 2026-07-29

**Epic 2: the audio engine.** Synthesized, not sampled; a lock-free SPSC ring
between the UI thread and the realtime callback.

## [v0.3.2] — 2026-07-29

No workarounds left.

## [v0.3.1] — 2026-07-28

The exception boundary stops being a workaround.

## [v0.3.0] — 2026-07-28

**Epic 3: Minesweeper.** Mouse routing and grid rendering.

## [v0.2.0] — 2026-07-28

**Epic 1: the arcade shell.** Game interface, explicit registry, selector UI,
fixed timestep.

## [v0.1.0] — 2026-07-28

**Epic 0: project bootstrap.**

[Unreleased]: https://github.com/gobha-me/glyphcade/compare/v0.19.0...HEAD
[v0.19.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.19.0
[v0.18.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.18.0
[v0.17.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.17.0
[v0.16.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.16.0
[v0.15.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.15.0
[v0.14.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.14.0
[v0.13.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.13.0
[v0.12.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.12.0
[v0.11.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.11.0
[v0.10.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.10.0
[v0.9.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.9.0
[v0.8.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.8.0
[v0.7.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.7.0
[v0.6.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.6.0
[v0.5.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.5.0
[v0.4.1]: https://github.com/gobha-me/glyphcade/releases/tag/v0.4.1
[v0.4.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.4.0
[v0.3.2]: https://github.com/gobha-me/glyphcade/releases/tag/v0.3.2
[v0.3.1]: https://github.com/gobha-me/glyphcade/releases/tag/v0.3.1
[v0.3.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.3.0
[v0.2.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.2.0
[v0.1.0]: https://github.com/gobha-me/glyphcade/releases/tag/v0.1.0
