#pragma once

// glyphcade — Minesweeper: what a cell looks like, at both capability tiers.
//
// This is the presentation layer over an already-resolved model, which is why
// two facts the reference stores are DERIVED here instead: which mines to show
// on a lost board (state == Lost && mine), and which flags turned out to be
// wrong (mark == Flag && !mine). Neither is a rule, so neither belongs in
// board.hpp.
//
// ⚠ Why this is its own header rather than detail inside minesweeper.cpp: the
// three static_asserts at the bottom. They make "playable in 7-bit ASCII" a
// compile error rather than a promise, in the same spirit as icon_is_safe() in
// arcade/game_meta.hpp. The most important of them is the distinctness check —
// FallbackDriver discards colour entirely, so at the bottom tier the glyph is
// the ONLY carrier of a cell's state, and two states sharing a glyph is an
// unplayable game that no colour-capable developer would ever notice.

#include <array>
#include <cstddef>
#include <string_view>

#include <termforge/widgets/detail/width.hpp>

#include <glyphcade/games/minesweeper/board.hpp>

namespace glyphcade::minesweeper {

enum class Tile : std::uint8_t {
  Hidden,
  Empty,
  N1, N2, N3, N4, N5, N6, N7, N8,
  Flag,
  Question,
  Mine,
  Exploded,
  WrongFlag,
};

struct TileGlyphs {
  std::string_view hidden;
  std::string_view empty;
  std::array<std::string_view, 8> numbers;
  std::string_view flag;
  std::string_view question;
  std::string_view mine;
  std::string_view exploded;
  std::string_view wrong;
};

// The 7-bit floor. This is the tier the Shell picks when the driver reports no
// colour, which is also the tier every headless test runs under (test_run_frames
// installs FallbackDriver) and the tier AGENTS.md promises always works.
inline constexpr TileGlyphs kAsciiTiles{
    .hidden = "#",
    .empty = " ",
    .numbers = {"1", "2", "3", "4", "5", "6", "7", "8"},
    .flag = "F",
    .question = "?",
    .mine = "*",
    .exploded = "@",
    .wrong = "X",
};

// The colour tier. Every glyph here is a single code point that measures one
// column by termforge's own width table — drawn from Geometric Shapes, Dingbats
// and Block Elements, the same families termforge already bets on for its box
// drawing. Colour is layered on top of these and is always redundant with them.
inline constexpr TileGlyphs kUnicodeTiles{
    .hidden = "▒",    // ▒ MEDIUM SHADE
    .empty = " ",
    .numbers = {"1", "2", "3", "4", "5", "6", "7", "8"},
    .flag = "⚑",      // ⚑ BLACK FLAG
    .question = "?",
    .mine = "●",      // ● BLACK CIRCLE
    .exploded = "◉",  // ◉ FISHEYE
    .wrong = "✗",     // ✗ BALLOT X
};

[[nodiscard]] constexpr auto tiles_for(bool ascii) noexcept -> const TileGlyphs& {
  return ascii ? kAsciiTiles : kUnicodeTiles;
}

[[nodiscard]] constexpr auto glyph_for(const TileGlyphs& g, Tile t) noexcept
    -> std::string_view {
  switch (t) {
    case Tile::Hidden: return g.hidden;
    case Tile::Empty: return g.empty;
    case Tile::N1: return g.numbers[0];
    case Tile::N2: return g.numbers[1];
    case Tile::N3: return g.numbers[2];
    case Tile::N4: return g.numbers[3];
    case Tile::N5: return g.numbers[4];
    case Tile::N6: return g.numbers[5];
    case Tile::N7: return g.numbers[6];
    case Tile::N8: return g.numbers[7];
    case Tile::Flag: return g.flag;
    case Tile::Question: return g.question;
    case Tile::Mine: return g.mine;
    case Tile::Exploded: return g.exploded;
    case Tile::WrongFlag: return g.wrong;
  }
  return g.hidden;
}

// The only place model state becomes a look. Order matters: a finished board
// overrides marks, because the player has stopped guessing and wants the
// answer. Not constexpr — Board::at() lives in a translation unit — which is
// why the static_asserts below are written against glyph_for() instead.
[[nodiscard]] inline auto tile_for(const Board& b, Coord p) -> Tile {
  const Cell& c = b.at(p);

  if (b.state() == State::Lost) {
    if (c.mine) {
      const auto hit = b.exploded();
      return (hit && *hit == p) ? Tile::Exploded : Tile::Mine;
    }
    // A flag on a safe cell: the mistake that cost the game, and the reference
    // never shows it. Derived, not stored.
    if (c.mark == Mark::Flag) return Tile::WrongFlag;
  }

  if (!c.revealed) {
    if (c.mark == Mark::Flag) return Tile::Flag;
    if (c.mark == Mark::Question) return Tile::Question;
    return Tile::Hidden;
  }
  if (c.adjacent == 0) return Tile::Empty;
  return static_cast<Tile>(static_cast<std::uint8_t>(Tile::N1) + c.adjacent - 1);
}

// Classic Minesweeper number colours, applied only at the Unicode tier and only
// as reinforcement — the digit already says everything the colour does.
struct Rgb8 {
  std::uint8_t r, g, b;
};
inline constexpr std::array<Rgb8, 8> kNumberColors{{
    {0x38, 0xBD, 0xF8},  // 1 blue
    {0x10, 0xB9, 0x81},  // 2 green
    {0xEF, 0x44, 0x44},  // 3 red
    {0x8B, 0x5C, 0xF6},  // 4 purple
    {0xF5, 0x9E, 0x0B},  // 5 amber
    {0x06, 0xB6, 0xD4},  // 6 cyan
    {0xEC, 0x48, 0x99},  // 7 pink
    {0x9C, 0xA3, 0xAF},  // 8 grey
}};

// ─── Compile-time tier guarantees ─────────────────────────────────────────

namespace glyph_detail {

inline constexpr Tile kAllTiles[]{
    Tile::Hidden, Tile::Empty, Tile::N1, Tile::N2, Tile::N3,
    Tile::N4, Tile::N5, Tile::N6, Tile::N7, Tile::N8,
    Tile::Flag, Tile::Question, Tile::Mine, Tile::Exploded, Tile::WrongFlag,
};

[[nodiscard]] constexpr auto all_seven_bit(const TileGlyphs& g) -> bool {
  for (const Tile t : kAllTiles) {
    for (const char ch : glyph_for(g, t)) {
      if (static_cast<unsigned char>(ch) >= 0x80) return false;
    }
  }
  return true;
}

[[nodiscard]] constexpr auto all_width_one(const TileGlyphs& g) -> bool {
  for (const Tile t : kAllTiles) {
    if (termforge::detail::display_width(glyph_for(g, t)) != 1) return false;
  }
  return true;
}

[[nodiscard]] constexpr auto all_distinct(const TileGlyphs& g) -> bool {
  for (const Tile a : kAllTiles) {
    for (const Tile b : kAllTiles) {
      if (a == b) continue;
      if (glyph_for(g, a) == glyph_for(g, b)) return false;
    }
  }
  return true;
}

}  // namespace glyph_detail

// The floor is literally 7-bit. AGENTS.md promises every game is playable at
// the bottom tier; this is that promise as a build failure.
static_assert(glyph_detail::all_seven_bit(kAsciiTiles),
              "the ASCII tile table contains a byte >= 0x80 — the bottom tier "
              "must be 7-bit, see AGENTS.md");

// Measured by the same table Screen::write_text lays glyphs out with. A
// two-column glyph here shifts every cell to its right for the rest of the row
// — the same failure icon_is_safe() exists to prevent in the selector.
static_assert(glyph_detail::all_width_one(kAsciiTiles) &&
                  glyph_detail::all_width_one(kUnicodeTiles),
              "a tile glyph does not measure exactly one column");

// ⚠ The one that matters. FallbackDriver discards colour, so at the bottom tier
// the glyph is the only thing distinguishing a flag from a mine from a hidden
// cell. Two states sharing a glyph is an unplayable game, and it is invisible
// to anyone developing on a colour terminal.
static_assert(glyph_detail::all_distinct(kAsciiTiles) &&
                  glyph_detail::all_distinct(kUnicodeTiles),
              "two tiles share a glyph — at the no-colour tier the glyph is the "
              "only carrier of state");

}  // namespace glyphcade::minesweeper
