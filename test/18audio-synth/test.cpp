// The synth: the SFX bank, the oscillators, and the envelope.
//
// ⚠ READ THIS BEFORE ADDING A "GOLDEN FILE". You will not find one here, and
// its absence is a decision rather than an omission.
//
// AGENTS.md asks for golden files per SFX and, two sections earlier, forbids
// binary blobs in git. Both cannot hold. The resolution is that the interesting
// claim was never "these exact bytes" anyway — it is "this effect is the right
// length, the right loudness and the right pitch, and it is reproducible". So:
//
//   1. A COMPACT NUMERIC FINGERPRINT per effect, committed below as source
//      text. Frames exact; peak, RMS and zero-crossings within tight
//      tolerances. These are physically meaningful and they bite: at 660 Hz for
//      24 ms a semitone of pitch error moves the zero-crossing count by ~6 %,
//      an order of magnitude outside the band.
//   2. BIT-EXACT SELF-DETERMINISM within a build — two renders of the same spec
//      must be byte-identical.
//
// What is deliberately NOT asserted is a cross-toolchain byte digest. Measured
// during development, the output IS byte-identical across GCC -O0, GCC -O2, GCC
// -O3 -ffp-contract=fast -march=native and Clang -O2 — which is the payoff for
// synth.cpp using integer phase and linear envelopes instead of sin and exp.
// But "we measured it identical on four compilers on one machine" is not the
// same claim as "it is specified", and board.hpp was already careful about
// exactly that distinction when it rejected std::uniform_int_distribution. A
// committed digest would convert a future glibc or codegen change into a
// mystery failure that looks like a synth bug.
//
// ⚠ The fingerprints record what the bank IS, not what it SHOULD sound like.
// Nothing in this container has ever been heard. When the maintainer retunes a
// spec by ear, the matching row here is expected to move with it — that is the
// table doing its job, not a regression.
//
// ⚠ No termforge header, no Screen. Same discipline as test/14minesweeper.
//
// Mutation-tested, and the first attempt was not good enough:
//
//   * raising MenuMove's gain from 14 to 16 (+14 %, still inside the headroom)
//     turns the peak and RMS assertions red.
//   * retuning Click from 660 Hz to 623 Hz — one semitone — initially PASSED,
//     because the zero-crossing band was max(2, 1 %) and that is ±6.5 % on a
//     24 ms effect. Tightened to max(1, 0.2 %), it now goes red. A square
//     wave's peak and RMS do not move with frequency, so that band is the only
//     thing standing between an audible pitch regression and a green suite.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <vector>

#include <termgame/audio/synth.hpp>

namespace {

using namespace termgame::audio;

constexpr int kRate = 48000;

// Ignore fuzz around zero when counting crossings, so numerical noise in the
// envelope tail cannot manufacture a crossing that is not audibly there.
constexpr float kZeroBand = 1.0F / 1000.0F;

struct Fingerprint {
  std::string_view name;
  int frames;
  float peak;
  float rms;
  int zero_crossings;
};

// Generated from the implementation and committed as text. Regenerate by
// rendering each spec at kRate and recomputing the four quantities below.
constexpr std::array<Fingerprint, 8> kFingerprints{{
    {.name = "Click", .frames = 1152, .peak = 0.078125F, .rms = 0.038033F,
     .zero_crossings = 31},
    {.name = "Reveal", .frames = 1536, .peak = 0.067755F, .rms = 0.017765F,
     .zero_crossings = 60},
    {.name = "Flag", .frames = 1440, .peak = 0.078125F, .rms = 0.035495F,
     .zero_crossings = 71},
    {.name = "Explode", .frames = 12480, .peak = 0.115140F, .rms = 0.026682F,
     .zero_crossings = 5753},
    {.name = "Win", .frames = 15360, .peak = 0.100096F, .rms = 0.034456F,
     .zero_crossings = 496},
    {.name = "Lose", .frames = 19200, .peak = 0.092298F, .rms = 0.028732F,
     .zero_crossings = 194},
    {.name = "MenuMove", .frames = 960, .peak = 0.054688F, .rms = 0.023517F,
     .zero_crossings = 8},
    {.name = "MenuSelect", .frames = 4320, .peak = 0.093750F, .rms = 0.049243F,
     .zero_crossings = 117},
}};

static_assert(kFingerprints.size() == kSfxIds.size(),
              "every SfxId needs a fingerprint row");

struct Measured {
  int frames{0};
  double peak{0.0};
  double rms{0.0};
  int zero_crossings{0};
};

// Render one voice into a buffer with slack past the end, so the "adds exactly
// zero after it finishes" claim is measurable in the same pass.
[[nodiscard]] auto render_voice(SfxId id, int extra = 0)
    -> std::vector<float> {
  const auto& spec = spec_for(id);
  const int n = frames_for(spec, kRate);
  std::vector<float> buf(static_cast<std::size_t>(n + extra), 0.0F);

  Voice v;
  v.trigger(spec, kRate, id, 1);
  v.render_add(buf.data(), n + extra);
  return buf;
}

[[nodiscard]] auto measure(const std::vector<float>& buf, int frames)
    -> Measured {
  Measured m;
  m.frames = frames;
  double sumsq = 0.0;
  int sign = 0;

  for (int i = 0; i < frames; ++i) {
    const float s = buf[static_cast<std::size_t>(i)];
    m.peak = std::max(m.peak, static_cast<double>(std::fabs(s)));
    sumsq += static_cast<double>(s) * static_cast<double>(s);

    const int cur = s > kZeroBand ? 1 : (s < -kZeroBand ? -1 : 0);
    if (cur != 0) {
      if (sign != 0 && cur != sign) ++m.zero_crossings;
      sign = cur;
    }
  }

  m.rms = std::sqrt(sumsq / static_cast<double>(frames));
  return m;
}

[[nodiscard]] auto within_relative(double got, double want, double tol)
    -> bool {
  return std::fabs(got - want) <= std::fabs(want) * tol;
}

}  // namespace

TEST_CASE("the bank covers every SfxId exactly once", "[audio][synth]") {
  // kSfxIds' own consteval check enforces this at compile time; echoing it as a
  // runtime case is what makes it visible to someone reading the tests. The
  // first draft of that array listed Explode twice and omitted MenuSelect,
  // which is precisely the failure this pins.
  REQUIRE(sfx_bank().size() == kSfxIds.size());

  for (std::size_t i = 0; i < kSfxIds.size(); ++i) {
    REQUIRE(static_cast<std::size_t>(kSfxIds[i]) == i);
  }
}

TEST_CASE("every spec stays inside the voice headroom", "[audio][synth]") {
  // ⚠ The mixer's no-clip guarantee is arithmetic, not a limiter: kMaxVoices
  // voices each peaking at kVoicePeakQ8 sum to exactly full scale. That holds
  // only while every spec respects the cap, so this is the case that keeps the
  // guarantee true when someone makes an effect louder.
  for (const auto& spec : sfx_bank()) {
    REQUIRE(spec.gain_q8 > 0);
    REQUIRE(spec.gain_q8 <= kVoicePeakQ8);
    REQUIRE(spec.env.attack_ms >= 1);
    REQUIRE(spec.env.attack_ms + spec.env.decay_ms + spec.env.release_ms <=
            spec.duration_ms);
  }
  REQUIRE(kMaxVoices * kVoicePeakQ8 <= 256);
}

TEST_CASE("each SFX matches its committed fingerprint", "[audio][synth]") {
  for (std::size_t i = 0; i < kSfxIds.size(); ++i) {
    const auto id = kSfxIds[i];
    const auto& want = kFingerprints[i];
    INFO("SFX: " << want.name);

    const auto& spec = spec_for(id);
    const int frames = frames_for(spec, kRate);

    // Exact. Duration is integer arithmetic from the spec, so any tolerance
    // here would hide a real bug rather than absorb numerical noise.
    REQUIRE(frames == want.frames);

    const auto buf = render_voice(id);
    const auto got = measure(buf, frames);

    // Not silence, and inside the headroom. These two bracket every possible
    // gain regression from either direction.
    REQUIRE(got.peak > 0.005);
    REQUIRE(got.peak <= static_cast<double>(kVoicePeakQ8) / 256.0);

    REQUIRE(within_relative(got.peak, want.peak, 0.005));
    REQUIRE(within_relative(got.rms, want.rms, 0.01));

    // Pitch, effectively — and for a square wave it is the ONLY quantity here
    // that can detect a pitch change at all, since peak and RMS are unmoved by
    // frequency. So the band has to be tight.
    //
    // ⚠ It started at max(2, 1 %) and that was too loose, which mutation
    // testing caught: retuning Click from 660 Hz to 623 Hz — one semitone, an
    // obviously audible change — moves the count from 31 to 29 and slipped
    // through. On a short effect ±2 crossings is ±6.5 %.
    //
    // ±1 is safe rather than brittle because the measurement is deterministic:
    // the render was verified byte-identical across GCC -O0/-O2/-O3 and Clang
    // -O2, so the only thing that moves this number is a real change. The 1/500
    // term exists solely for Explode, where the count is thousands of noise
    // crossings rather than a pitch.
    const int slack = std::max(1, want.zero_crossings / 500);
    REQUIRE(std::abs(got.zero_crossings - want.zero_crossings) <= slack);
  }
}

TEST_CASE("two renders of the same spec are bit-identical", "[audio][synth]") {
  // ⚠ THE PORTABLE HALF of the golden-file replacement. It is 100 % reliable —
  // same code, same machine, same build — and it catches the bugs a digest
  // would: uninitialised voice state, an unreset noise seed, state leaking
  // between renders, NaN contamination.
  for (const auto id : kSfxIds) {
    INFO("SfxId: " << static_cast<int>(id));
    const auto a = render_voice(id);
    const auto b = render_voice(id);

    REQUIRE(a.size() == b.size());
    REQUIRE(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
  }
}

TEST_CASE("a finished voice adds exactly zero", "[audio][synth]") {
  // Exactly zero, not nearly zero. The envelope is linear precisely so the
  // release lands on 0.0f instead of decaying asymptotically into denormals —
  // which are slow on the audio thread and would make this untestable.
  constexpr int kSlack = 512;

  for (const auto id : kSfxIds) {
    INFO("SfxId: " << static_cast<int>(id));
    const auto& spec = spec_for(id);
    const int frames = frames_for(spec, kRate);
    const auto buf = render_voice(id, kSlack);

    for (int i = frames; i < frames + kSlack; ++i) {
      REQUIRE(buf[static_cast<std::size_t>(i)] == 0.0F);
    }
  }
}

TEST_CASE("a voice reports itself finished after its duration",
          "[audio][synth]") {
  for (const auto id : kSfxIds) {
    INFO("SfxId: " << static_cast<int>(id));
    const auto& spec = spec_for(id);
    const int frames = frames_for(spec, kRate);

    Voice v;
    REQUIRE_FALSE(v.active());
    REQUIRE(v.id() == -1);

    v.trigger(spec, kRate, id, 7);
    REQUIRE(v.active());
    REQUIRE(v.id() == static_cast<int>(id));
    REQUIRE(v.age() == 7);

    std::vector<float> buf(static_cast<std::size_t>(frames) - 1, 0.0F);
    v.render_add(buf.data(), frames - 1);
    REQUIRE(v.active());  // one frame short: still sounding

    std::vector<float> last(4, 0.0F);
    v.render_add(last.data(), 4);
    REQUIRE_FALSE(v.active());
    REQUIRE(v.id() == -1);
  }
}

TEST_CASE("every effect starts at exactly zero", "[audio][synth]") {
  // An audible property, not a numerical one: a waveform that starts at full
  // amplitude is a step discontinuity, and a step is a click. The bank enforces
  // attack_ms >= 1 so the envelope always ramps in from silence.
  for (const auto id : kSfxIds) {
    INFO("SfxId: " << static_cast<int>(id));
    const auto buf = render_voice(id);
    REQUIRE(buf[0] == 0.0F);
  }
}

TEST_CASE("render_add sums into the buffer rather than overwriting it",
          "[audio][synth]") {
  // ⚠ The property the whole mixer rests on. If a voice ever overwrote, N
  // simultaneous sounds would render as whichever one happened to go last —
  // which sounds like "the mixer works" until two things overlap.
  const auto id = SfxId::Click;
  const auto& spec = spec_for(id);
  const int frames = frames_for(spec, kRate);

  const auto solo = render_voice(id);

  std::vector<float> both(static_cast<std::size_t>(frames), 0.0F);
  Voice a;
  Voice b;
  a.trigger(spec, kRate, id, 1);
  b.trigger(spec, kRate, id, 2);
  a.render_add(both.data(), frames);
  b.render_add(both.data(), frames);

  for (int i = 0; i < frames; ++i) {
    const auto k = static_cast<std::size_t>(i);
    REQUIRE(both[k] == solo[k] + solo[k]);
  }
}

TEST_CASE("output does not depend on how the frames are chunked",
          "[audio][synth]") {
  // ⚠ "Voices do not drift", operationally. A device hands us whatever block
  // size it likes and may change it; the offline pump uses a different one
  // again. If phase advanced per CALL rather than per SAMPLE, every effect
  // would render differently depending on the buffer size — and the offline
  // fingerprints above would then be asserting something no device ever
  // produces.
  for (const auto id : kSfxIds) {
    INFO("SfxId: " << static_cast<int>(id));
    const auto& spec = spec_for(id);
    const int frames = frames_for(spec, kRate);

    const auto whole = render_voice(id);

    for (const int block : {1, 7, 64, 256, 1000}) {
      std::vector<float> chunked(static_cast<std::size_t>(frames), 0.0F);
      Voice v;
      v.trigger(spec, kRate, id, 1);

      int done = 0;
      while (done < frames) {
        const int n = std::min(block, frames - done);
        v.render_add(chunked.data() + done, n);
        done += n;
      }

      INFO("block size: " << block);
      REQUIRE(std::memcmp(whole.data(), chunked.data(),
                          static_cast<std::size_t>(frames) * sizeof(float)) ==
              0);
    }
  }
}

TEST_CASE("the noise oscillator is deterministic", "[audio][synth]") {
  // Same seed, same noise — the claim board.hpp makes about mine layouts, for
  // the same reason and with the same hand-rolled splitmix64 behind it. Without
  // it the Explode fingerprint could not exist at all.
  const auto a = render_voice(SfxId::Explode);
  const auto b = render_voice(SfxId::Explode);
  REQUIRE(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);

  // And it is actually noisy: a tone would cross zero a few hundred times over
  // 260 ms, not thousands.
  const auto m = measure(a, frames_for(spec_for(SfxId::Explode), kRate));
  REQUIRE(m.zero_crossings > 2000);
}

TEST_CASE("spec_for is defensive about an out-of-range id", "[audio][synth]") {
  // Defensive rather than UB on a caller bug — the policy Screen::at() applies,
  // for the same reason: a bad index must not corrupt memory.
  const auto& bad = spec_for(static_cast<SfxId>(200));
  REQUIRE(bad.duration_ms == spec_for(SfxId::Click).duration_ms);
}
