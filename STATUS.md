# STATUS — term-game

Live state. Update this when something lands; do not let it drift.

**Last updated: 2026-07-28**

---

## Where the project actually is

**Design phase. No code yet.** The repo contains
[DESIGN.md](DESIGN.md), [AGENTS.md](AGENTS.md), [README.md](README.md), and this
file. Nine epics are filed on gitea (`xcaliber/term-game`, issues 1–9).

**Next move: Epic 0 (project bootstrap).** It is blocked — see below.

---

## Blocking state

| Epic | State | Blocked by |
|---|---|---|
| 0 — Bootstrap | **blocked** | termforge [#27](https://github.com/gobha-me/termforge/issues/27) |
| 1 — Arcade shell | not started | Epic 0 |
| 2 — Audio engine | not started | Epic 0 |
| 3 — Minesweeper | not started | Epics 0, 1 |
| 4 — 2048 | not started | Epics 0, 1 |
| 5 — Snake | not started | Epics 0, 1 |
| 6 — Tetris | not started | Epics 0, 1; termforge #60 (degradable) |
| 7 — Sokoban | not started | termforge #64 → #63 |
| 8 — Solitaire | not started | termforge #63 |

### The one thing standing in the way

termforge **[#27](https://github.com/gobha-me/termforge/issues/27)** — no
`install()`/`export()`, so `find_package(termforge CONFIG)` cannot succeed; and
`add_subdirectory` currently drags in termforge's bin, examples and 412 tests
*and* defaults `CMAKE_TOOLCHAIN_FILE` out from under the consumer. **Nothing in
this repo builds until one of the two consumption paths works.** Still open as of
this update.

---

## Upstream framework work (termforge, GitHub)

Filed from this project's design audit:

| Issue | Gap | State |
|---|---|---|
| [#27](https://github.com/gobha-me/termforge/issues/27) | CMake consumption (install/export, `PROJECT_IS_TOP_LEVEL` gating) | **open — critical path** |
| [#58](https://github.com/gobha-me/termforge/issues/58) | Frame pacing: idle loop capped ~7.5fps | **fixed upstream**, issue still open |
| [#59](https://github.com/gobha-me/termforge/issues/59) | No `on_tick(dt)` hook | open (worked around in Shell) |
| [#60](https://github.com/gobha-me/termforge/issues/60) | No key release (Kitty keyboard protocol) | open |
| [#61](https://github.com/gobha-me/termforge/issues/61) | `Key` enum stops at F4 | open |
| [#62](https://github.com/gobha-me/termforge/issues/62) | `Cell` has no text attributes | open |
| [#63](https://github.com/gobha-me/termforge/issues/63) | `Image` has no blit/alpha compositing | open |
| [#64](https://github.com/gobha-me/termforge/issues/64) | MapWidget (Epic 3.6) | open |

### #58 — fixed and empirically confirmed

termforge commit `5d4d9a9` (*"make the frame budget authoritative"*) landed the
fix, and measurement in a real pty confirmed the analysis exactly:

- **before:** 7.6 fps at `frame_ms=33` (predicted ~7.5)
- **after:** 30.7 fps at `frame_ms=33`
- **before, `set_frame_ms(0)`:** exactly **10.0 fps** — the predicted hard
  ceiling from the 100 ms `VTIME=1` floor with the sleep removed

`set_frame_ms` is now authoritative rather than a hint, and `App` gained a
headless frame-stepping harness (`frame_step()`, with overridable
`now_steady()` / `wait_readable()` / `read_available()`) so cadence is testable
over a fake clock and fake fd.

**Consequence for us:** Epic 5 (Snake) and Epic 6 (Tetris) are no longer blocked
on frame pacing. Epic 6 still wants #60 for DAS feel, but degrades.

Note the issue numbers are still open upstream; check state with `gh` rather
than trusting this table if it looks stale.

---

## Decisions already made (don't relitigate)

These are settled and recorded in [DESIGN.md](DESIGN.md) with reasoning:

- **Suite, not one long-form game** — each game is an independent vertical slice
  that dogfoods one subsystem, so a blocked game blocks only itself.
- **Roster ordered by increasing framework risk** — Minesweeper needs nothing,
  Snake forced #58, Solitaire is the flagship.
- **Explicit game registration**, never self-registering statics.
- **SFX synthesized, not sampled** — no decoder, no asset pipeline, no blobs in
  git. Venice is for sprite/title art instead.
- **Audio behind an `AudioSink`** with `WavFileSink` for offline testing, because
  the dev container has no sound hardware.
- **Port logic from HTML-Games** rather than reinventing game rules.

---

## Environment facts that bite

- **No audio hardware in the dev container** — no `/dev/snd`, no PulseAudio,
  `librtaudio-dev` not installed (available in Ubuntu noble universe). The
  maintainer verifies audio on their own machine. Never report audio as verified
  from here.
- **Two forges.** term-game and HTML-Games are gitea (`tea`); termforge and
  cpp-template are GitHub (`gh`). See [AGENTS.md](AGENTS.md).
- `arcadectl` on gitea is a **k8s controller for game-server deployments** —
  unrelated to this project despite the name.
