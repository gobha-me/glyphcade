# term-game

A TUI arcade suite in **C++23**. [TermForge](https://github.com/gobha-me/termforge)
renders it, [RtAudio](https://github.com/thestk/rtaudio) sounds it.

Sibling to [HTML-Games](https://git.gobha.me/xcaliber/HTML-Games) — same
arcade-of-small-games idea and the same roster to raid, but terminal-native and
compiled instead of HTML/CSS/JS. One binary, a selector, and a set of
self-contained games.

> **Status: the arcade runs, there is a game in it, and it makes noise.** Pick
> Minesweeper from the selector and play it — mouse or keyboard, three
> difficulties, timer and mine counter, pause and quit-to-menu. Playable on a
> bare 7-bit terminal with no colour. Sound is synthesized, not sampled, and
> degrades to silence rather than failing. See [STATUS.md](STATUS.md)
> for live state, [DESIGN.md](DESIGN.md) for the architecture, and the issue
> tracker for the epic breakdown.

**CI:** [five build arms](https://git.gobha.me/xcaliber/term-game/actions?workflow=ci.yaml)
— gcc and clang, plus ASan, UBSan and TSan — on every push and pull request,
each in a pinned `debian:trixie` container with `-Werror` and
`TERMGAME_WITH_AUDIO=OFF`.

<!-- A status *badge* would be the convention here, and there is deliberately
     none. git.gobha.me does not serve badge routes: the URL Gitea's own API
     advertises as this workflow's badge_url returns 404, as do /badges/
     release.svg, issues.svg and stars.svg, and as does the equivalent URL for
     a sibling repo whose workflow has been succeeding for months. So this is
     an instance setting, not something this repo can fix. Tracked in gitea
     #21; swap this paragraph for the badge the day those routes answer. A
     linked word beats an image that cannot load. -->



## The idea

Small games, escalating deliberately. Each one dogfoods a different part of
TermForge — mouse routing, animation, real-time ticks, tile maps, inline pixel
graphics — so framework gaps are found by a real consumer rather than guessed at.
The suite doubles as the honest test of TermForge's pitch: notcurses-class inline
graphics from a stdlib-only C++23 API.

| # | Game | Dogfoods |
|---|------|----------|
| 1 | Shell + selector ✓ | ListWidget, Frame, dialogs, FocusRing, `on_tick` |
| 2 | Minesweeper ✓ | mouse routing, grid rendering |
| 3 | 2048 ✓ | tween animation |
| 4 | Snake ✓ | real-time tick |
| 5 | Tetris | held-key feel |
| 6 | Sokoban | tile maps |
| 7 | Solitaire | Kitty sprites, mouse drag-and-drop |

Every game is playable at the bottom tier. Pixel sprites are an enhancement over
a glyph fallback that always exists — never a requirement.

## Audio

Sound effects are **synthesized, not sampled** — square/triangle/noise
oscillators with ADSR envelopes. No WAV decoder, no asset pipeline, no binary
blobs in git, and it is the right sound for an arcade.

The device is abstracted behind an `AudioSink` with three implementations:
`RtAudioSink` (real hardware), `NullSink` (no sound card present), and
`WavFileSink` (renders to disk, which is what makes the audio path testable
offline). `TERMGAME_WITH_AUDIO` auto-detects rtaudio and defaults OFF when it is
absent, so the repo builds and tests anywhere.

## Build

```bash
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Requires CMake 3.28+ and a C++23 compiler (GCC 13+ / Clang 19+). TermForge is
fetched automatically if it is not already installed. `librtaudio-dev` is
optional — without it you get a silent but fully playable arcade; force the
no-audio build with `-DTERMGAME_WITH_AUDIO=OFF`.

## License

[MIT](LICENSE.md).
