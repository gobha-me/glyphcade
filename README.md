# glyphcade

[![CI](https://github.com/gobha-me/glyphcade/actions/workflows/ci.yml/badge.svg)](https://github.com/gobha-me/glyphcade/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)

A terminal arcade in **C++23**. One binary, a selector, and five self-contained
games. [TermForge](https://github.com/gobha-me/termforge) renders it, optional
[RtAudio](https://github.com/thestk/rtaudio) sounds it.

**Minesweeper · 2048 · Snake · Tetris · Sokoban** — mouse or keyboard, high
scores that persist, pause and quit-to-menu. All of it playable on a bare 7-bit
terminal with no colour, no mouse and no Unicode.

## Install

Grab a package from the [latest release](https://github.com/gobha-me/glyphcade/releases):

```bash
# Debian / Ubuntu — apt resolves the audio library from the archive
sudo apt install ./glyphcade_<version>_amd64.deb

# Fedora / RHEL
sudo dnf install ./glyphcade-<version>.x86_64.rpm

# anywhere else
tar xzf glyphcade-<version>-Linux-x86_64.tar.gz && ./glyphcade-*/bin/glyphcade
```

Then run `glyphcade`.

The `.deb` and `.rpm` contain the game and nothing else. The tarball is the whole
install tree — it also carries the static libraries, headers and CMake package
config described under [Use it as a library](#use-it-as-a-library).

## Build

```bash
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
./build/src/bin/glyphcade
```

Requires **CMake 3.28+** and a **C++23** compiler (GCC 13+ / Clang 19+ — clang 18
and earlier cannot compile `std::expected` against libstdc++). TermForge is
fetched automatically if it is not already installed; there is nothing else to
install.

`librtaudio-dev` is optional. Without it you get a silent but fully playable
arcade — detection is automatic, and `-DGLYPHCADE_WITH_AUDIO=OFF` forces the
no-audio build, which is the configuration CI runs on every arm.

## Use it as a library

```cmake
find_package(glyphcade CONFIG REQUIRED)
target_link_libraries(app PRIVATE glyphcade::lib)
```

Individual games are exported too — `glyphcade::game_tetris`,
`glyphcade::core`, and so on. A ctest (`consumer-resolves`) proves the installed
package is genuinely resolvable by a real `find_package`, rather than only
checking that the files were written.

## The idea

Small games, escalating deliberately. Each one dogfoods a different part of
TermForge — mouse routing, animation, real-time ticks, tile maps, inline pixel
graphics — so framework gaps get found by a real consumer instead of guessed at.
The suite doubles as the honest test of TermForge's pitch: notcurses-class inline
graphics from a stdlib-only C++23 API.

| # | Game | Dogfoods |
|---|------|----------|
| 1 | Shell + selector ✓ | ListWidget, Frame, dialogs, FocusRing, `on_tick` |
| 2 | Minesweeper ✓ | mouse routing, grid rendering |
| 3 | 2048 ✓ | tween animation |
| 4 | Snake ✓ | real-time tick |
| 5 | Tetris ✓ | held-key feel, the kitty keyboard protocol |
| 6 | Sokoban ✓ | tile maps, the camera, MapWidget's first consumer |
| 7 | Solitaire | Kitty sprites, mouse drag-and-drop |

Two rules hold across all of them:

- **Every game is playable at the bottom tier.** Pixel sprites are an
  enhancement over a glyph fallback that always exists — never a requirement.
- **Rules extent never scales.** A bigger terminal buys a bigger *view*, never a
  bigger board. Board size *is* the game.

## Audio

Sound effects are **synthesized, not sampled** — square/triangle/noise
oscillators with ADSR envelopes. No decoder, no asset pipeline, no binary blobs
in git, and it is the right sound for an arcade.

The device sits behind an `AudioSink` with three implementations: `RtAudioSink`
(hardware), `NullSink` (no card present), and `WavFileSink` (renders to disk,
which is what makes the audio path testable with no sound hardware at all).

## Testing

Roughly 14k lines of tests against 15k lines of source, 32 ctest targets. CI runs
five arms on every push and pull request — gcc, clang, ASan, UBSan and TSan —
each in a pinned `debian:trixie` container with `-Werror`.

Games are driven headlessly: fixed timestep with a clamped delta means logic
advances by *N* ticks with no `Screen` and no TTY, and rendering is asserted by
reading cells back out of an off-screen buffer. Terminal restore is checked in a
real pty via `script(1)`.

What that does **not** cover is *feel*, and [STATUS.md](STATUS.md) is explicit
about it. Nothing in CI can hear a sound or judge whether Tetris's DAS timing is
right; those need hands on a keyboard.

## Documentation

- [STATUS.md](STATUS.md) — live state, what is verified and what is not
- [DESIGN.md](DESIGN.md) — architecture
- [AGENTS.md](AGENTS.md) — conventions, hard rules, and how to verify a change
- [CONTRIBUTING.md](CONTRIBUTING.md) — start here to send a patch

## License

[MIT](LICENSE.md).
