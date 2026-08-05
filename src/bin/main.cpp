// glyphcade — the single binary.
//
// Everything is in the Shell: the terminal, the loop, the selector, the game
// registry. This file should stay roughly this size forever.

#include <cstdlib>
#include <filesystem>
#include <string_view>

#include <glyphcade/arcade/exception_boundary.hpp>
#include <glyphcade/arcade/scores.hpp>
#include <glyphcade/arcade/shell.hpp>

// ⚠ NOT under include/. This header belongs to src/audio_backend/, a target that
// is never installed and never exported (term-game#13), and it is reachable here
// only because src/bin links that target. It declares one function, and CMake
// decides which translation unit DEFINES it: the RtAudio backend when
// GLYPHCADE_WITH_AUDIO is on, a NullSink when it is not.
//
// That is why there is deliberately no #ifdef in this file. The two build arms
// differ by a source file, so this file is byte-identical in both.
#include <glyphcade/audio/device_sink.hpp>

namespace {

// getenv, and the whole of the environment's influence on this program, lives
// HERE and nowhere else.
//
// ⚠ Not a stylistic choice. glyphcade::scores::resolve_path() takes its two
// directories as strings and core contains no getenv at all, which means THE
// LIBRARY CANNOT NAME A REAL FILE. A default-constructed Shell is therefore
// memory-only by construction, so no future edit to shell.cpp — and no test that
// forgets to inject a temp path — can start writing into a developer's $HOME.
// Enforcing that with a boundary is worth more than enforcing it with a comment.
[[nodiscard]] auto env_or_empty(const char* name) -> std::string_view {
  const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe)
  return value != nullptr ? std::string_view{value} : std::string_view{};
}

[[nodiscard]] auto scores_path() -> std::filesystem::path {
  // A verbatim override, exactly the shape GLYPHCADE_AUDIO_WAV already has in
  // src/audio_backend/. It exists for manual QA and for the AGENTS.md pty
  // recipe, and is NOT how tests get a temp file — they pass the Shell a path.
  if (const std::string_view override_path = env_or_empty("GLYPHCADE_SCORES");
      !override_path.empty()) {
    return std::filesystem::path{override_path};
  }
  return glyphcade::scores::resolve_path(env_or_empty("XDG_DATA_HOME"),
                                        env_or_empty("HOME"));
}

}  // namespace

auto main() -> int {
  return glyphcade::run_or_report([] {
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
    //
    // The scores path is the same bargain one layer over: the Shell owns the
    // store, this file decides where it lives, and an empty path (neither
    // XDG_DATA_HOME nor HOME set) is a memory-only session rather than an error.
    // Nothing is created on disk until a record is actually made.
    glyphcade::Shell app{glyphcade::audio::make_device_sink(), scores_path()};
    return app.run();
  });
}
