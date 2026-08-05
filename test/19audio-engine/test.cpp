// The Engine: ring + voice pool + sink, and the two claims that only hold when
// all three are wired together — "N voices neither clip nor drift" (AGENTS.md).
//
// ⚠ No termforge header, no Screen. engine.hpp names no termforge type, which
// is why an open() failure is reported as a std::string rather than an
// ErrorEvent. Same discipline as test/14minesweeper.
//
// The most important case in this file is the NullSink one. With a Discard sink
// nothing ever drains the ring, so play() must not post — otherwise the ring
// fills once and dropped() climbs forever on the very configuration CI runs.
// That short-circuit is easy to "simplify" away, and the only thing that would
// go wrong is a counter quietly becoming meaningless.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <system_error>
#include <vector>

#include <glyphcade/audio/engine.hpp>

namespace {

using namespace glyphcade::audio;

constexpr int kRate = 48000;
using Seconds = std::chrono::duration<double>;

class TempWav {
 public:
  explicit TempWav(const std::string& name)
      : m_path(std::filesystem::temp_directory_path() /
               ("glyphcade-engine-" + name + ".wav")) {
    std::filesystem::remove(m_path);
  }
  ~TempWav() {
    std::error_code ec;
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

// Render an engine directly, the way a device callback would.
[[nodiscard]] auto pull(Engine& e, int frames, int channels = 1)
    -> std::vector<float> {
  std::vector<float> buf(
      static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels),
      0.0F);
  e.render(buf.data(), frames, channels);
  return buf;
}

}  // namespace

// ── The Discard short-circuit ────────────────────────────────────────────────

TEST_CASE("a closed engine records intent and posts nothing",
          "[audio][engine]") {
  Engine e;
  REQUIRE_FALSE(e.is_open());
  REQUIRE(e.kind() == SinkKind::Discard);
  REQUIRE(e.sink_name() == "none");

  REQUIRE_FALSE(e.play(SfxId::Reveal));

  // Intent is still recorded — which is what makes the binding tests in
  // test/11selector and test/15minesweeper-ui work on a silent build.
  REQUIRE(e.play_count(SfxId::Reveal) == 1);
  REQUIRE(e.last_played() == static_cast<int>(SfxId::Reveal));

  const auto s = e.stats();
  REQUIRE(s.pushed == 0);
  REQUIRE(s.dropped == 0);
  REQUIRE(s.silenced == 1);
}

TEST_CASE("a NullSink engine never fills the ring", "[audio][engine]") {
  // ⚠ THE CASE THAT PINS THE SHORT-CIRCUIT. Without it, the 65th sound of the
  // session starts incrementing dropped() and every one after it does too — on
  // the GLYPHCADE_WITH_AUDIO=OFF arm, i.e. everywhere CI runs. The counter that
  // is supposed to mean "the audio thread is in trouble" would instead mean
  // "this build has no audio", which is already reported elsewhere.
  Engine e;
  REQUIRE(e.open(std::make_unique<NullSink>()).has_value());
  REQUIRE(e.kind() == SinkKind::Discard);

  for (int i = 0; i < 500; ++i) e.play(SfxId::Click);

  const auto s = e.stats();
  REQUIRE(s.pushed == 0);
  REQUIRE(s.dropped == 0);
  REQUIRE(s.silenced == 500);
  REQUIRE(e.play_count(SfxId::Click) == 500);
}

// ── Open / close ─────────────────────────────────────────────────────────────

TEST_CASE("open rejects a null sink and a double open", "[audio][engine]") {
  Engine e;
  REQUIRE_FALSE(e.open(nullptr).has_value());

  REQUIRE(e.open(std::make_unique<NullSink>()).has_value());
  REQUIRE_FALSE(e.open(std::make_unique<NullSink>()).has_value());

  e.close();
  REQUIRE_FALSE(e.is_open());
  REQUIRE(e.open(std::make_unique<NullSink>()).has_value());  // reusable
}

TEST_CASE("the engine adopts the format the sink granted", "[audio][engine]") {
  const TempWav tmp("format");
  Engine e;
  REQUIRE(e.open(std::make_unique<WavFileSink>(tmp.path()),
                 {.sample_rate = 44100, .channels = 2,
                  .frames_per_buffer = 128})
              .has_value());

  REQUIRE(e.kind() == SinkKind::Offline);
  REQUIRE(e.sink_name() == "wav");
  REQUIRE(e.sink() != nullptr);
  REQUIRE(e.sink()->format().sample_rate == 44100);
}

// ── Rendering ────────────────────────────────────────────────────────────────

TEST_CASE("render overwrites the buffer it is handed", "[audio][engine]") {
  // ⚠ Engine::render is the TOP of the render stack, where Voice and Mixer both
  // add. A device hands back whatever was last in its buffer, so if this did
  // not clear, the previous block would be heard again — and again on top of
  // that, as an echo that grows until it clips.
  const TempWav tmp("overwrite");
  Engine e;
  REQUIRE(e.open(std::make_unique<WavFileSink>(tmp.path())).has_value());

  std::vector<float> buf(256, 12345.0F);
  e.render(buf.data(), 256, 1);

  for (const float s : buf) REQUIRE(s == 0.0F);  // nothing playing == silence
}

TEST_CASE("a played sound reaches the mix on the next render",
          "[audio][engine]") {
  const TempWav tmp("reaches");
  Engine e;
  REQUIRE(e.open(std::make_unique<WavFileSink>(tmp.path())).has_value());

  REQUIRE(e.play(SfxId::Reveal));
  REQUIRE(e.stats().pushed == 1);
  REQUIRE(e.stats().popped == 0);  // nothing has drained it yet

  const auto buf = pull(e, 512);

  REQUIRE(e.stats().popped == 1);
  REQUIRE(e.stats().active_voices == 1);

  double peak = 0.0;
  for (const float s : buf) peak = std::max(peak, std::fabs(double{s}));
  REQUIRE(peak > 0.001);
}

TEST_CASE("the mono mix is fanned out to every channel", "[audio][engine]") {
  const TempWav tmp("channels");
  Engine e;
  REQUIRE(e.open(std::make_unique<WavFileSink>(tmp.path()),
                 {.sample_rate = kRate, .channels = 2,
                  .frames_per_buffer = 256})
              .has_value());
  REQUIRE(e.play(SfxId::Win));

  const auto buf = pull(e, 256, 2);
  for (std::size_t f = 0; f < 256; ++f) {
    REQUIRE(buf[f * 2] == buf[(f * 2) + 1]);
  }
}

// ── The two AGENTS.md claims ─────────────────────────────────────────────────

TEST_CASE("N simultaneous voices do not clip", "[audio][engine]") {
  // ⚠ "Summed with headroom so N simultaneous voices do not clip" (term-game#3).
  // Here the guarantee is arithmetic rather than a limiter: every spec's gain
  // is capped at kVoicePeakQ8 == 1/8 FS and there are 8 voices, so the sum
  // cannot exceed full scale. This is the case that would catch someone raising
  // a gain past the cap or adding a ninth voice.
  const TempWav tmp("clip");
  Engine e;
  REQUIRE(e.open(std::make_unique<WavFileSink>(tmp.path())).has_value());

  // ⚠ Eight copies of the SAME effect, which is the genuine worst case rather
  // than a convenient one. Noise is seeded from the spec, so eight Explodes are
  // phase-locked and sum coherently at 8x a single voice — where eight
  // different effects would partially cancel. If the headroom holds here it
  // holds for anything.
  for (int i = 0; i < kMaxVoices; ++i) REQUIRE(e.play(SfxId::Explode));

  const auto buf = pull(e, 4096);
  REQUIRE(e.stats().active_voices == kMaxVoices);

  for (const float s : buf) {
    REQUIRE(s <= 1.0F);
    REQUIRE(s >= -1.0F);
    REQUIRE(std::isfinite(s));
  }

  // And nothing lands on a 16-bit rail, which is what clipping sounds like.
  for (const float s : buf) {
    REQUIRE(to_pcm16(s) < 32767);
    REQUIRE(to_pcm16(s) > -32767);
  }
}

TEST_CASE("the mix does not depend on how the frames are chunked",
          "[audio][engine]") {
  // ⚠ "…nor drift", operationally. A device picks its own block size and may
  // change it mid-stream; the offline pump uses a different one again. If any
  // state advanced per CALL rather than per SAMPLE, the same session would
  // render differently at 64 frames than at 1024 — and every fingerprint in
  // test/18audio-synth would then describe something no device produces.
  const TempWav tmp_a("chunk-a");
  const TempWav tmp_b("chunk-b");
  constexpr int kFrames = 4096;

  Engine whole;
  REQUIRE(whole.open(std::make_unique<WavFileSink>(tmp_a.path())).has_value());
  whole.play(SfxId::Win);
  whole.play(SfxId::Explode);
  const auto one_go = pull(whole, kFrames);

  Engine chunked;
  REQUIRE(chunked.open(std::make_unique<WavFileSink>(tmp_b.path()))
              .has_value());
  chunked.play(SfxId::Win);
  chunked.play(SfxId::Explode);

  // Deliberately irregular: a different block size every call, including 1 and
  // one larger than the engine's internal scratch, so the chunking loop inside
  // render() is exercised from both directions.
  constexpr std::array<int, 6> kBlocks{13, 64, 1, 500, 2048, 777};

  std::vector<float> pieces(kFrames, 0.0F);
  int done = 0;
  std::size_t next = 0;
  while (done < kFrames) {
    const int n = std::min(kBlocks[next % kBlocks.size()], kFrames - done);
    chunked.render(pieces.data() + done, n, 1);
    done += n;
    ++next;
  }

  for (int i = 0; i < kFrames; ++i) {
    REQUIRE(one_go[static_cast<std::size_t>(i)] ==
            pieces[static_cast<std::size_t>(i)]);
  }
}

// ── Voice stealing ───────────────────────────────────────────────────────────

TEST_CASE("the ninth voice steals the oldest", "[audio][engine]") {
  const TempWav tmp("steal");
  Engine e;
  REQUIRE(e.open(std::make_unique<WavFileSink>(tmp.path())).has_value());

  // Eight long sounds, each a render apart so their ages are distinct and the
  // pool is genuinely full.
  for (int i = 0; i < kMaxVoices; ++i) {
    REQUIRE(e.play(SfxId::Lose));
    (void)pull(e, 64);
  }

  REQUIRE(e.stats().active_voices == kMaxVoices);
  REQUIRE(e.stats().stolen == 0);

  // The ninth has nowhere to go.
  REQUIRE(e.play(SfxId::Win));
  (void)pull(e, 64);

  REQUIRE(e.stats().stolen == 1);
  REQUIRE(e.stats().active_voices == kMaxVoices);  // still eight, not nine
}

TEST_CASE("stealing takes the oldest voice specifically", "[audio][engine]") {
  // ⚠ Driven through Mixer rather than Engine, because the claim is about WHICH
  // voice went — and counting alone cannot tell "stole the oldest" from "stole
  // slot 0" or "stole a random one". voice_ids() exists for exactly this.
  Mixer m;

  // Eight distinct long sounds, triggered oldest-first.
  const std::array<SfxId, kMaxVoices> filled{
      SfxId::Lose, SfxId::Win,      SfxId::Explode,   SfxId::Lose,
      SfxId::Win,  SfxId::Explode,  SfxId::Lose,      SfxId::Win};
  for (const auto id : filled) m.trigger(spec_for(id), id, kRate);

  REQUIRE(m.active_count() == kMaxVoices);
  REQUIRE(m.stolen() == 0);

  // The first one triggered is the oldest; it must be the one replaced.
  const auto before = m.voice_ids();
  const int oldest_slot = 0;
  REQUIRE(before[oldest_slot] == static_cast<int>(filled[0]));

  m.trigger(spec_for(SfxId::MenuSelect), SfxId::MenuSelect, kRate);

  REQUIRE(m.stolen() == 1);
  REQUIRE(m.active_count() == kMaxVoices);

  const auto after = m.voice_ids();
  REQUIRE(after[oldest_slot] == static_cast<int>(SfxId::MenuSelect));

  // Every other slot is untouched — nothing else was disturbed to make room.
  for (std::size_t i = 1; i < after.size(); ++i) {
    REQUIRE(after[i] == before[i]);
  }
}

TEST_CASE("a free slot is preferred over stealing", "[audio][engine]") {
  const TempWav tmp("nosteal");
  Engine e;
  REQUIRE(e.open(std::make_unique<WavFileSink>(tmp.path())).has_value());

  // Short sounds, fully rendered between plays, so a slot is always free.
  for (int i = 0; i < 40; ++i) {
    REQUIRE(e.play(SfxId::MenuMove));
    (void)pull(e, 2048);  // MenuMove is 960 frames; this finishes it
  }

  REQUIRE(e.stats().stolen == 0);
  REQUIRE(e.stats().active_voices == 0);
}

// ── pump ─────────────────────────────────────────────────────────────────────

TEST_CASE("pump renders wall-clock time into an offline sink",
          "[audio][engine]") {
  const TempWav tmp("pump");
  Engine e;
  auto sink = std::make_unique<WavFileSink>(tmp.path());
  auto* raw = sink.get();
  REQUIRE(e.open(std::move(sink)).has_value());

  e.play(SfxId::MenuSelect);

  // One second of game time at the Shell's 60 Hz tick.
  constexpr Seconds kTick{1.0 / 60.0};
  for (int i = 0; i < 60; ++i) e.pump(kTick);

  // ⚠ Within a frame of exact, not exact: 1.0/60.0 is not representable in
  // binary, so 60 ticks is not bit-for-bit one second. The carried remainder is
  // what keeps that error BOUNDED rather than accumulating — without it the
  // shortfall would compound every tick and a long session would drift audibly
  // out of step with the game clock.
  REQUIRE(raw->frames_written() >= kRate - 1);
  REQUIRE(raw->frames_written() <= kRate + 1);
}

TEST_CASE("pump is a no-op for a non-offline sink", "[audio][engine]") {
  Engine e;
  REQUIRE(e.open(std::make_unique<NullSink>()).has_value());
  constexpr Seconds kTick{1.0 / 60.0};
  for (int i = 0; i < 60; ++i) e.pump(kTick);  // must not crash or block
  REQUIRE(e.stats().pushed == 0);
}

TEST_CASE("pumping is deterministic", "[audio][engine]") {
  // The same tick sequence must produce the same file, or the Shell-driven
  // audio tests could never assert anything.
  const TempWav a("det-a");
  const TempWav b("det-b");
  constexpr Seconds kTick{1.0 / 60.0};

  std::vector<std::int16_t> first;
  std::vector<std::int16_t> second;

  for (int run = 0; run < 2; ++run) {
    Engine e;
    auto sink = std::make_unique<WavFileSink>(run == 0 ? a.path() : b.path());
    auto* raw = sink.get();
    REQUIRE(e.open(std::move(sink)).has_value());

    e.play(SfxId::Explode);
    for (int i = 0; i < 30; ++i) e.pump(kTick);

    auto& into = run == 0 ? first : second;
    into.assign(raw->samples().begin(), raw->samples().end());
  }

  REQUIRE(first.size() == second.size());
  REQUIRE(first == second);
}

// ── Counters ─────────────────────────────────────────────────────────────────

TEST_CASE("play_count counts intent per effect", "[audio][engine]") {
  Engine e;
  REQUIRE(e.open(std::make_unique<NullSink>()).has_value());

  e.play(SfxId::Flag);
  e.play(SfxId::Flag);
  e.play(SfxId::Win);

  REQUIRE(e.play_count(SfxId::Flag) == 2);
  REQUIRE(e.play_count(SfxId::Win) == 1);
  REQUIRE(e.play_count(SfxId::Lose) == 0);
  REQUIRE(e.last_played() == static_cast<int>(SfxId::Win));

  e.reset_counts();
  REQUIRE(e.play_count(SfxId::Flag) == 0);
  REQUIRE(e.last_played() == -1);
}

TEST_CASE("an empty Player is silent rather than a crash", "[audio][engine]") {
  // ⚠ The whole point of the Player handle. A default-constructed GameContext
  // hands one of these out, so a game calls ctx.audio().play(...) with no null
  // check anywhere — and "this build makes no sound" stays a property of the
  // engine rather than a shape every call site carries.
  const Player empty;
  REQUIRE(empty.empty());
  empty.play(SfxId::Explode);  // must be a no-op, not a null dereference

  Engine e;
  const Player p{&e};
  REQUIRE_FALSE(p.empty());
  p.play(SfxId::Explode);
  REQUIRE(e.play_count(SfxId::Explode) == 1);
}
