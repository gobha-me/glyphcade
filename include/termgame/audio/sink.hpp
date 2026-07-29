#pragma once

// term-game — AudioSink: the device abstraction, and the reason this whole
// subsystem is testable on a machine that cannot make a sound.
//
// DESIGN.md calls WavFileSink "the interesting one", and that is right: it makes
// the entire audio path deterministically testable offline, which is the same
// discipline termforge applies to its drivers. The abstraction is load-bearing
// from the first commit, not speculative — this container has librtaudio-dev
// and no /dev/snd, so the offline path is the ONLY one anything here can judge.
//
// ⚠ THIS HEADER DELIBERATELY INCLUDES NO TERMFORGE HEADER, and must not start.
// The error type is std::string rather than termforge::ErrorEvent for exactly
// that reason; the Shell translates one into the other, because the Shell is
// the layer that owns a terminal. Same discipline as board.hpp and ring.hpp.
//
// ⚠ AND NO #ifdef TERMGAME_WITH_AUDIO, here or anywhere else under include/.
// That is an AGENTS.md hard rule: the build's audio configuration is reported at
// runtime through termgame::build_has_audio(), never compiled into the shape of
// a public header. RtAudioSink is not declared here at all — it lives in
// src/audio_backend/, outside the installed headers entirely (gitea #13).

#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace termgame::audio {

struct SinkFormat {
  // 48000, not 44100, for two reasons that agree: it is the modern ALSA/Pulse
  // default, so a device is less likely to resample us — and 48000 / 60 == 800
  // exactly, which is what lets the offline pump render a whole number of
  // frames per game tick with no accumulating remainder. See Engine::pump.
  int sample_rate{48000};
  int channels{1};
  // A REQUEST, not a promise. A device grants what it likes; open() writes the
  // granted value back, and callers must read it from format() afterwards
  // rather than assuming they got what they asked for.
  int frames_per_buffer{256};
};

// Which of the three implementations a sink is, without a dynamic_cast.
//
// This is not decoration: Engine::play() branches on it. A Discard sink has no
// consumer, so posting to it would fill the ring once and then climb the
// dropped() counter forever — turning the single most useful diagnostic in the
// subsystem into noise, on the very configuration CI runs.
enum class SinkKind : std::uint8_t {
  Device,   // a real output; pulls itself, on its own realtime thread
  Offline,  // rendered by its caller; no thread exists
  Discard,  // consumes nothing and is never pulled at all
};

// ⚠ CALLED ON THE AUDIO THREAD by a Device sink. Everything AGENTS.md forbids
// in the callback is forbidden inside this function: no locks, no allocation,
// no syscalls, no I/O.
//
// A raw function pointer, not std::function: assigning a std::function may
// allocate, and calling one is an indirect branch through a type-erased target.
// Neither belongs here. `user` carries the Engine.
using RenderFn = auto (*)(float* out, int frames, int channels,
                          void* user) noexcept -> void;

class AudioSink {
 public:
  virtual ~AudioSink();

  AudioSink(const AudioSink&) = delete;
  auto operator=(const AudioSink&) -> AudioSink& = delete;

  // Begin producing. On success the sink is running and may already have called
  // `fn` before this returns — which is why Engine::open() finishes wiring
  // itself up BEFORE calling this.
  [[nodiscard]] virtual auto open(const SinkFormat& want, RenderFn fn,
                                  void* user)
      -> std::expected<void, std::string> = 0;

  virtual auto close() noexcept -> void = 0;

  [[nodiscard]] virtual auto kind() const noexcept -> SinkKind = 0;
  [[nodiscard]] virtual auto name() const noexcept -> std::string_view = 0;
  // The GRANTED format, valid after a successful open().
  [[nodiscard]] virtual auto format() const noexcept -> const SinkFormat& = 0;

 protected:
  AudioSink() = default;
};

// Consumes and discards. Lets the whole repo build and run on a box with no
// sound hardware — which is every CI runner and this dev container.
//
// It never calls the render function at all. That is the honest shape of "there
// is no output": pretending to pull would spin a thread to throw samples away.
class NullSink final : public AudioSink {
 public:
  [[nodiscard]] auto open(const SinkFormat& want, RenderFn fn, void* user)
      -> std::expected<void, std::string> override;
  auto close() noexcept -> void override;

  [[nodiscard]] auto kind() const noexcept -> SinkKind override {
    return SinkKind::Discard;
  }
  [[nodiscard]] auto name() const noexcept -> std::string_view override {
    return "null";
  }
  [[nodiscard]] auto format() const noexcept -> const SinkFormat& override {
    return m_format;
  }

 private:
  SinkFormat m_format{};
};

// Renders the mix to a 16-bit PCM WAV file.
//
// ⚠ DRIVEN, NOT THREADED, and that is the entire point. A device pulls itself
// from a realtime thread; this one produces nothing until its caller asks for
// frames. So the UI thread is both producer and consumer — still one of each,
// still a legal SPSC pairing — and the output is a pure function of the
// commands posted and the number of frames requested. That is what makes a
// golden assertion possible at all.
class WavFileSink final : public AudioSink {
 public:
  explicit WavFileSink(std::filesystem::path path);
  ~WavFileSink() override;  // closes, back-patching the RIFF sizes

  [[nodiscard]] auto open(const SinkFormat& want, RenderFn fn, void* user)
      -> std::expected<void, std::string> override;
  auto close() noexcept -> void override;

  // ⚠ NOT on AudioSink, deliberately. Only an offline sink is driven by its
  // caller; hoisting this to the base class would invite someone to call it on
  // a device sink from the UI thread, which is a data race with that device's
  // own callback. Returns the number of frames actually rendered.
  auto render(int frames) -> int;

  [[nodiscard]] auto kind() const noexcept -> SinkKind override {
    return SinkKind::Offline;
  }
  [[nodiscard]] auto name() const noexcept -> std::string_view override {
    return "wav";
  }
  [[nodiscard]] auto format() const noexcept -> const SinkFormat& override {
    return m_format;
  }

  [[nodiscard]] auto path() const noexcept -> const std::filesystem::path& {
    return m_path;
  }
  [[nodiscard]] auto frames_written() const noexcept -> std::int64_t {
    return m_frames;
  }

  // An in-memory mirror of every sample written. Offline sinks exist to be
  // asserted on and memory is not a constraint in a test, so keeping both lets
  // one case prove the bytes on disk equal the bytes we rendered — which is the
  // only way to catch a header-arithmetic bug that happens to be
  // self-consistent.
  [[nodiscard]] auto samples() const noexcept -> std::span<const std::int16_t> {
    return m_mirror;
  }

  // The fixed size of a canonical PCM WAV header. Public because the test
  // asserts the file layout field by field against it.
  static constexpr std::streamoff kHeaderBytes = 44;

 private:
  auto write_header(std::uint32_t data_bytes) -> void;

  std::filesystem::path m_path;
  std::ofstream m_out;
  SinkFormat m_format{};
  RenderFn m_render{nullptr};
  void* m_user{nullptr};
  std::int64_t m_frames{0};
  bool m_open{false};
  std::vector<float> m_scratch;
  std::vector<std::int16_t> m_mirror;
};

// Convert one float sample to 16-bit PCM, clamping to full scale.
//
// ⚠ No std::lround, no libm, no rint: multiply, compare, add, truncate. Every
// operation here is exactly specified by IEEE 754, so the result does not
// depend on the toolchain's libm version — the same portability argument
// board.hpp makes for hand-rolling splitmix64 instead of trusting
// std::uniform_int_distribution. See test/18audio-synth for what rests on it.
//
// 32767 rather than 32768: scaling by 32768 lets +1.0 land on 32768, which
// wraps to -32768 — a full-scale positive sample rendering as full-scale
// negative, i.e. the loudest possible click.
[[nodiscard]] constexpr auto to_pcm16(float v) noexcept -> std::int16_t {
  const float clamped = v > 1.0F ? 1.0F : (v < -1.0F ? -1.0F : v);
  const float scaled = clamped * 32767.0F;
  return static_cast<std::int16_t>(scaled >= 0.0F ? scaled + 0.5F
                                                  : scaled - 0.5F);
}

}  // namespace termgame::audio
