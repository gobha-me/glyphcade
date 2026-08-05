#pragma once

// glyphcade — Snake: what a cell looks like, at both capability tiers.
//
// Presentation over an already-resolved model, which is why Dead is DERIVED here
// (state == Lost, at the head) rather than stored in board.hpp. Dying is a rule;
// how the corpse looks is not.
//
// ⚠ Why this is its own header rather than detail inside snake.cpp: the three
// static_asserts at the bottom, the same three minesweeper's glyphs.hpp carries.
// The distinctness one is again the one that matters — FallbackDriver discards
// colour, so at the bottom tier the glyph is the ONLY thing separating the head
// from the body from the food, and the reference distinguishes all three by
// colour alone (renderer.js paints a brighter cyan head and a red food; every
// cell is the same rounded rectangle). Ported naively, that is an unplayable
// game on a bare terminal and it is invisible to anyone developing on a colour
// one.
//
// ⚠ Every glyph here is TWO columns, not one. See layout.hpp for why a cell is
// two columns wide; the consequence for this file is that the width assert
// checks 2, and that a glyph is a PAIR of characters rather than a doubled one —
// which is what lets the head read as a head rather than as a wider body.

#include <array>
#include <cstdint>
#include <string_view>

#include <termforge/widgets/detail/width.hpp>

#include <glyphcade/games/snake/layout.hpp>

namespace glyphcade::snake {

enum class Cell : std::uint8_t { Empty, Head, Body, Food, Dead };

struct CellGlyphs {
  std::string_view empty;
  std::string_view head;
  std::string_view body;
  std::string_view food;
  std::string_view dead;
};

// The 7-bit floor: the tier the Shell picks when the driver reports no colour,
// which is also the tier every headless test runs under (test_run_frames
// installs FallbackDriver) and the tier AGENTS.md promises always works.
//
// The three live glyphs are deliberately far apart in visual weight — a solid
// mass, a small round mark and a star — rather than a case distinction like
// O/o, which is legible in a proportional editor font and much less so in a
// terminal at speed.
inline constexpr CellGlyphs kAsciiCells{
    .empty = "  ",
    .head = "@@",
    .body = "oo",
    .food = "**",
    .dead = "XX",
};

// The colour tier. Every code point here measures one column by termforge's own
// width table, drawn from Block Elements, Geometric Shapes and Dingbats — the
// families termforge already bets on for its box drawing, and the same ones
// minesweeper's tile set draws from.
inline constexpr CellGlyphs kUnicodeCells{
    .empty = "  ",
    .head = "██",  // █ FULL BLOCK
    .body = "▒▒",  // ▒ MEDIUM SHADE
    .food = "◆◆",  // ◆ BLACK DIAMOND
    .dead = "✗✗",  // ✗ BALLOT X
};

[[nodiscard]] constexpr auto cells_for(bool ascii) noexcept -> const CellGlyphs& {
  return ascii ? kAsciiCells : kUnicodeCells;
}

[[nodiscard]] constexpr auto glyph_for(const CellGlyphs& g, Cell c) noexcept
    -> std::string_view {
  switch (c) {
    case Cell::Empty: return g.empty;
    case Cell::Head: return g.head;
    case Cell::Body: return g.body;
    case Cell::Food: return g.food;
    case Cell::Dead: return g.dead;
  }
  return g.empty;
}

struct Rgb8 {
  std::uint8_t r, g, b;
};

// Ported from the reference's theme (snake/css/style.css): cyan snake with a
// brighter head, red food. Applied only at the Unicode tier and always redundant
// with the glyph, never instead of it.
inline constexpr Rgb8 kHeadColor{0x7D, 0xE8, 0xFF};  // brighter than the body
inline constexpr Rgb8 kBodyColor{0x00, 0xD4, 0xFF};  // the reference's accent
inline constexpr Rgb8 kFoodColor{0xEF, 0x44, 0x44};
inline constexpr Rgb8 kDeadColor{0xEF, 0x44, 0x44};

// ─── Compile-time tier guarantees ─────────────────────────────────────────

namespace glyph_detail {

inline constexpr Cell kAllCells[]{Cell::Empty, Cell::Head, Cell::Body,
                                  Cell::Food, Cell::Dead};

[[nodiscard]] constexpr auto all_seven_bit(const CellGlyphs& g) -> bool {
  for (const Cell c : kAllCells) {
    for (const char ch : glyph_for(g, c)) {
      if (static_cast<unsigned char>(ch) >= 0x80) return false;
    }
  }
  return true;
}

[[nodiscard]] constexpr auto all_cell_width(const CellGlyphs& g) -> bool {
  for (const Cell c : kAllCells) {
    if (termforge::detail::display_width(glyph_for(g, c)) != kCellCols) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] constexpr auto all_distinct(const CellGlyphs& g) -> bool {
  for (const Cell a : kAllCells) {
    for (const Cell b : kAllCells) {
      if (a == b) continue;
      if (glyph_for(g, a) == glyph_for(g, b)) return false;
    }
  }
  return true;
}

}  // namespace glyph_detail

// The floor is literally 7-bit. AGENTS.md promises every game is playable at the
// bottom tier; this is that promise as a build failure.
static_assert(glyph_detail::all_seven_bit(kAsciiCells),
              "the ASCII cell table contains a byte >= 0x80 — the bottom tier "
              "must be 7-bit, see AGENTS.md");

// Measured by the same table Screen::write_text lays glyphs out with. A glyph
// that is not exactly kCellCols wide shifts every cell to its right for the rest
// of the row — the failure icon_is_safe() exists to prevent in the selector, and
// the reason a two-column cell needs its own assert rather than minesweeper's.
static_assert(glyph_detail::all_cell_width(kAsciiCells) &&
                  glyph_detail::all_cell_width(kUnicodeCells),
              "a cell glyph does not measure exactly kCellCols columns");

// ⚠ The one that matters. The reference separates head, body and food by COLOUR
// only; FallbackDriver discards colour outright, so a faithful port would be a
// board of identical marks on a bare terminal.
static_assert(glyph_detail::all_distinct(kAsciiCells) &&
                  glyph_detail::all_distinct(kUnicodeCells),
              "two cells share a glyph — at the no-colour tier the glyph is the "
              "only carrier of state");

}  // namespace glyphcade::snake
