// The sinks: NullSink's contract, and WavFileSink's file format.
//
// ⚠ These are the ONLY bit-exact assertions in the audio subsystem, and that is
// not an accident — everything here is integer header arithmetic and integer
// sample conversion, so it is specified rather than merely reproducible. The
// synth's output is asserted very differently, for reasons test/18audio-synth
// explains at length.
//
// ⚠ No termforge header, no Screen. sink.hpp names no termforge type — the
// error channel is std::string precisely so it does not — so this file
// structurally cannot construct one. Same discipline as test/14minesweeper.
//
// Files go to std::filesystem::temp_directory_path() under a per-case name and
// are removed by a scope guard, so a failing case leaves nothing behind and two
// cases cannot collide.
//
// Two claims here were mutation-tested rather than assumed:
//
//   * removing the std::fill_n in WavFileSink::render turns "clears between
//     chunks" and "render is cumulative" red. Notably it does NOT turn the
//     disk-equals-mirror case red — both sides of that comparison come from the
//     same scratch buffer — which is why the chunking claim needs its own case
//     rather than riding on that one.
//   * scaling by 32768 instead of 32767 in to_pcm16 turns the clamp case red.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <glyphcade/audio/sink.hpp>

namespace {

using namespace glyphcade::audio;

// Removes its file on the way out, however the case ends.
class TempWav {
 public:
  explicit TempWav(const std::string& name)
      : m_path(std::filesystem::temp_directory_path() /
               ("glyphcade-test-" + name + ".wav")) {
    std::filesystem::remove(m_path);
  }
  ~TempWav() {
    std::error_code ec;  // noexcept: a destructor must not throw
    std::filesystem::remove(m_path, ec);
  }
  TempWav(const TempWav&) = delete;
  auto operator=(const TempWav&) -> TempWav& = delete;

  [[nodiscard]] auto path() const -> const std::filesystem::path& {
    return m_path;
  }

 private:
  std::filesystem::path m_path;
};

// A render function that writes a known, position-dependent ramp, so that a
// chunking bug shows up as a discontinuity rather than as silence. It ADDS,
// like every real renderer, which is what makes the "cleared every chunk"
// behaviour in WavFileSink::render assertable.
struct Ramp {
  int calls{0};
  int frames_seen{0};
  int next{0};

  static auto fn(float* out, int frames, int channels, void* user) noexcept
      -> void {
    auto* self = static_cast<Ramp*>(user);
    ++self->calls;
    self->frames_seen += frames;
    for (int f = 0; f < frames; ++f) {
      // A slow ramp that never reaches full scale, so nothing clamps.
      const float v = static_cast<float>(self->next % 1000) / 2000.0F;
      for (int c = 0; c < channels; ++c) out[(f * channels) + c] += v;
      ++self->next;
    }
  }
};

[[nodiscard]] auto read_file(const std::filesystem::path& p)
    -> std::vector<unsigned char> {
  std::ifstream in(p, std::ios::binary);
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                    std::istreambuf_iterator<char>());
}

[[nodiscard]] auto u32_at(const std::vector<unsigned char>& b, std::size_t at)
    -> std::uint32_t {
  return static_cast<std::uint32_t>(b[at]) |
         (static_cast<std::uint32_t>(b[at + 1]) << 8) |
         (static_cast<std::uint32_t>(b[at + 2]) << 16) |
         (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

[[nodiscard]] auto u16_at(const std::vector<unsigned char>& b, std::size_t at)
    -> std::uint16_t {
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(b[at]) |
                                    (static_cast<std::uint16_t>(b[at + 1])
                                     << 8));
}

[[nodiscard]] auto tag_at(const std::vector<unsigned char>& b, std::size_t at)
    -> std::string {
  return std::string(reinterpret_cast<const char*>(b.data()) + at, 4);
}

}  // namespace

// ── to_pcm16 ────────────────────────────────────────────────────────────────

TEST_CASE("to_pcm16 clamps at full scale without wrapping", "[audio][sink]") {
  // ⚠ The case that matters. Scaling by 32768 instead of 32767 lets +1.0 land
  // on 32768, which wraps to -32768: a full-scale POSITIVE sample rendering as
  // full-scale negative, i.e. the loudest click the format can express. It
  // would be inaudible in a quiet effect and awful in a loud one.
  REQUIRE(to_pcm16(1.0F) == 32767);
  REQUIRE(to_pcm16(-1.0F) == -32767);
  REQUIRE(to_pcm16(2.0F) == 32767);
  REQUIRE(to_pcm16(-2.0F) == -32767);
  REQUIRE(to_pcm16(0.0F) == 0);
}

TEST_CASE("to_pcm16 rounds symmetrically", "[audio][sink]") {
  REQUIRE(to_pcm16(0.5F) == 16384);
  REQUIRE(to_pcm16(-0.5F) == -16384);
  REQUIRE(to_pcm16(0.25F) == 8192);
  REQUIRE(to_pcm16(-0.25F) == -8192);
  // Rounds away from zero at the half, on both sides — no bias toward one.
  REQUIRE(to_pcm16(1.0F / 65534.0F) == 1);
  REQUIRE(to_pcm16(-1.0F / 65534.0F) == -1);
}

// ── NullSink ────────────────────────────────────────────────────────────────

TEST_CASE("NullSink opens, reports Discard, and never pulls",
          "[audio][sink]") {
  // ⚠ "Never pulls" is the contract Engine::play() relies on to short-circuit.
  // If a NullSink ever started calling its render function, the ring would
  // drain and the short-circuit would become a silent behaviour change.
  NullSink sink;
  Ramp ramp;

  const SinkFormat want{.sample_rate = 48000, .channels = 1,
                        .frames_per_buffer = 256};
  const auto opened = sink.open(want, &Ramp::fn, &ramp);

  REQUIRE(opened.has_value());
  REQUIRE(sink.kind() == SinkKind::Discard);
  REQUIRE(sink.name() == "null");
  REQUIRE(sink.format().sample_rate == 48000);
  REQUIRE(sink.format().frames_per_buffer == 256);
  REQUIRE(ramp.calls == 0);

  sink.close();
  REQUIRE(ramp.calls == 0);
}

// ── WavFileSink ─────────────────────────────────────────────────────────────

TEST_CASE("WavFileSink writes a canonical 44-byte PCM header",
          "[audio][sink]") {
  const TempWav tmp("header");
  constexpr int kFrames = 1000;

  {
    WavFileSink sink(tmp.path());
    Ramp ramp;
    REQUIRE(sink.open({.sample_rate = 48000, .channels = 1,
                       .frames_per_buffer = 256},
                      &Ramp::fn, &ramp)
                .has_value());
    REQUIRE(sink.render(kFrames) == kFrames);
  }  // destructor closes and back-patches

  const auto bytes = read_file(tmp.path());
  const std::uint32_t data_bytes = kFrames * 1 * 2;

  REQUIRE(bytes.size() == 44 + data_bytes);

  // Field by field, at the offsets every hex dump and every reference uses.
  REQUIRE(tag_at(bytes, 0) == "RIFF");
  REQUIRE(u32_at(bytes, 4) == 36 + data_bytes);
  REQUIRE(tag_at(bytes, 8) == "WAVE");
  REQUIRE(tag_at(bytes, 12) == "fmt ");
  REQUIRE(u32_at(bytes, 16) == 16);  // PCM fmt chunk size
  REQUIRE(u16_at(bytes, 20) == 1);   // format tag: uncompressed PCM
  REQUIRE(u16_at(bytes, 22) == 1);   // channels
  REQUIRE(u32_at(bytes, 24) == 48000);
  REQUIRE(u32_at(bytes, 28) == 48000 * 2);  // byte rate = rate * block align
  REQUIRE(u16_at(bytes, 32) == 2);          // block align = channels * 2
  REQUIRE(u16_at(bytes, 34) == 16);         // bits per sample
  REQUIRE(tag_at(bytes, 36) == "data");
  REQUIRE(u32_at(bytes, 40) == data_bytes);
}

TEST_CASE("WavFileSink header tracks channels and rate", "[audio][sink]") {
  // The same arithmetic with different inputs, so a hardcoded 48000 or a
  // hardcoded mono block align cannot pass the case above by luck.
  const TempWav tmp("stereo");
  constexpr int kFrames = 300;

  {
    WavFileSink sink(tmp.path());
    Ramp ramp;
    REQUIRE(sink.open({.sample_rate = 44100, .channels = 2,
                       .frames_per_buffer = 128},
                      &Ramp::fn, &ramp)
                .has_value());
    REQUIRE(sink.render(kFrames) == kFrames);
  }

  const auto bytes = read_file(tmp.path());
  const std::uint32_t data_bytes = kFrames * 2 * 2;

  REQUIRE(u16_at(bytes, 22) == 2);
  REQUIRE(u32_at(bytes, 24) == 44100);
  REQUIRE(u16_at(bytes, 32) == 4);           // block align = 2ch * 2 bytes
  REQUIRE(u32_at(bytes, 28) == 44100 * 4);   // byte rate
  REQUIRE(u32_at(bytes, 40) == data_bytes);
  REQUIRE(bytes.size() == 44 + data_bytes);
}

TEST_CASE("a zero-frame wav is still a valid RIFF file", "[audio][sink]") {
  // Opening and closing without rendering must not produce a truncated or
  // header-less file. This is what happens on the OFF arm if a WavFileSink is
  // ever wired up and nothing plays.
  const TempWav tmp("empty");

  {
    WavFileSink sink(tmp.path());
    Ramp ramp;
    REQUIRE(sink.open({}, &Ramp::fn, &ramp).has_value());
  }

  const auto bytes = read_file(tmp.path());
  REQUIRE(bytes.size() == 44);
  REQUIRE(tag_at(bytes, 0) == "RIFF");
  REQUIRE(u32_at(bytes, 4) == 36);
  REQUIRE(u32_at(bytes, 40) == 0);
}

TEST_CASE("the file on disk equals the in-memory mirror", "[audio][sink]") {
  // ⚠ The case that catches a self-consistent header bug. Every assertion above
  // reads the file the same way write_header() wrote it, so a wrong-but-uniform
  // convention would satisfy all of them. This one compares the payload against
  // a value produced by a completely different path — to_pcm16 on the samples
  // the renderer emitted — so the two can only agree if both are right.
  const TempWav tmp("mirror");
  constexpr int kFrames = 777;  // not a multiple of the block, on purpose

  std::vector<std::int16_t> mirror;
  {
    WavFileSink sink(tmp.path());
    Ramp ramp;
    REQUIRE(sink.open({.sample_rate = 48000, .channels = 1,
                       .frames_per_buffer = 256},
                      &Ramp::fn, &ramp)
                .has_value());
    REQUIRE(sink.render(kFrames) == kFrames);
    REQUIRE(sink.frames_written() == kFrames);

    mirror.assign(sink.samples().begin(), sink.samples().end());
  }

  REQUIRE(mirror.size() == static_cast<std::size_t>(kFrames));

  const auto bytes = read_file(tmp.path());
  REQUIRE(bytes.size() == 44 + (mirror.size() * 2));

  for (std::size_t i = 0; i < mirror.size(); ++i) {
    const auto on_disk =
        static_cast<std::int16_t>(u16_at(bytes, 44 + (i * 2)));
    REQUIRE(on_disk == mirror[i]);
  }
}

TEST_CASE("render chunks by frames_per_buffer and clears between chunks",
          "[audio][sink]") {
  // ⚠ The renderer ADDS into the buffer, because that is what lets voices mix
  // by summation. So the sink must clear each chunk before pulling. If it did
  // not, chunk N would still hold chunk N-1's samples and every block would be
  // louder than the last — an echo that grows, which is a memorable way to find
  // this line missing but a slow one.
  const TempWav tmp("chunks");
  constexpr int kBlock = 64;
  constexpr int kFrames = 200;  // 3 full blocks + a partial one

  WavFileSink sink(tmp.path());
  Ramp ramp;
  REQUIRE(sink.open({.sample_rate = 48000, .channels = 1,
                     .frames_per_buffer = kBlock},
                    &Ramp::fn, &ramp)
              .has_value());
  REQUIRE(sink.render(kFrames) == kFrames);

  REQUIRE(ramp.calls == 4);            // 64 + 64 + 64 + 8
  REQUIRE(ramp.frames_seen == kFrames);

  // The ramp is a pure function of the frame index, so each written sample must
  // equal to_pcm16 of the value the renderer produced for that index — nothing
  // accumulated across chunk boundaries.
  const auto samples = sink.samples();
  REQUIRE(samples.size() == static_cast<std::size_t>(kFrames));
  for (int i = 0; i < kFrames; ++i) {
    const float expect = static_cast<float>(i % 1000) / 2000.0F;
    REQUIRE(samples[static_cast<std::size_t>(i)] == to_pcm16(expect));
  }
}

TEST_CASE("render is cumulative across calls", "[audio][sink]") {
  // Rendering N then M must equal rendering N+M: the sink holds no per-call
  // state beyond its position. This is what lets Engine::pump be called once
  // per game tick rather than once per session.
  const TempWav tmp("cumulative");

  WavFileSink sink(tmp.path());
  Ramp ramp;
  REQUIRE(sink.open({.sample_rate = 48000, .channels = 1,
                     .frames_per_buffer = 32},
                    &Ramp::fn, &ramp)
              .has_value());

  REQUIRE(sink.render(100) == 100);
  REQUIRE(sink.frames_written() == 100);
  REQUIRE(sink.render(50) == 50);
  REQUIRE(sink.frames_written() == 150);
  REQUIRE(sink.samples().size() == 150);

  for (int i = 0; i < 150; ++i) {
    const float expect = static_cast<float>(i % 1000) / 2000.0F;
    REQUIRE(sink.samples()[static_cast<std::size_t>(i)] == to_pcm16(expect));
  }
}

TEST_CASE("WavFileSink refuses a nonsensical open", "[audio][sink]") {
  // Failures are the feature (AGENTS.md). Each of these returns an error rather
  // than producing a file that no player can open.
  const TempWav tmp("bad");

  {
    WavFileSink sink(tmp.path());
    REQUIRE_FALSE(sink.open({}, nullptr, nullptr).has_value());
  }
  {
    WavFileSink sink(tmp.path());
    Ramp ramp;
    REQUIRE_FALSE(sink.open({.sample_rate = 0, .channels = 1,
                             .frames_per_buffer = 64},
                            &Ramp::fn, &ramp)
                      .has_value());
  }
  {
    WavFileSink sink(tmp.path());
    Ramp ramp;
    REQUIRE(sink.open({}, &Ramp::fn, &ramp).has_value());
    REQUIRE_FALSE(sink.open({}, &Ramp::fn, &ramp).has_value());  // already open
  }
}

TEST_CASE("rendering a closed sink is a no-op, not a crash", "[audio][sink]") {
  const TempWav tmp("closed");
  WavFileSink sink(tmp.path());
  Ramp ramp;

  REQUIRE(sink.render(64) == 0);  // never opened
  REQUIRE(sink.open({}, &Ramp::fn, &ramp).has_value());
  REQUIRE(sink.render(64) == 64);
  sink.close();
  REQUIRE(sink.render(64) == 0);  // closed again
  REQUIRE(sink.frames_written() == 64);
  REQUIRE(ramp.frames_seen == 64);
}
