#pragma once

// A fully specified PRNG, shared by every game that needs one.
//
// Lived in games/minesweeper/board.hpp until 2048 needed the same generator.
// Moved here rather than copied: two hand-rolled splitmix64s would be two things
// to keep identical, and the whole point of the class is that its output is
// pinned. `minesweeper::Rng` is still a valid spelling — board.hpp aliases it, so
// existing call sites did not move.
//
// ── Why not <random> ────────────────────────────────────────────────────────
//
// std::mt19937 is specified bit-for-bit but std::uniform_int_distribution is
// NOT — its mapping from engine output to range is implementation-defined. This
// repo builds under both libstdc++ and libc++, so "same seed, same board" via
// <random> would be a coin flip between toolchains, and a determinism test would
// pass on GCC while being a lie on Clang.
//
// The same argument the audio synth makes about sin/exp: a thing this repo
// asserts is reproducible has to be specified here, not inherited.
//
// ⚠ Changing anything below changes every game's layouts and spawns for a given
// seed. test/14minesweeper pins mine placement against fixed seeds, so a tweak
// here surfaces there — which is the tripwire working, not a reason to relax it.

#include <cstdint>

namespace termgame {

class Rng {
 public:
  explicit constexpr Rng(std::uint64_t seed) noexcept : m_state(seed) {}

  // splitmix64.
  constexpr auto next() noexcept -> std::uint64_t {
    m_state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = m_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  // Unbiased [0, n) by rejection on the low word (Lemire). Rejection here is
  // bounded — it discards at most a 1/n fraction per draw — unlike the mine
  // placement rejection loop minesweeper's board.cpp replaces.
  constexpr auto below(std::uint64_t n) noexcept -> std::uint64_t {
    if (n <= 1) {
      return 0;
    }
    const std::uint64_t limit = ~0ULL - (~0ULL % n);
    std::uint64_t r = next();
    while (r >= limit) {
      r = next();
    }
    return r % n;
  }

 private:
  std::uint64_t m_state;
};

}  // namespace termgame
