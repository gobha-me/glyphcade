#pragma once

// term-game — the synth: oscillators, a linear envelope, and the SFX bank.
//
// SFX are synthesized, not sampled (DESIGN.md). That removes the WAV decoder,
// the asset pipeline and the binary blobs from git in one go, and it is the
// correct sound for an arcade — venice is better spent on sprite and title art.
// Each effect is a small declarative struct; there is no audio content in this
// repo, only a description of how to make some.
//
// ⚠ NO TRANSCENDENTALS ANYWHERE. No sin, no exp, no pow — the phase is an
// integer accumulator, the frequency sweep is an integer lerp, and the envelope
// is linear over integer sample counts. That is a portability decision, not a
// performance one, and it is the same one board.hpp already made when it
// hand-rolled splitmix64 rather than trust std::uniform_int_distribution:
//
//   * glibc's sin and exp are not correctly-rounded and change between
//     versions, so a golden value recorded today can drift under a libc bump
//     with no code change at all.
//   * -ffp-contract defaults differ (GCC `fast`, Clang `on`), and -O0 vs -O2
//     changes whether an intermediate gets contracted into an FMA.
//
// Written this way, the only floating point left is int-to-float conversion,
// division and multiplication — all exactly specified by IEEE 754. In practice
// the output is byte-identical across GCC, Clang, -O0 and -O2, and
// test/18audio-synth asserts that WITHIN a build. It deliberately does not
// commit a cross-toolchain digest, because "we measured it identical" is not
// the same claim as "it is specified", and board.hpp was already careful about
// that distinction.
//
// ⚠ NO TERMFORGE HEADER, and no #ifdef TERMGAME_WITH_AUDIO. Same as ring.hpp
// and sink.hpp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace termgame::audio {

enum class Wave : std::uint8_t { Square, Triangle, Saw, Noise };

// A linear ADSR, in milliseconds and a q8 sustain level (0..256 == 0.0..1.0).
//
// Linear rather than exponential for two reasons that agree: it needs no exp,
// and it reaches exactly 0.0f at the end of the release instead of decaying
// asymptotically into denormals — which are slow on the audio thread and would
// make "a finished voice adds exactly zero" untestable.
struct Adsr {
  int attack_ms{2};
  int decay_ms{20};
  int sustain_q8{160};
  int release_ms{40};
};

// One effect, entirely declaratively.
struct SfxSpec {
  Wave wave{Wave::Square};
  int freq_start_hz{440};
  int freq_end_hz{440};  // linear sweep to here across the duration
  int duration_ms{80};
  Adsr env{};
  int gain_q8{16};    // peak amplitude, q8. Must be <= kVoicePeakQ8.
  int duty_q8{128};   // Square only; 128 == a symmetric square wave
};

enum class SfxId : std::uint8_t {
  Click,
  Reveal,
  Flag,
  Explode,
  Win,
  Lose,
  MenuMove,
  MenuSelect,
};

// Every id, in order, so a test can loop the whole bank and a new effect cannot
// be added without being covered. Same shape as kLevels[] in board.hpp.
//
// ⚠ The static_assert below is not ceremony. The first draft of this array had
// Explode twice and no MenuSelect — which reads as fine, keeps the right
// length, and would have quietly excluded one whole effect from every test that
// loops the bank. A hand-written list parallel to an enum wants a machine to
// check it.
inline constexpr std::array<SfxId, 8> kSfxIds{
    SfxId::Click, SfxId::Reveal,   SfxId::Flag,     SfxId::Explode,
    SfxId::Win,   SfxId::Lose,     SfxId::MenuMove, SfxId::MenuSelect,
};

// Every id appears exactly once: each entry equals its own index, which is only
// possible if the array is a complete, in-order enumeration.
consteval auto sfx_ids_are_complete() noexcept -> bool {
  for (std::size_t i = 0; i < kSfxIds.size(); ++i) {
    if (static_cast<std::size_t>(kSfxIds[i]) != i) return false;
  }
  return true;
}
static_assert(sfx_ids_are_complete(),
              "kSfxIds must list every SfxId exactly once, in enum order");

inline constexpr int kMaxVoices = 8;

// ⚠ HEADROOM BY CONSTRUCTION, not by a limiter.
//
// Every voice peaks at or below 1/8 of full scale, and there are 8 voices, so
// the sum cannot exceed 1.0 even if all of them fire on the same sample. That
// is provable — see the static_assert below — where a runtime limiter would be
// a thing you have to HEAR to trust, and nobody in this container can.
//
// The cost is honest and worth stating: a single sound peaks at -18 dBFS, which
// is quiet. Raising it needs a limiter, and a limiter needs a human ear. Ship
// provably-quiet, and let the maintainer decide after listening.
inline constexpr int kVoicePeakQ8 = 256 / kMaxVoices;
static_assert(kMaxVoices * kVoicePeakQ8 <= 256,
              "the mixer must not be able to clip by construction");

// The bank. Declarative, constexpr, and the only place a sound is described.
[[nodiscard]] auto sfx_bank() noexcept -> std::span<const SfxSpec>;
[[nodiscard]] auto spec_for(SfxId id) noexcept -> const SfxSpec&;

// The number of sample frames a spec occupies at a given rate. Integer
// throughout, which is why test/18audio-synth can assert it exactly.
[[nodiscard]] auto frames_for(const SfxSpec& spec, int sample_rate) noexcept
    -> int;

// One sounding note.
//
// ⚠ Everything below trigger() runs on the AUDIO THREAD. No allocation, no
// locks, no I/O — a Voice holds only scalars, by design.
class Voice {
 public:
  // Resolve a spec into per-sample increments. `age` is the mixer's monotonic
  // counter and is what makes oldest-stolen well defined.
  auto trigger(const SfxSpec& spec, int sample_rate, SfxId id,
               std::uint64_t age) noexcept -> void;

  // ⚠ ADDS into `out`, never overwrites — that is what lets voices mix by
  // summation, and it is why every caller must clear the buffer first.
  // Mono; channel fan-out is the mixer's job.
  auto render_add(float* out, int frames) noexcept -> void;

  [[nodiscard]] auto active() const noexcept -> bool { return m_pos < m_total; }
  [[nodiscard]] auto age() const noexcept -> std::uint64_t { return m_age; }
  // The SfxId currently sounding, or -1 when idle. Exists for the
  // voice-stealing test, which has to name which voice survived.
  [[nodiscard]] auto id() const noexcept -> int { return m_id; }

  auto reset() noexcept -> void;

 private:
  [[nodiscard]] auto envelope_at(int pos) const noexcept -> float;
  [[nodiscard]] auto oscillator() noexcept -> float;

  Wave m_wave{Wave::Square};
  std::uint32_t m_phase{0};
  std::uint32_t m_inc_start{0};
  std::uint32_t m_inc_end{0};
  std::uint32_t m_duty{0x80000000U};

  int m_pos{0};
  int m_total{0};
  int m_attack{0};
  int m_decay{0};
  int m_release{0};
  float m_sustain{0.0F};
  float m_gain{0.0F};

  std::uint64_t m_noise{0};
  std::uint64_t m_age{0};
  int m_id{-1};
};

}  // namespace termgame::audio
