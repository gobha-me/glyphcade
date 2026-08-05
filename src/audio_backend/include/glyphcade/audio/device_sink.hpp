#pragma once

// glyphcade — whatever output this build can actually talk to.
//
// ⚠ THIS HEADER IS NOT UNDER include/ AND IS NEVER INSTALLED, and that is the
// whole of term-game#13's decision expressed as a file path. It lives in a private
// include root owned by src/audio_backend/, so there is no way to install it by
// accident and no way for a downstream consumer to reach a symbol that is not
// in the exported library.
//
// The rest of the audio subsystem lives in include/glyphcade/audio/ and links no
// rtaudio at all. Only this target does, and this target is never exported. See
// the long note at the bottom of cmake/audio.cmake for what that avoids.
//
// ⚠ There is no #ifdef GLYPHCADE_WITH_AUDIO anywhere in this file, or in
// src/bin/main.cpp. CMake chooses which translation unit DEFINES the function
// below — rtaudio_sink.cpp when the backend is on, silent_sink.cpp when it is
// not — which is the same trick build_has_audio() already plays, and it means
// the two arms differ by a source file rather than by preprocessor branches
// scattered through the tree.

#include <memory>

#include <glyphcade/audio/sink.hpp>

namespace glyphcade::audio {

// The best output this build and this machine can provide. NEVER null.
//
// Failure to reach a device is NOT reported here — the returned sink's open()
// reports it, so there is exactly one degradation path and the Shell has
// exactly one place to translate it into an ErrorEvent. That also means this
// container, which has librtaudio-dev and no /dev/snd, exercises the failure
// path for real rather than hypothetically.
[[nodiscard]] auto make_device_sink() -> std::unique_ptr<AudioSink>;

// ── GLYPHCADE_AUDIO_WAV ──────────────────────────────────────────────────────
//
// If the environment names a path, both arms return a WavFileSink writing there
// instead of talking to a device.
//
// ⚠ This is not a debugging leftover. AGENTS.md states flatly that audio needs a
// human to hear it and that this container cannot provide one — and that was
// true of every commit in Epic 2 until this hook existed. With it, a machine
// with no sound hardware can render a real session to a file and send it to
// someone who can listen:
//
//     GLYPHCADE_AUDIO_WAV=/tmp/session.wav ./build/src/bin/glyphcade
//
// It works because Shell::on_tick pumps an offline sink once per frame, so the
// file follows the game clock rather than wall time.
[[nodiscard]] auto wav_path_from_env() -> const char*;

}  // namespace glyphcade::audio
