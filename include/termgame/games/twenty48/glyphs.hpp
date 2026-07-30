#pragma once

// 2048's presentation tables, and the compile-time guarantees on them.
//
// This file exists for the static_asserts at the bottom, the same way
// games/minesweeper/glyphs.hpp does. The tables themselves would be unremarkable
// inline in the draw code; what is not unremarkable is that every label fits a
// tile and that the bottom tier stays 7-bit, and neither is checkable once the
// values are literals at a call site.
//
// ── What differs between the two tiers, and what must not ───────────────────
//
// A tile's NUMBER is the information. It is identical at both tiers, because
// FallbackDriver discards colour and a board whose state is carried by background
// colour is a board that cannot be played on a bare TTY — the tier this repo
// promises always works, and the only tier the headless tests can reach.
//
// So colour is reinforcement here, never information: the ramp makes 512 and 1024
// distinguishable at a glance, and their labels already distinguish them. What
// the ASCII tier loses is prettiness, not playability.
//
// The one thing the bottom tier genuinely needs is the LATTICE — without a
// visible grid, four blank cells and a gap are indistinguishable from empty
// space, so the gap cells get a rule glyph instead of colour.

#include <array>
#include <cstdint>
#include <string_view>

#include <termforge/widgets/detail/width.hpp>

#include <termgame/games/twenty48/board.hpp>

// For kTileCols, and the dependency direction is deliberate: the assertion this
// file exists for — "every label a tile can hold fits in a tile" — is a statement
// about the label table AND the geometry at once, so it can only live in whichever
// of the two includes the other. Glyphs are drawn into a layout, not the reverse.
#include <termgame/games/twenty48/layout.hpp>

namespace termgame::twenty48 {

// Local, so this header needs no termforge colour type — same trick as
// minesweeper/glyphs.hpp, which keeps the include above to the width table alone.
struct Rgb8 {
  std::uint8_t r{0};
  std::uint8_t g{0};
  std::uint8_t b{0};
};

// The lattice: what fills the one-cell gaps between tiles.
struct Lattice {
  std::string_view vertical;    // between two tiles in a row
  std::string_view horizontal;  // between two tiles in a column
  std::string_view cross;       // where a gap row meets a gap column
};

inline constexpr Lattice kAsciiLattice{
    .vertical = "|",
    .horizontal = "-",
    .cross = "+",
};

inline constexpr Lattice kUnicodeLattice{
    .vertical = "│",    // BOX DRAWINGS LIGHT VERTICAL
    .horizontal = "─",  // BOX DRAWINGS LIGHT HORIZONTAL
    .cross = "┼",       // BOX DRAWINGS LIGHT VERTICAL AND HORIZONTAL
};

[[nodiscard]] constexpr auto lattice_for(bool ascii) noexcept -> const Lattice& {
  return ascii ? kAsciiLattice : kUnicodeLattice;
}

// ── The colour ramp ─────────────────────────────────────────────────────────
//
// Ported from the reference's per-value CSS classes (2048/css/style.css:204-215),
// which is a dark-theme ramp rather than the classic beige one: cool greys for
// 2 and 4, then violet through pink through red as the value climbs, and amber
// for 2048 itself. Two deliberate flattenings: the reference's 2048 and >2048
// tiers are CSS gradients with a box-shadow, and a terminal cell has one
// background colour, so each becomes its single dominant tone.
struct TileColor {
  int value{0};
  Rgb8 bg{};
  Rgb8 fg{};
};

inline constexpr Rgb8 kTileFgDim{0xE0, 0xE0, 0xF0};   // 2 and 4
inline constexpr Rgb8 kTileFgBright{0xFF, 0xFF, 0xFF};  // 8 and up

// The reference switches text colour between 4 and 8. Kept, even though on a dark
// ramp it is a barely-perceptible off-white to white step, because it is the one
// place the reference says "these two tiers are the low ones" in the styling.
inline constexpr std::array<TileColor, 11> kTileColors{{
    {2, {0x3A, 0x3A, 0x5C}, kTileFgDim},
    {4, {0x4A, 0x4A, 0x6C}, kTileFgDim},
    {8, {0x63, 0x66, 0xF1}, kTileFgBright},
    {16, {0x81, 0x8C, 0xF8}, kTileFgBright},
    {32, {0xA7, 0x8B, 0xFA}, kTileFgBright},
    {64, {0xC0, 0x84, 0xFC}, kTileFgBright},
    {128, {0xE8, 0x79, 0xF9}, kTileFgBright},
    {256, {0xF4, 0x72, 0xB6}, kTileFgBright},
    {512, {0xFB, 0x71, 0x85}, kTileFgBright},
    {1024, {0xF8, 0x71, 0x71}, kTileFgBright},
    {2048, {0xF5, 0x9E, 0x0B}, kTileFgBright},  // the gradient's amber
}};

// Anything above 2048 — the reference's .tile-super.
inline constexpr TileColor kSuperTile{0, {0xDC, 0x26, 0x26}, kTileFgBright};

// An empty cell's background at the colour tier. The reference's --cell-bg.
inline constexpr Rgb8 kEmptyBg{0x2A, 0x2A, 0x4A};

[[nodiscard]] constexpr auto color_for(int value) noexcept -> TileColor {
  for (const auto& t : kTileColors) {
    if (t.value == value) {
      return t;
    }
  }
  return kSuperTile;
}

// ── Labels ──────────────────────────────────────────────────────────────────
//
// Hand-rolled rather than std::to_string / std::format: this runs on the render
// path once per occupied cell per frame, and the repo has no formatting
// dependency. Same argument as minesweeper.cpp's pad3().
//
// Returns the digit count; writes into `out` without a terminator. constexpr so
// the width assertions below can run it at compile time.
struct Label {
  std::array<char, 8> text{};
  int len{0};

  [[nodiscard]] constexpr auto view() const -> std::string_view {
    return std::string_view{text.data(), static_cast<std::size_t>(len)};
  }
};

[[nodiscard]] constexpr auto label_for(int value) noexcept -> Label {
  Label out{};
  if (value <= 0) {
    return out;
  }
  // Digits emerge least-significant first, so fill from the back of a scratch
  // buffer and copy forward.
  std::array<char, 8> rev{};
  int n = 0;
  for (int v = value; v > 0 && n < static_cast<int>(rev.size()); v /= 10) {
    rev[static_cast<std::size_t>(n++)] = static_cast<char>('0' + (v % 10));
  }
  for (int i = 0; i < n; ++i) {
    out.text[static_cast<std::size_t>(i)] =
        rev[static_cast<std::size_t>(n - 1 - i)];
  }
  out.len = n;
  return out;
}

// ── The guarantees ──────────────────────────────────────────────────────────

namespace glyph_detail {

// Every value a tile can hold: the powers of two from 2 up to kMaxTile. Written
// as a generator rather than a hand-listed table so it cannot fall out of step
// with kMaxTile the way a literal list would.
constexpr auto every_tile_value_fits() -> bool {
  for (int v = 2; v <= kMaxTile; v *= 2) {
    if (label_for(v).len > kTileCols) {
      return false;
    }
  }
  return true;
}

constexpr auto labels_are_seven_bit() -> bool {
  for (int v = 2; v <= kMaxTile; v *= 2) {
    const auto l = label_for(v);
    for (int i = 0; i < l.len; ++i) {
      const auto c = static_cast<unsigned char>(l.text[static_cast<std::size_t>(i)]);
      if (c >= 0x80) {
        return false;
      }
    }
  }
  return true;
}

constexpr auto lattice_is_seven_bit(const Lattice& l) -> bool {
  for (std::string_view s : {l.vertical, l.horizontal, l.cross}) {
    for (const char ch : s) {
      if (static_cast<unsigned char>(ch) >= 0x80) {
        return false;
      }
    }
  }
  return true;
}

constexpr auto lattice_is_width_one(const Lattice& l) -> bool {
  for (std::string_view s : {l.vertical, l.horizontal, l.cross}) {
    if (termforge::detail::display_width(s) != 1) {
      return false;
    }
  }
  return true;
}

constexpr auto ramp_covers_every_value_once() -> bool {
  // Each entry distinct, and ordered, so color_for's linear scan cannot be
  // shadowed by a duplicate and a reader can see the ramp climb.
  for (std::size_t i = 1; i < kTileColors.size(); ++i) {
    if (kTileColors[i].value <= kTileColors[i - 1].value) {
      return false;
    }
  }
  // The ramp must start at the spawn value and reach the win tile, or some
  // reachable tile silently renders as kSuperTile.
  return kTileColors.front().value == 2 && kTileColors.back().value == kWinTile;
}

}  // namespace glyph_detail

// ⚠ THE assertion of this file. kMaxTile is 131072 — six digits — and kTileCols
// is 6, so the widest legal tile fits with nothing to spare. Narrowing the tile,
// or growing the board (kCells feeds kMaxTile), breaks this and it breaks HERE,
// at compile time, instead of as a number clipped mid-digit on a board nobody
// reaches in testing.
static_assert(glyph_detail::every_tile_value_fits(),
              "a reachable tile value's label is wider than kTileCols — see "
              "kMaxTile in board.hpp and kTileCols in layout.hpp");

static_assert(glyph_detail::labels_are_seven_bit(),
              "a tile label is not 7-bit ASCII");

static_assert(glyph_detail::lattice_is_seven_bit(kAsciiLattice),
              "the ASCII lattice must be 7-bit — it is the tier that has to work "
              "on a terminal that told us it can draw nothing else, see AGENTS.md");

static_assert(glyph_detail::lattice_is_width_one(kAsciiLattice) &&
                  glyph_detail::lattice_is_width_one(kUnicodeLattice),
              "a lattice glyph does not measure exactly one column, so the grid "
              "would shear to the right of it");

static_assert(glyph_detail::ramp_covers_every_value_once(),
              "the colour ramp must be strictly ascending from 2 to kWinTile");

}  // namespace termgame::twenty48
