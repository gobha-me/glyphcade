#include <termgame/audio/synth.hpp>

#include <algorithm>
#include <cmath>

namespace termgame::audio {

namespace {

// The bank, indexed by SfxId. Order must match the enum; the static_assert
// below pins that, and kSfxIds' own assert pins the enumeration.
//
// Every gain is <= kVoicePeakQ8, which is what makes the mixer unable to clip.
// Frequencies are chosen so the set is distinguishable by ear at the bottom of
// a laptop speaker: menu sounds sit low and short, feedback sounds mid, the two
// outcomes are long and unmistakable.
//
// ⚠ These values have NEVER BEEN HEARD. This container has no /dev/snd. They
// are a starting point that is provably in range and provably distinct, not a
// tuned bank — expect the maintainer to move them after listening, and treat
// the fingerprints in test/18audio-synth as recording what IS, not what should
// be.
constexpr std::array<SfxSpec, 8> kBank{{
    // Click — the generic acknowledgement. Short, dry, unobtrusive.
    {.wave = Wave::Square,
     .freq_start_hz = 660,
     .freq_end_hz = 660,
     .duration_ms = 24,
     .env = {.attack_ms = 1, .decay_ms = 6, .sustain_q8 = 120,
             .release_ms = 12},
     .gain_q8 = 20,
     .duty_q8 = 128},

    // Reveal — the most frequent sound in Minesweeper, so the quietest and
    // shortest thing that still registers. A tick, not a note.
    {.wave = Wave::Triangle,
     .freq_start_hz = 880,
     .freq_end_hz = 1046,
     .duration_ms = 32,
     .env = {.attack_ms = 1, .decay_ms = 8, .sustain_q8 = 100,
             .release_ms = 18},
     .gain_q8 = 18,
     .duty_q8 = 128},

    // Flag — deliberately thinner than Reveal (narrow duty), so placing a flag
    // and opening a cell are told apart by timbre and not only by pitch.
    {.wave = Wave::Square,
     .freq_start_hz = 1200,
     .freq_end_hz = 1200,
     .duration_ms = 30,
     .env = {.attack_ms = 1, .decay_ms = 7, .sustain_q8 = 110,
             .release_ms = 16},
     .gain_q8 = 20,
     .duty_q8 = 40},

    // Explode — noise, long, falling out of the way of the Lose tone that
    // follows it a moment later.
    {.wave = Wave::Noise,
     .freq_start_hz = 400,
     .freq_end_hz = 60,
     .duration_ms = 260,
     .env = {.attack_ms = 1, .decay_ms = 60, .sustain_q8 = 90,
             .release_ms = 180},
     .gain_q8 = 30,
     .duty_q8 = 128},

    // Win — a rising octave. The only sound in the bank that is allowed to be
    // long and pretty.
    {.wave = Wave::Triangle,
     .freq_start_hz = 523,
     .freq_end_hz = 1046,
     .duration_ms = 320,
     .env = {.attack_ms = 4, .decay_ms = 40, .sustain_q8 = 190,
             .release_ms = 200},
     .gain_q8 = 26,
     .duty_q8 = 128},

    // Lose — a falling saw, two octaves down. The mirror of Win, on purpose.
    {.wave = Wave::Saw,
     .freq_start_hz = 392,
     .freq_end_hz = 98,
     .duration_ms = 400,
     .env = {.attack_ms = 3, .decay_ms = 50, .sustain_q8 = 170,
             .release_ms = 260},
     .gain_q8 = 24,
     .duty_q8 = 128},

    // MenuMove — the quietest thing in the bank. It fires on every arrow key,
    // so anything more than a blip becomes irritating within a minute.
    {.wave = Wave::Square,
     .freq_start_hz = 220,
     .freq_end_hz = 220,
     .duration_ms = 20,
     .env = {.attack_ms = 1, .decay_ms = 5, .sustain_q8 = 90,
             .release_ms = 10},
     .gain_q8 = 14,
     .duty_q8 = 128},

    // MenuSelect — a rising fifth, so entering a game feels like a commitment
    // rather than another move.
    {.wave = Wave::Square,
     .freq_start_hz = 440,
     .freq_end_hz = 880,
     .duration_ms = 90,
     .env = {.attack_ms = 2, .decay_ms = 18, .sustain_q8 = 150,
             .release_ms = 50},
     .gain_q8 = 24,
     .duty_q8 = 128},
}};

static_assert(kBank.size() == kSfxIds.size(),
              "the bank must have an entry for every SfxId");

// Bank discipline, checked at compile time so a bad spec cannot be committed.
//
// The envelope check is the one that matters: attack + decay + release must fit
// inside the duration, or the sustain segment has negative length and the
// envelope arithmetic runs off the end of its own timeline.
consteval auto bank_is_sane() noexcept -> bool {
  for (const auto& s : kBank) {
    if (s.gain_q8 <= 0 || s.gain_q8 > kVoicePeakQ8) return false;
    if (s.duration_ms < 10 || s.duration_ms > 1500) return false;
    if (s.env.attack_ms < 1) return false;  // a 0 ms attack is a click
    if (s.env.decay_ms < 0 || s.env.release_ms < 0) return false;
    if (s.env.attack_ms + s.env.decay_ms + s.env.release_ms > s.duration_ms) {
      return false;
    }
    if (s.env.sustain_q8 < 0 || s.env.sustain_q8 > 256) return false;
    if (s.freq_start_hz < 40 || s.freq_start_hz > 8000) return false;
    if (s.freq_end_hz < 40 || s.freq_end_hz > 8000) return false;
    if (s.duty_q8 < 8 || s.duty_q8 > 248) return false;
  }
  return true;
}
static_assert(bank_is_sane(), "an SFX spec is out of range — see the checks");

[[nodiscard]] constexpr auto ms_to_frames(int ms, int rate) noexcept -> int {
  return static_cast<int>((static_cast<std::int64_t>(ms) * rate) / 1000);
}

// Phase increment for a frequency, as a fixed-point fraction of the full
// 32-bit phase range. Integer throughout: the same frequency and rate give the
// same increment on every machine, which is the foundation everything else in
// this file rests on.
[[nodiscard]] constexpr auto phase_inc(int hz, int rate) noexcept
    -> std::uint32_t {
  if (rate <= 0 || hz <= 0) return 0;
  const auto num = static_cast<std::uint64_t>(hz) << 32U;
  return static_cast<std::uint32_t>(num / static_cast<std::uint64_t>(rate));
}

// splitmix64, for the noise oscillator.
//
// ⚠ Hand-rolled rather than <random>, for the reason board.hpp spells out at
// length: std::uniform_int_distribution's mapping from engine output to range
// is implementation-defined, so "same seed, same noise" would be a coin flip
// between libstdc++ and libc++ — and this repo builds under both.
[[nodiscard]] constexpr auto splitmix64(std::uint64_t& state) noexcept
    -> std::uint64_t {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = state;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

}  // namespace

auto sfx_bank() noexcept -> std::span<const SfxSpec> { return kBank; }

auto spec_for(SfxId id) noexcept -> const SfxSpec& {
  const auto i = static_cast<std::size_t>(id);
  // Defensive rather than UB on a caller bug — the same policy Screen::at()
  // applies, for the same reason.
  return kBank[i < kBank.size() ? i : 0];
}

auto frames_for(const SfxSpec& spec, int sample_rate) noexcept -> int {
  return ms_to_frames(spec.duration_ms, sample_rate);
}

// ── Voice ───────────────────────────────────────────────────────────────────

auto Voice::reset() noexcept -> void {
  m_pos = 0;
  m_total = 0;
  m_id = -1;
  m_phase = 0;
}

auto Voice::trigger(const SfxSpec& spec, int sample_rate, SfxId id,
                    std::uint64_t age) noexcept -> void {
  m_wave = spec.wave;
  m_phase = 0;
  m_inc_start = phase_inc(spec.freq_start_hz, sample_rate);
  m_inc_end = phase_inc(spec.freq_end_hz, sample_rate);
  m_duty = static_cast<std::uint32_t>(spec.duty_q8) << 24U;

  m_total = ms_to_frames(spec.duration_ms, sample_rate);
  m_attack = ms_to_frames(spec.env.attack_ms, sample_rate);
  m_decay = ms_to_frames(spec.env.decay_ms, sample_rate);
  m_release = ms_to_frames(spec.env.release_ms, sample_rate);
  m_sustain = static_cast<float>(spec.env.sustain_q8) / 256.0F;
  m_gain = static_cast<float>(spec.gain_q8) / 256.0F;

  // Seeded from the spec rather than from a clock or a global counter, so the
  // same effect renders the same noise every time. Determinism beats variety
  // here: an explosion that differs run to run cannot be fingerprinted, and
  // nobody can hear the difference between two white-noise bursts anyway.
  m_noise = 0x243F6A8885A308D3ULL ^
            (static_cast<std::uint64_t>(spec.duration_ms) << 32U) ^
            static_cast<std::uint64_t>(spec.freq_start_hz);

  m_pos = 0;
  m_age = age;
  m_id = static_cast<int>(id);
}

auto Voice::envelope_at(int pos) const noexcept -> float {
  // Linear segments over integer sample counts. Every divisor is checked,
  // because a zero-length segment is legal in a spec and dividing by it here
  // would be the kind of NaN that spreads silently through the whole mix.
  if (pos < m_attack) {
    return m_attack > 0 ? static_cast<float>(pos) / static_cast<float>(m_attack)
                        : 1.0F;
  }

  const int after_attack = pos - m_attack;
  if (after_attack < m_decay) {
    const float t =
        m_decay > 0 ? static_cast<float>(after_attack) /
                          static_cast<float>(m_decay)
                    : 1.0F;
    return 1.0F - ((1.0F - m_sustain) * t);
  }

  const int release_start = m_total - m_release;
  if (pos < release_start) return m_sustain;

  const int into_release = pos - release_start;
  const float t = m_release > 0 ? static_cast<float>(into_release) /
                                      static_cast<float>(m_release)
                                : 1.0F;
  // Reaches exactly 0.0f at pos == m_total, which is what makes "a finished
  // voice adds exactly zero" true rather than approximately true.
  return m_sustain * (1.0F - t);
}

auto Voice::oscillator() noexcept -> float {
  switch (m_wave) {
    case Wave::Square:
      return m_phase < m_duty ? 1.0F : -1.0F;

    case Wave::Saw:
      // uint32 phase mapped onto [-1, 1). Multiplication and subtraction only.
      return (static_cast<float>(m_phase) * (2.0F / 4294967296.0F)) - 1.0F;

    case Wave::Triangle: {
      const float saw =
          (static_cast<float>(m_phase) * (2.0F / 4294967296.0F)) - 1.0F;
      // std::fabs is a sign-bit clear, not a libm transcendental — exact, and
      // constant-folded to a single instruction everywhere this builds.
      return (2.0F * std::fabs(saw)) - 1.0F;
    }

    case Wave::Noise: {
      const std::uint64_t r = splitmix64(m_noise);
      // Top 32 bits, mapped the same way as the saw.
      const auto top = static_cast<std::uint32_t>(r >> 32U);
      return (static_cast<float>(top) * (2.0F / 4294967296.0F)) - 1.0F;
    }
  }
  return 0.0F;
}

auto Voice::render_add(float* out, int frames) noexcept -> void {
  if (m_id < 0) return;

  for (int i = 0; i < frames; ++i) {
    if (m_pos >= m_total) {
      m_id = -1;  // finished mid-buffer; the rest of this block is untouched
      return;
    }

    // The frequency sweep, as an integer lerp across the voice's own timeline.
    // int64 so that the multiply cannot overflow at high frequencies.
    const auto span = static_cast<std::int64_t>(m_inc_end) -
                      static_cast<std::int64_t>(m_inc_start);
    const auto inc = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(m_inc_start) +
        ((span * m_pos) / (m_total > 0 ? m_total : 1)));

    out[i] += oscillator() * envelope_at(m_pos) * m_gain;

    m_phase += inc;  // wraps naturally; that IS the oscillator period
    ++m_pos;
  }

  if (m_pos >= m_total) m_id = -1;
}

}  // namespace termgame::audio
