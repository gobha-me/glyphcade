// term-game — the single binary.
//
// Everything is in the Shell: the terminal, the loop, the selector, the game
// registry. This file should stay roughly this size forever.

#include <termgame/arcade/exception_boundary.hpp>
#include <termgame/arcade/shell.hpp>

// ⚠ NOT under include/ — a private header owned by src/audio_backend/, which is
// never installed and never exported (gitea #13). It declares one function, and
// CMake decides which translation unit defines it: the RtAudio backend when
// TERMGAME_WITH_AUDIO is on, a NullSink when it is not.
//
// That is why there is no #ifdef in this file. The two build arms differ by a
// source file, not by a preprocessor branch here.
#include <termgame/audio/device_sink.hpp>

// ⚠ NOT under include/. This header belongs to src/audio_backend/, a target that
// is never installed and never exported (gitea #13), and it is reachable here
// only because src/bin links that target. There is deliberately no #ifdef
// TERMGAME_WITH_AUDIO in this file: CMake picks which translation unit DEFINES
// make_device_sink(), so this file is byte-identical in both build arms.
#include <termgame/audio/device_sink.hpp>

auto main() -> int {
  return termgame::run_or_report([] {
    // Constructed inside the callable, which is no longer a *requirement* — it
    // was, until termforge v0.1.10, because unwinding into the catch below
    // was the only thing that ran teardown(). run() tears the terminal down
    // itself now, on every path out. Kept anyway, for a smaller reason that
    // still holds: `app`'s lifetime is then strictly inside the try, so
    // ~Shell runs before the diagnostic prints. teardown() is idempotent, so
    // that is free belt-and-braces rather than a second restore.
    //
    // The point of saying so: do not inherit an expired constraint from this
    // file. Hoisting `app` out would be harmless today.
    // The Shell owns the engine but does not choose its sink — which is what
    // keeps rtaudio out of the library, and what lets a test hand it a
    // WavFileSink instead. make_device_sink() never returns null; a device that
    // cannot be opened reports itself through the sink's own open(), and the
    // Shell degrades to silence with a notice.
    termgame::Shell app{termgame::audio::make_device_sink()};
    return app.run();
  });
}
