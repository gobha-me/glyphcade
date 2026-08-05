#pragma once

// glyphcade — Minesweeper: where the grid is on screen. Integers only.
//
// ⚠ Like board.hpp, this header names no termforge type — it takes a screen
// size as two ints and hands back coordinates. That is what lets the geometry
// be tested exhaustively over a size matrix with no Screen in the process.
//
// One Layout value is computed per frame and consumed by BOTH draw() and
// on_event(). That is the point of the file: if drawing and hit-testing derive
// their coordinates separately they will eventually disagree, and a grid where
// the cell you click is not the cell you see is the worst kind of bug to read a
// bug report about. termforge's own widget.hpp calls out the same hazard.

#include <optional>

#include <glyphcade/games/minesweeper/board.hpp>  // Coord — and still no termforge

namespace glyphcade::minesweeper {

// A cell is two columns wide: a gutter column and a glyph column.
//
//   interior x:  0   1   2   3   4  ...  2C-2  2C-1   2C
//                [   #   ]   #   ' '      #     ' '   ' '
//                 cell 0    cell 1            cell C-1  ^ trailing column
//
// The gutter is where the cursor's brackets go — see glyphs.hpp for why the
// cursor must be a character and not a colour. The trailing column at 2C exists
// solely so the closing bracket of a cursor on the LAST column has somewhere to
// live inside the frame: the maximum index written is 2*(C-1)+2 == 2C, so no
// draw can land on the border or off the screen. Deleting it moves a write onto
// the frame's right edge on every board.
inline constexpr int kCellCols = 2;

// Frame border (2) + the trailing bracket column (1).
inline constexpr int kChromeCols = 3;
// Frame border (2) + the status row (1) + the hint row (1).
inline constexpr int kChromeRows = 4;

[[nodiscard]] constexpr auto needed_cols(int cols) noexcept -> int {
  return kCellCols * cols + kChromeCols;
}
[[nodiscard]] constexpr auto needed_rows(int rows) noexcept -> int {
  return rows + kChromeRows;
}

// Easy 21x13, Medium 35x20, Hard 63x20. All three fit an 80x24 terminal; Easy
// needs 21 columns while the Shell's floor is 20, so the "does not fit" path is
// reachable in practice and has to be a real screen, not an assert.
struct Layout {
  bool fits{false};
  int frame_x{0};
  int frame_y{0};
  int frame_w{0};
  int frame_h{0};
  int origin_x{0};  // interior x of cell (r, 0)'s gutter column
  int origin_y{0};  // interior y of row 0
  int rows{0};
  int cols{0};
  int status_y{0};
  int hint_y{0};

  [[nodiscard]] constexpr auto gutter_x(int col) const noexcept -> int {
    return origin_x + kCellCols * col;
  }
  [[nodiscard]] constexpr auto glyph_x(int col) const noexcept -> int {
    return gutter_x(col) + 1;
  }
  [[nodiscard]] constexpr auto row_y(int row) const noexcept -> int {
    return origin_y + row;
  }

  // The inverse, and the reason this type exists. Returns nullopt for the
  // border, the status and hint rows, the trailing bracket column, and anything
  // off-screen — every one of which is somewhere a player can click.
  //
  // Note that BOTH columns of a cell map back to it: a click on the gutter is a
  // click on the cell it belongs to. Mapping only the glyph column would make
  // half the grid dead to the mouse, which is exactly the limitation the
  // Shell's selector gutter has (termforge #72) and there is no reason to
  // repeat it here.
  [[nodiscard]] constexpr auto cell_at(int x, int y) const noexcept
      -> std::optional<Coord> {
    if (!fits) return std::nullopt;
    const int ry = y - origin_y;
    if (ry < 0 || ry >= rows) return std::nullopt;
    const int rx = x - origin_x;
    if (rx < 0) return std::nullopt;
    const int col = rx / kCellCols;
    if (col >= cols) return std::nullopt;  // includes the trailing column
    return Coord{.row = ry, .col = col};
  }
};

// screen_cols/screen_rows are the whole Screen: a running game owns all of it,
// so these coordinates are also the coordinates MouseEvent arrives in.
[[nodiscard]] constexpr auto compute_layout(int screen_cols, int screen_rows,
                                            int rows, int cols) noexcept
    -> Layout {
  Layout out;
  out.rows = rows;
  out.cols = cols;
  // The status row is row 0 and the hint row is the last row, whether or not
  // the board fits — a player on a too-small terminal still needs to be told
  // what is wrong and which keys change it.
  out.status_y = 0;
  out.hint_y = screen_rows - 1;

  const int want_cols = needed_cols(cols);
  const int want_rows = needed_rows(rows);
  out.fits = screen_cols >= want_cols && screen_rows >= want_rows;
  if (!out.fits) return out;

  out.frame_w = want_cols;
  out.frame_h = rows + 2;
  out.frame_x = (screen_cols - out.frame_w) / 2;
  // Centred in the band between the status row and the hint row.
  const int band_top = 1;
  const int band_h = screen_rows - 2;
  out.frame_y = band_top + (band_h - out.frame_h) / 2;
  out.origin_x = out.frame_x + 1;
  out.origin_y = out.frame_y + 1;
  return out;
}

}  // namespace glyphcade::minesweeper
