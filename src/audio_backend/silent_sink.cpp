// The make_device_sink() implementation compiled when GLYPHCADE_WITH_AUDIO=OFF.
//
// ⚠ This file and rtaudio_sink.cpp are alternatives, chosen by CMake, never both
// compiled. That is what keeps main.cpp free of preprocessor branches and keeps
// the two build arms differing by a source file rather than by #ifdefs spread
// through the tree.
//
// There is nothing conditional inside it, deliberately: a build with no backend
// has no device to try, so the honest answer is a NullSink and no diagnostic.
// "This build has no audio" is already reported by glyphcade::build_has_audio(),
// and the Shell deliberately does NOT raise a degradation notice for it — see
// the note in Shell::sync_capabilities for why a permanent footer line on every
// CI run would be noise rather than an event.

#include <glyphcade/audio/device_sink.hpp>

#include <cstdlib>
#include <memory>

namespace glyphcade::audio {

auto wav_path_from_env() -> const char* {
  const char* path = std::getenv("GLYPHCADE_AUDIO_WAV");  // NOLINT(concurrency-mt-unsafe)
  return (path != nullptr && path[0] != '\0') ? path : nullptr;
}

auto make_device_sink() -> std::unique_ptr<AudioSink> {
  // Available on this arm too, and that is the point: a box that cannot build
  // the RtAudio backend can still produce something audible for someone else.
  if (const char* path = wav_path_from_env(); path != nullptr) {
    return std::make_unique<WavFileSink>(path);
  }
  return std::make_unique<NullSink>();
}

}  // namespace glyphcade::audio
