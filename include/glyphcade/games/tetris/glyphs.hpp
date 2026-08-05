#pragma once

// glyphcade — Tetris: what a cell looks like, at both capability tiers.
//
// Presentation over an already-resolved model. The board stores which
// tetromino left each block; what that block LOOKS like is decided here.
//
// ⚠ THE REFERENCE SEPARATES ALL SEVEN PIECES BY COLOUR AND NOTHING ELSE.
// state.js:19-27 is a table of seven hex colours, and renderer.js draws every
// block as the same rounded rectangle. FallbackDriver discards colour entirely,
// so a naive port is a board of identical grey blocks on a bare terminal — the
// exact trap Snake's glyphs.hpp records for head-vs-body-vs-food, one game
// earlier and with three cells instead of seven.
//
// ⚠ AND YET THE PIECES DO NOT GET SEVEN DIFFERENT GLYPHS. That is the decision
// worth writing down, because "distinguish everything" is the reflex. Once a
// piece is locked, WHICH piece it was carries no information a player can act
// on: the stack is just occupied cells, and the only questions are "is this
// square full" and "is this row nearly complete". Seven glyphs would be seven
// things to read where the game asks one. What genuinely must be
// distinguishable at the bottom tier is a much shorter list, and it is the list
// below: empty, stack, the piece you are steering, its ghost, and a row that is
// about to vanish.
//
// Colour still varies per piece on the colour tier, where it is free and
// pretty. It is an enhancement over a glyph fallback that always exists — the
// same relationship AGENTS.md requires of pixel sprites.
//
// ⚠ Every glyph here is TWO columns, not one. See layout.hpp.

#include <array>
#include <cstdint>
#include <string_view>

#include <termforge/core/types.hpp>
#include <termforge/widgets/detail/width.hpp>

#include <glyphcade/games/tetris/layout.hpp>

namespace glyphcade::tetris {

// What a screen cell in the well is showing, after the model has been resolved.
enum class Cell : std::uint8_t { Empty, Stack, Active, Ghost, Clearing };

struct CellGlyphs {
  std::string_view empty;
  std::string_view stack;
  std::string_view active;
  std::string_view ghost;
  std::string_view clearing;
};

// The 7-bit floor: the tier the Shell picks when the driver reports no colour,
// which is also the tier every headless test runs under and the one AGENTS.md
// promises always works.
//
// Chosen far apart in visual weight rather than by case or by similar
// punctuation: a solid mass for the stack, a heavier mass for the live piece, a
// hollow outline for the ghost, and a row of dashes for a line on its way out.
inline constexpr CellGlyphs kAsciiCells{
    .empty = "  ",
    .stack = "[]",
    .active = "##",
    .ghost = "::",
    .clearing = "--",
};

// The colour tier. Every code point measures one column by termforge's own
// table, so a pair is exactly two columns — the same rule Snake's tier keeps.
inline constexpr CellGlyphs kUnicodeCells{
    .empty = "  ",
    .stack = "▓▓",
    .active = "██",
    .ghost = "░░",
    .clearing = "▁▁",
};

[[nodiscard]] constexpr auto cells_for(bool ascii) noexcept
    -> const CellGlyphs& {
  return ascii ? kAsciiCells : kUnicodeCells;
}

[[nodiscard]] constexpr auto glyph_for(const CellGlyphs& g, Cell c) noexcept
    -> std::string_view {
  switch (c) {
    case Cell::Empty: return g.empty;
    case Cell::Stack: return g.stack;
    case Cell::Active: return g.active;
    case Cell::Ghost: return g.ghost;
    case Cell::Clearing: return g.clearing;
  }
  return g.empty;
}

// Per-piece colour, colour tier only. The reference's palette (state.js:19-27),
// which is the standard one and the thing a returning player recognises.
[[nodiscard]] constexpr auto colour_for(Piece p) noexcept -> termforge::Rgb {
  switch (p) {
    case Piece::I: return {0x00, 0xF0, 0xF0};
    case Piece::O: return {0xF0, 0xF0, 0x00};
    case Piece::T: return {0xA0, 0x00, 0xF0};
    case Piece::S: return {0x00, 0xF0, 0x00};
    case Piece::Z: return {0xF0, 0x00, 0x00};
    case Piece::J: return {0x00, 0x00, 0xF0};
    case Piece::L: return {0xF0, 0xA0, 0x00};
  }
  return {0xE0, 0xE0, 0xF0};
}

// ── The three assertions ────────────────────────────────────────────────────

constexpr auto ascii_is_seven_bit() noexcept -> bool {
  for (const std::string_view s :
       {kAsciiCells.empty, kAsciiCells.stack, kAsciiCells.active,
        kAsciiCells.ghost, kAsciiCells.clearing}) {
    for (const char c : s) {
      if (static_cast<unsigned char>(c) >= 0x80) return false;
    }
  }
  return true;
}
static_assert(ascii_is_seven_bit(),
              "the ASCII tier must be 7-bit: it is the tier for terminals that "
              "have told us they cannot render anything else");

constexpr auto every_glyph_is_one_cell_wide() noexcept -> bool {
  for (const CellGlyphs& g : {kAsciiCells, kUnicodeCells}) {
    for (const std::string_view s :
         {g.empty, g.stack, g.active, g.ghost, g.clearing}) {
      if (termforge::detail::display_width(s) != kCellCols) return false;
    }
  }
  return true;
}
static_assert(every_glyph_is_one_cell_wide(),
              "a cell glyph is not exactly kCellCols columns — a narrow one "
              "shifts every cell to its right for the rest of the frame");

// ⚠ THE ONE THAT MATTERS. Two glyphs that render alike are two things the
// player cannot tell apart, and at the bottom tier the glyph is all there is.
// The ghost against the active piece is the pair to watch: they are the same
// shape in the same well, and if they collided the player would be steering
// something they cannot locate.
constexpr auto live_glyphs_are_distinct() noexcept -> bool {
  for (const CellGlyphs& g : {kAsciiCells, kUnicodeCells}) {
    const std::string_view live[]{g.empty, g.stack, g.active, g.ghost,
                                  g.clearing};
    for (std::size_t i = 0; i < std::size(live); ++i) {
      for (std::size_t j = i + 1; j < std::size(live); ++j) {
        if (live[i] == live[j]) return false;
      }
    }
  }
  return true;
}
static_assert(live_glyphs_are_distinct(),
              "two cell glyphs are identical — FallbackDriver discards colour, "
              "so at the bottom tier the glyph is the only thing telling the "
              "stack, the active piece and its ghost apart");

}  // namespace glyphcade::tetris
