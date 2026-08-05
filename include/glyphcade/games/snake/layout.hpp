#pragma once

// glyphcade — Snake: where the playfield is on screen. Integers only.
//
// ⚠ Like board.hpp, this header names no termforge type — it takes a screen size
// as two ints and hands back coordinates, which is what lets the geometry be
// swept exhaustively over a size matrix with no Screen in the process.
//
// One Layout value is computed per frame, the same discipline minesweeper's
// layout.hpp keeps. There is no inverse (no cell_at) here and that is not an
// omission: Snake takes no mouse input, so nothing hit-tests. See the deferral
// note in STATUS.md — a click has nothing to mean in a game whose only verb is a
// direction, and inventing one is a feel decision with no reference behind it.

#include <glyphcade/games/snake/board.hpp>  // kCols/kRows — and still no termforge

namespace glyphcade::snake {

// A cell is TWO columns wide, and both carry the same glyph.
//
//   interior x:  0  1   2  3   4  5  ...
//                [cell 0][cell 1][cell 2]
//
// Not decoration and not the same argument minesweeper's two columns make (there
// the second column is a gutter for the cursor's brackets). A terminal cell is
// roughly twice as tall as it is wide, so a one-column playfield cell makes a
// 28x16 grid render as a tall thin rectangle and, worse, makes the snake's
// horizontal speed look like half its vertical speed on a board where they are
// equal. Two columns per cell is the cheapest way to make the grid read square.
inline constexpr int kCellCols = 2;

// Frame border only. Unlike minesweeper there is no trailing column, because
// nothing here draws outside the cell it belongs to.
inline constexpr int kChromeCols = 2;
// Frame border (2) + the status row (1) + the hint row (1).
inline constexpr int kChromeRows = 4;

// 28*2 + 2 = 58 columns, 16 + 4 = 20 rows. Inside minesweeper Hard's 63x20
// envelope, and well above the Shell's 20x8 floor — so the "does not fit" path
// is reachable in practice and has to be a real screen rather than an assert.
// Same answer Epics 3 and 4 gave. gitea #15 has since put this pair into
// kMeta's geometry so the menu warns first, but it warns rather than refuses,
// so the screen below is still what a player who presses Enter anyway sees.
//
// ⚠ THESE TWO NUMBERS MUST NEVER GROW WITH THE TERMINAL, and for this game
// that is correctness rather than taste: score_key(Level, Walls) in snake.cpp
// does not include the field size, so a bigger field would silently make every
// stored record incomparable with every new one. See AGENTS.md's rule.
inline constexpr int kNeedCols = (kCellCols * kCols) + kChromeCols;
inline constexpr int kNeedRows = kRows + kChromeRows;

static_assert(kNeedCols == 58);
static_assert(kNeedRows == 20);

struct Layout {
  bool fits{false};
  int frame_x{0};
  int frame_y{0};
  int frame_w{0};
  int frame_h{0};
  int origin_x{0};  // interior x of cell (0, y)'s first column
  int origin_y{0};  // interior y of row 0
  int status_y{0};
  int hint_y{0};

  [[nodiscard]] constexpr auto cell_x(int x) const noexcept -> int {
    return origin_x + (kCellCols * x);
  }
  [[nodiscard]] constexpr auto cell_y(int y) const noexcept -> int {
    return origin_y + y;
  }
};

// screen_cols/screen_rows are the whole Screen: a running game owns all of it.
[[nodiscard]] constexpr auto compute_layout(int screen_cols,
                                            int screen_rows) noexcept -> Layout {
  Layout out;
  // Status row 0 and hint row last, whether or not the board fits — a player on
  // a too-small terminal still needs to be told what is wrong and which keys
  // change it.
  out.status_y = 0;
  out.hint_y = screen_rows - 1;

  out.fits = screen_cols >= kNeedCols && screen_rows >= kNeedRows;
  if (!out.fits) return out;

  out.frame_w = kNeedCols;
  out.frame_h = kRows + 2;
  out.frame_x = (screen_cols - out.frame_w) / 2;
  // Centred in the band between the status row and the hint row.
  const int band_top = 1;
  const int band_h = screen_rows - 2;
  out.frame_y = band_top + ((band_h - out.frame_h) / 2);
  out.origin_x = out.frame_x + 1;
  out.origin_y = out.frame_y + 1;
  return out;
}

}  // namespace glyphcade::snake
