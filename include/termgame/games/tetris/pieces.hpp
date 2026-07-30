#pragma once

// term-game — Tetris: the seven tetrominoes, their four rotations, and the SRS
// wall-kick tables.
//
// ── This is the part of the reference that WORKS ─────────────────────────────
//
// Ported from HTML-Games' tetris/js/state.js:32-122. Almost everything else in
// that directory needed fixing (see board.hpp's defect table), but the rotation
// matrices and both kick tables were checked entry by entry against the
// standard SRS tables and are correct — including the sign convention, which
// game.js:285-299 converts explicitly because SRS is written y-up and a screen
// is y-down. Taking data verbatim is the whole point of having a reference; the
// alternative is retyping 88 offsets and introducing exactly the class of bug
// nobody finds by playing.
//
// ⚠ Separate header, unlike Snake, which keeps everything in board.hpp. These
// are DATA, and the boundary between "ported verbatim" and "written here" is
// worth being able to see in a file list. It also keeps board.hpp readable.
//
// ⚠ Like board.hpp, this file includes no termforge header.

#include <array>
#include <cstdint>

namespace termgame::tetris {

enum class Piece : std::uint8_t { I, O, T, S, Z, J, L };

// For cases that sweep every piece. Same shape as snake's kDirs and
// minesweeper's kLevels.
inline constexpr Piece kPieces[]{Piece::I, Piece::O, Piece::T, Piece::S,
                                 Piece::Z, Piece::J, Piece::L};
inline constexpr int kPieceCount = 7;

// One 7-bag is exactly the roster, which is what makes the bag testable: a bag
// is a permutation, so every piece must appear exactly once in any seven.
static_assert(std::size(kPieces) == kPieceCount);

inline constexpr int kRotations = 4;

// The largest bounding box any piece uses. I is 4x4, O is 2x2, the rest 3x3.
//
// ⚠ The box sizes are NOT uniform and must not be made uniform. SRS's kick
// offsets are expressed relative to each piece's own bounding box; normalising
// I and O into a 3x3 or everything into a 4x4 would silently move every kick.
inline constexpr int kBoxMax = 4;

// A rotation state, as a fixed 4x4 grid of 0/1 laid out the way it is drawn.
// Only the top-left box x box sub-grid is meaningful; the rest is padding and
// is asserted empty below.
using Shape = std::array<std::array<std::uint8_t, kBoxMax>, kBoxMax>;

struct PieceDef {
  int box{3};
  std::array<Shape, kRotations> states{};
};

// clang-format off
inline constexpr PieceDef kI{4, {{
  {{{0,0,0,0},
    {1,1,1,1},
    {0,0,0,0},
    {0,0,0,0}}},
  {{{0,0,1,0},
    {0,0,1,0},
    {0,0,1,0},
    {0,0,1,0}}},
  {{{0,0,0,0},
    {0,0,0,0},
    {1,1,1,1},
    {0,0,0,0}}},
  {{{0,1,0,0},
    {0,1,0,0},
    {0,1,0,0},
    {0,1,0,0}}},
}}};

// O does not rotate, and all four states are identical rather than special-cased
// in the rotate path. Cheaper than a branch, and it makes "rotating O is a
// no-op" a property of the data instead of a rule someone can forget.
inline constexpr PieceDef kO{2, {{
  {{{1,1,0,0},
    {1,1,0,0},
    {0,0,0,0},
    {0,0,0,0}}},
  {{{1,1,0,0},
    {1,1,0,0},
    {0,0,0,0},
    {0,0,0,0}}},
  {{{1,1,0,0},
    {1,1,0,0},
    {0,0,0,0},
    {0,0,0,0}}},
  {{{1,1,0,0},
    {1,1,0,0},
    {0,0,0,0},
    {0,0,0,0}}},
}}};

inline constexpr PieceDef kT{3, {{
  {{{0,1,0,0},
    {1,1,1,0},
    {0,0,0,0},
    {0,0,0,0}}},
  {{{0,1,0,0},
    {0,1,1,0},
    {0,1,0,0},
    {0,0,0,0}}},
  {{{0,0,0,0},
    {1,1,1,0},
    {0,1,0,0},
    {0,0,0,0}}},
  {{{0,1,0,0},
    {1,1,0,0},
    {0,1,0,0},
    {0,0,0,0}}},
}}};

inline constexpr PieceDef kS{3, {{
  {{{0,1,1,0},
    {1,1,0,0},
    {0,0,0,0},
    {0,0,0,0}}},
  {{{0,1,0,0},
    {0,1,1,0},
    {0,0,1,0},
    {0,0,0,0}}},
  {{{0,0,0,0},
    {0,1,1,0},
    {1,1,0,0},
    {0,0,0,0}}},
  {{{1,0,0,0},
    {1,1,0,0},
    {0,1,0,0},
    {0,0,0,0}}},
}}};

inline constexpr PieceDef kZ{3, {{
  {{{1,1,0,0},
    {0,1,1,0},
    {0,0,0,0},
    {0,0,0,0}}},
  {{{0,0,1,0},
    {0,1,1,0},
    {0,1,0,0},
    {0,0,0,0}}},
  {{{0,0,0,0},
    {1,1,0,0},
    {0,1,1,0},
    {0,0,0,0}}},
  {{{0,1,0,0},
    {1,1,0,0},
    {1,0,0,0},
    {0,0,0,0}}},
}}};

inline constexpr PieceDef kJ{3, {{
  {{{1,0,0,0},
    {1,1,1,0},
    {0,0,0,0},
    {0,0,0,0}}},
  {{{0,1,1,0},
    {0,1,0,0},
    {0,1,0,0},
    {0,0,0,0}}},
  {{{0,0,0,0},
    {1,1,1,0},
    {0,0,1,0},
    {0,0,0,0}}},
  {{{0,1,0,0},
    {0,1,0,0},
    {1,1,0,0},
    {0,0,0,0}}},
}}};

inline constexpr PieceDef kL{3, {{
  {{{0,0,1,0},
    {1,1,1,0},
    {0,0,0,0},
    {0,0,0,0}}},
  {{{0,1,0,0},
    {0,1,0,0},
    {0,1,1,0},
    {0,0,0,0}}},
  {{{0,0,0,0},
    {1,1,1,0},
    {1,0,0,0},
    {0,0,0,0}}},
  {{{1,1,0,0},
    {0,1,0,0},
    {0,1,0,0},
    {0,0,0,0}}},
}}};
// clang-format on

[[nodiscard]] constexpr auto def_for(Piece p) noexcept -> const PieceDef& {
  switch (p) {
    case Piece::I: return kI;
    case Piece::O: return kO;
    case Piece::T: return kT;
    case Piece::S: return kS;
    case Piece::Z: return kZ;
    case Piece::J: return kJ;
    case Piece::L: return kL;
  }
  return kT;
}

[[nodiscard]] constexpr auto cell_at(Piece p, int rot, int r, int c) noexcept
    -> bool {
  if (r < 0 || r >= kBoxMax || c < 0 || c >= kBoxMax) return false;
  return def_for(p).states[static_cast<std::size_t>(rot & 3)]
                          [static_cast<std::size_t>(r)]
                          [static_cast<std::size_t>(c)] != 0;
}

// ── The data checks itself ──────────────────────────────────────────────────
//
// A tetromino has four cells. That is what the name means, and a typo in the
// grids above is far more likely to change a cell count than to produce another
// legal shape — so it is the one property worth deciding at compile time rather
// than in a case somebody might not run.

constexpr auto every_state_has_four_cells() noexcept -> bool {
  for (const Piece p : kPieces) {
    for (int rot = 0; rot < kRotations; ++rot) {
      int n = 0;
      for (int r = 0; r < kBoxMax; ++r) {
        for (int c = 0; c < kBoxMax; ++c) {
          if (cell_at(p, rot, r, c)) ++n;
        }
      }
      if (n != 4) return false;
    }
  }
  return true;
}
static_assert(every_state_has_four_cells(),
              "a tetromino state does not have exactly four cells — check the "
              "grids above against tetris/js/state.js:32-96");

// Nothing may be set outside a piece's own bounding box. The padding columns
// exist only so every state can be written as a 4x4 literal; a cell out there
// would be invisible to the eye and would move the piece's effective width,
// which is what the spawn column and every kick are computed from.
constexpr auto nothing_outside_the_box() noexcept -> bool {
  for (const Piece p : kPieces) {
    const int box = def_for(p).box;
    for (int rot = 0; rot < kRotations; ++rot) {
      for (int r = 0; r < kBoxMax; ++r) {
        for (int c = 0; c < kBoxMax; ++c) {
          if ((r >= box || c >= box) && cell_at(p, rot, r, c)) return false;
        }
      }
    }
  }
  return true;
}
static_assert(nothing_outside_the_box(),
              "a piece has a cell outside its own bounding box — SRS kick "
              "offsets are relative to that box, so this moves every kick");

// ── Wall kicks ──────────────────────────────────────────────────────────────
//
// state.js:99-122, and the standard tables. Five candidate offsets per
// transition, tried in order; the first that fits wins, and a piece that fits
// none does not rotate at all.
//
// ⚠ THESE ARE Y-UP, and the board is y-down. The conversion is NOT baked in
// here on purpose: keeping the table in the same orientation as every published
// SRS reference is what lets a reader check it against one. board.cpp applies
// `-y`, exactly as game.js:293 does, and a case pins that it does.

struct Kick {
  int x{0};
  int y{0};
  auto operator==(const Kick&) const -> bool = default;
};

inline constexpr int kKicksPerTransition = 5;
using KickSet = std::array<Kick, kKicksPerTransition>;

// Indexed [from][to]. Only the eight adjacent transitions are ever looked up —
// a rotation is +1 or -1 — and the diagonal and 180s hold the identity kick, so
// a lookup that should not happen yields "no offset" rather than reading past
// the end.
using KickTable = std::array<std::array<KickSet, kRotations>, kRotations>;

[[nodiscard]] constexpr auto make_kick_table(bool is_i) noexcept -> KickTable {
  KickTable t{};
  if (is_i) {
    t[0][1] = {{{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}};
    t[1][0] = {{{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}}};
    t[1][2] = {{{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}}};
    t[2][1] = {{{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}};
    t[2][3] = {{{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}}};
    t[3][2] = {{{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}};
    t[3][0] = {{{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}};
    t[0][3] = {{{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}}};
  } else {
    t[0][1] = {{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}};
    t[1][0] = {{{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}};
    t[1][2] = {{{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}};
    t[2][1] = {{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}};
    t[2][3] = {{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}};
    t[3][2] = {{{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}};
    t[3][0] = {{{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}};
    t[0][3] = {{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}};
  }
  return t;
}

inline constexpr KickTable kKicksI = make_kick_table(true);
inline constexpr KickTable kKicksJlstz = make_kick_table(false);

// I gets its own table; O never rotates so its table is never consulted, and it
// shares JLSTZ's rather than being a third case that has to be kept in step.
[[nodiscard]] constexpr auto kicks_for(Piece p, int from, int to) noexcept
    -> const KickSet& {
  const KickTable& t = (p == Piece::I) ? kKicksI : kKicksJlstz;
  return t[static_cast<std::size_t>(from & 3)][static_cast<std::size_t>(to & 3)];
}

// The first candidate of every real transition is the identity, which is what
// makes "rotate in open space does not translate" true. A table whose first
// entry drifted would move every successful rotation by a cell, and it would
// look like a rendering bug.
constexpr auto every_transition_starts_at_origin() noexcept -> bool {
  for (int from = 0; from < kRotations; ++from) {
    for (const int to : {(from + 1) & 3, (from + 3) & 3}) {
      if (kicks_for(Piece::I, from, to)[0] != Kick{0, 0}) return false;
      if (kicks_for(Piece::T, from, to)[0] != Kick{0, 0}) return false;
    }
  }
  return true;
}
static_assert(every_transition_starts_at_origin(),
              "a kick set does not begin with the identity offset");

}  // namespace termgame::tetris
