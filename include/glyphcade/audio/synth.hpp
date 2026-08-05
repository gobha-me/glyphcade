#pragma once

// glyphcade — the synth: oscillators, a linear envelope, and the SFX bank.
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
// ⚠ NO TERMFORGE HEADER, and no #ifdef GLYPHCADE_WITH_AUDIO. Same as ring.hpp
// and sink.hpp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace glyphcade::audio {

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
  // Added for 2048. A move that merges nothing gets Slide; a move that merges
  // anything gets Merge instead, so one gesture is still one sound.
  //
  // ⚠ There is deliberately no Spawn effect, which diverges from term-game#5's
  // "SFX: slide, merge, spawn, game-over". A spawn happens on EVERY legal move,
  // so a spawn sound is a second blip on every single gesture — that is not a
  // sound, it is a stutter. Nothing is lost: the move already sounded.
  //
  // ⚠ And there is one Merge, not one per tile value. Pitching a merge by the
  // resulting tile would need 2^(cents/1200), i.e. exp, which is the exact
  // portability trap this synth exists to avoid (see the header note above).
  Slide,
  Merge,
  // Added for Snake (term-game#6), which asks for "eat, turn, die" — and gets ONE
  // new id, not three. Turn is Click (a turn is a generic acknowledged gesture,
  // which is exactly what Click is for) and dying is Lose, so only eating had
  // nothing in the bank that already meant it.
  //
  // ⚠ There is deliberately no Step effect. Snake advances several times a
  // second with no input at all, so a per-step sound is not feedback, it is a
  // metronome — the same argument that kept Spawn out for 2048, but stronger,
  // because a step does not even follow a keystroke.
  Eat,
  // Added for Tetris (term-game#7), which asks for "move, rotate, lock, line
  // clear, tetris, level-up, top-out" — and gets THREE new ids, not seven.
  //
  // Reused instead: a move and a rotate are Click (a generic acknowledged
  // gesture, which is what Click is for), a hard drop is Slide (a decisive
  // movement, which is what Slide already means in 2048), a one-to-three line
  // clear is Merge (things combined and vanished — 2048's exact meaning), and
  // topping out is Lose. Only three events had nothing in the bank that already
  // meant them.
  //
  // ⚠ There is deliberately no sound for gravity or for auto-shift. A piece
  // falls several times a second with no input at all, and DAS fires every
  // 50 ms while a key is held — either would be a metronome rather than
  // feedback. Same argument that kept Spawn out of 2048 and Step out of Snake,
  // and it now applies twice in one game.
  //
  // ⚠ Tetris is a SEPARATE spec, not a transposed Merge. "The same sound but
  // higher" needs 2^(cents/1200), i.e. exp, which is the portability trap this
  // synth exists to avoid — see the note on Merge.
  Lock,
  Tetris,
  LevelUp,
  // Added for Sokoban (term-game#8), which asks for "step, push, crate-on-goal,
  // level complete" — and gets ONE new id, the fewest of any game so far.
  //
  // Reused instead: a step is Click (a generic acknowledged gesture, and a
  // Sokoban step is the most generic gesture in the suite), a push is Slide
  // (a decisive movement — already 2048's slide and Tetris' hard drop), and
  // finishing a level is Win.
  //
  // Only seating a crate on its goal had nothing that meant it. It is not
  // Merge: nothing combines and nothing vanishes, one thing arrives where it
  // belongs and stays there. It is the one moment of progress in a game with no
  // score, so it is the one sound worth adding.
  //
  // ⚠ There is deliberately no sound for a BLOCKED move. A player walking into
  // a wall is holding a direction, so a rejection tone fires as fast as the key
  // repeats — the metronome argument again, for the fourth game running.
  Seat,
};

// Every id, in order, so a test can loop the whole bank and a new effect cannot
// be added without being covered. Same shape as kLevels[] in board.hpp.
//
// ⚠ The static_assert below is not ceremony. The first draft of this array had
// Explode twice and no MenuSelect — which reads as fine, keeps the right
// length, and would have quietly excluded one whole effect from every test that
// loops the bank. A hand-written list parallel to an enum wants a machine to
// check it.
// ⚠ ENUM order, not thematic order. Slide and Merge are appended at the end
// rather than grouped with the other in-game effects, because appending cannot
// renumber an existing id — and kBank is indexed by that number.
inline constexpr std::array<SfxId, 15> kSfxIds{
    SfxId::Click,    SfxId::Reveal,     SfxId::Flag,  SfxId::Explode,
    SfxId::Win,      SfxId::Lose,       SfxId::MenuMove, SfxId::MenuSelect,
    SfxId::Slide,    SfxId::Merge,      SfxId::Eat,
    SfxId::Lock,     SfxId::Tetris,     SfxId::LevelUp,
    SfxId::Seat,
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

}  // namespace glyphcade::audio
