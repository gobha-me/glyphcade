// The make_device_sink() implementation compiled when TERMGAME_WITH_AUDIO=ON.
//
// ⚠ This is the ONLY translation unit in the repo that includes <RtAudio.h> or
// links rtaudio, and it belongs to a target that is never installed and never
// exported (gitea #13). Nothing under include/termgame/ knows this file exists.
//
// ⚠ AND IT HAS NEVER BEEN RUN AGAINST A DEVICE. This container has
// librtaudio-dev 5.2.0 and no /dev/snd, so what IS exercised here — every time,
// automatically — is the failure path: construction succeeds, no output device
// is found, open() reports it, and the Shell degrades to silence with a notice.
// The success path needs hardware. Say so rather than implying otherwise.

#include <termgame/audio/device_sink.hpp>

#include <cstdlib>
#include <memory>
#include <string>

#include <RtAudio.h>

namespace termgame::audio {

namespace {

// ⚠ THE VERSION DISCRIMINATOR, and it is not obvious.
//
// RtAudio 5.2.0 defines only RTAUDIO_VERSION as a STRING ("5.2.0"), which is
// useless to the preprocessor — `#if RTAUDIO_VERSION >= 6` does not compile and
// does not mean anything. RtAudio 6.0 introduced RTAUDIO_VERSION_MAJOR as an
// integer macro, so its mere existence is the test.
//
// What actually changed, and why both halves matter:
//
//                   | 5.x                         | 6.x
//   construction    | throws RtAudioError         | never throws; ctor takes a
//                   |                             | std::function error callback
//   openStream      | returns void, throws        | returns RtAudioErrorType
//   startStream     | returns void, throws        | returns RtAudioErrorType
//   stream errors   | a SEPARATE raw-fn-ptr error | the ctor's callback
//                   | callback passed to          |
//                   | openStream()                |
//   enumeration     | getDeviceCount() + index    | getDeviceIds()
//
// ⚠ The easy mistake on the 5.x path is writing only the try/catch and stopping.
// Construction/open/start failures throw, but failures DURING streaming arrive
// through the error callback instead — two different mechanisms for what reads
// as one concern.
#if defined(RTAUDIO_VERSION_MAJOR) && RTAUDIO_VERSION_MAJOR >= 6
#  define TERMGAME_RTAUDIO6 1
#else
#  define TERMGAME_RTAUDIO6 0
#endif

class RtAudioSink final : public AudioSink {
 public:
  ~RtAudioSink() override { RtAudioSink::close(); }

  [[nodiscard]] auto open(const SinkFormat& want, RenderFn fn, void* user)
      -> std::expected<void, std::string> override {
    if (fn == nullptr) return std::unexpected("no render function");
    if (m_open) return std::unexpected("already open");

    m_format = want;
    m_render = fn;
    m_user = user;

    // The buffer size the device actually granted. ⚠ Read from openStream's
    // in/out parameter, NOT from the callback: an earlier draft took it from a
    // member the callback sets, which is not written yet when open() returns —
    // and on a device that granted something other than what was asked for, the
    // engine would have been told the requested size instead of the real one.
    unsigned int granted = static_cast<unsigned int>(want.frames_per_buffer);

#if TERMGAME_RTAUDIO6
    m_dac = std::make_unique<RtAudio>(
        RtAudio::UNSPECIFIED,
        [](RtAudioErrorType, const std::string&) {
          // Deliberately swallowed. This fires on the audio thread, where
          // AGENTS.md forbids I/O — so it cannot print, and it must not throw
          // into rtaudio. A stream that dies mid-session goes quiet; the
          // alternative is a realtime-thread violation to report it.
        });

    const auto ids = m_dac->getDeviceIds();
    if (ids.empty()) return std::unexpected("no output device");

    RtAudio::StreamParameters params;
    params.deviceId = m_dac->getDefaultOutputDevice();
    params.nChannels = static_cast<unsigned int>(want.channels);
    auto frames = static_cast<unsigned int>(want.frames_per_buffer);

    if (const auto err = m_dac->openStream(
            &params, nullptr, RTAUDIO_FLOAT32,
            static_cast<unsigned int>(want.sample_rate), &frames, &callback,
            this);
        err != RTAUDIO_NO_ERROR) {
      return std::unexpected("openStream failed: " + m_dac->getErrorText());
    }
    if (const auto err = m_dac->startStream(); err != RTAUDIO_NO_ERROR) {
      m_dac->closeStream();
      return std::unexpected("startStream failed: " + m_dac->getErrorText());
    }
    granted = frames;
#else
    try {
      m_dac = std::make_unique<RtAudio>();

      if (m_dac->getDeviceCount() == 0) {
        return std::unexpected("no output device");
      }

      RtAudio::StreamParameters params;
      params.deviceId = m_dac->getDefaultOutputDevice();
      params.nChannels = static_cast<unsigned int>(want.channels);
      auto frames = static_cast<unsigned int>(want.frames_per_buffer);

      // The trailing nullptr is options; the one after it is the STREAM error
      // callback, which is the second of 5.x's two mechanisms. Passing it is
      // not optional if a mid-session failure is to be survivable.
      m_dac->openStream(&params, nullptr, RTAUDIO_FLOAT32,
                        static_cast<unsigned int>(want.sample_rate), &frames,
                        &callback, this, nullptr, &stream_error);
      m_dac->startStream();
      granted = frames;
    } catch (const RtAudioError& e) {
      m_dac.reset();
      return std::unexpected(e.getMessage());
    } catch (const std::exception& e) {
      m_dac.reset();
      return std::unexpected(std::string{e.what()});
    }
#endif

    // ⚠ Written back, because a device grants what it likes rather than what it
    // was asked for, and Engine reads this after open() to size its work.
    m_format.frames_per_buffer = static_cast<int>(granted);
    m_open = true;
    return {};
  }

  auto close() noexcept -> void override {
    if (!m_open) return;
    m_open = false;

    // noexcept, and 5.x throws from all of these — so every path is guarded. A
    // throw from here would be a throw from ~RtAudioSink during unwinding.
    try {
      if (m_dac != nullptr) {
        if (m_dac->isStreamRunning()) m_dac->stopStream();
        if (m_dac->isStreamOpen()) m_dac->closeStream();
      }
    } catch (...) {  // NOLINT(bugprone-empty-catch) — see above
    }

    m_dac.reset();
    m_render = nullptr;
    m_user = nullptr;
  }

  [[nodiscard]] auto kind() const noexcept -> SinkKind override {
    return SinkKind::Device;
  }
  [[nodiscard]] auto name() const noexcept -> std::string_view override {
    return "rtaudio";
  }
  [[nodiscard]] auto format() const noexcept -> const SinkFormat& override {
    return m_format;
  }

 private:
  // ⚠ THE AUDIO THREAD. Everything AGENTS.md forbids is forbidden below: no
  // locks, no allocation, no syscalls, no I/O. It is also a hard boundary for
  // exceptions — nothing may escape into rtaudio's own thread — which holds
  // because Engine::render is noexcept all the way down.
  static auto callback(void* out, void* /*in*/, unsigned int frames,
                       double /*stream_time*/, RtAudioStreamStatus /*status*/,
                       void* user) -> int {
    auto* self = static_cast<RtAudioSink*>(user);
    // No clearing here: Engine::render OVERWRITES its buffer, which is exactly
    // why it does rather than adding like everything below it.
    self->m_render(static_cast<float*>(out), static_cast<int>(frames),
                   self->m_format.channels, self->m_user);
    return 0;  // 0 == keep streaming
  }

#if !TERMGAME_RTAUDIO6
  static auto stream_error(RtAudioError::Type /*type*/,
                           const std::string& /*text*/) -> void {
    // Same reasoning as the 6.x lambda above: this can arrive on the audio
    // thread, so it cannot print and must not throw.
  }
#endif

  std::unique_ptr<RtAudio> m_dac;
  SinkFormat m_format{};
  RenderFn m_render{nullptr};
  void* m_user{nullptr};
  bool m_open{false};
};

}  // namespace

auto wav_path_from_env() -> const char* {
  const char* path = std::getenv("TERMGAME_AUDIO_WAV");  // NOLINT(concurrency-mt-unsafe)
  return (path != nullptr && path[0] != '\0') ? path : nullptr;
}

auto make_device_sink() -> std::unique_ptr<AudioSink> {
  // Checked before the device, so a machine WITH working sound can still render
  // a session to a file — which is how a bank tuned by ear gets compared
  // against the one it replaced.
  if (const char* path = wav_path_from_env(); path != nullptr) {
    return std::make_unique<WavFileSink>(path);
  }
  return std::make_unique<RtAudioSink>();
}

}  // namespace termgame::audio
