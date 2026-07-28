#pragma once

// term-game — build_info: what this binary was compiled as.
//
// Deliberately a pair of functions rather than macros. The build's audio
// configuration is decided by CMake (cmake/audio.cmake) and reaches the library
// as a PRIVATE compile definition, which means it is NOT visible to anything
// that includes this header — a consumer or a test that tried to #ifdef on
// TERMGAME_WITH_AUDIO would silently read it as unset and be wrong.
//
// Routing the answer through a compiled function is what makes the CMake
// decision observable, and therefore assertable: test/00bootstrap compares
// build_has_audio() against the value CMake believed, so "the option exists but
// never reached the compiler" fails a test instead of shipping.

namespace termgame {

// The project version, as configured by cmake/version.cmake from `git
// describe`. "0.0.0" on a tree with no reachable tag — see the STATUS note in
// that file, it is expected rather than an error.
[[nodiscard]] auto version_string() noexcept -> const char*;

// True iff this library was compiled with the RtAudio backend enabled.
//
// ⚠ This reports how the build was configured, not whether sound will come out.
// Nothing links rtaudio yet (Epic 2 owns the audio engine), and a machine can
// have the library without having a device — the dev container is exactly that
// case: librtaudio-dev installed, no /dev/snd.
[[nodiscard]] auto build_has_audio() noexcept -> bool;

}  // namespace termgame
