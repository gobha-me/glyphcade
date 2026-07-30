#pragma once

// term-game — Tetris: where everything is on screen. Integers only.
//
// ⚠ Like board.hpp, this header names no termforge type — it takes a screen
// size as two ints and hands back coordinates, which is what lets the geometry
// be swept exhaustively over a size matrix with no Screen in the process.
//
// One Layout value is computed per frame, the discipline all three other games
// keep. There is no cell_at: Tetris takes no mouse input, so nothing hit-tests.

#include <termgame/games/tetris/board.hpp>  // kCols/kVisibleRows — still no termforge

namespace termgame::tetris {

// A cell is TWO columns wide, for Snake's reason: a terminal cell is roughly
// twice as tall as it is wide, so a one-column playfield cell makes the well a
// tall thin slot and makes a piece's horizontal motion look like half its
// vertical motion on a board where they are the same speed.
inline constexpr int kCellCols = 2;

// The well: 10 cells wide plus its frame.
inline constexpr int kWellCols = (kCellCols * kCols) + 2;
inline constexpr int kWellRows = kVisibleRows + 2;

// The side panel carries HOLD, NEXT x3, and the score block. A preview box is
// four cells wide (I is the widest piece) plus a frame, so 4*2 + 2 = 10; the
// score block's longest line is "lines 000" at 9. Ten interior columns plus a
// frame is 12, and one blank column separates it from the well.
inline constexpr int kPanelCols = 12;
inline constexpr int kPanelGap = 1;

// Status row above, hint row below — Snake's chrome exactly.
inline constexpr int kChromeRows = 2;

inline constexpr int kNeedCols = kWellCols + kPanelGap + kPanelCols;
inline constexpr int kNeedRows = kWellRows + kChromeRows;

// ⚠ 24 ROWS IS FOUR MORE THAN ANY OTHER GAME IN THE SUITE. Minesweeper Hard is
// 63x20, 2048 is 29x19, Snake is 58x20 — all of them fit in twenty rows, and
// this one cannot, because a Tetris well is twenty cells tall before any chrome
// at all. It fits the classic 80x24 exactly, with zero rows to spare, which is
// also the default size the UI test probes use. Any chrome beyond these two
// rows and the game stops fitting the terminal most people still have.
//
// The Shell's floor stays 20x8, so the selector will happily launch this on a
// terminal that cannot draw it — gitea #15 for the fourth time. The answer is
// the same as the other three games': a game-owned "does not fit" screen, which
// is why `fits` is a field here rather than an assert.
static_assert(kNeedCols == 35);
static_assert(kNeedRows == 24);

struct Layout {
  bool fits{false};
  int well_x{0};  // frame origin
  int well_y{0};
  int panel_x{0};
  int panel_y{0};
  int origin_x{0};  // interior x of cell (0, y)'s first column
  int origin_y{0};  // interior y of visible row 0
  int status_y{0};
  int hint_y{0};

  [[nodiscard]] constexpr auto cell_x(int col) const noexcept -> int {
    return origin_x + (kCellCols * col);
  }
  // Takes a BOARD row, hidden rows included, and returns where it lands on
  // screen. Rows above kHiddenRows are off the top of the well and must not be
  // drawn; the caller checks, because a clamp here would silently paint a
  // spawning piece over the top border.
  [[nodiscard]] constexpr auto cell_y(int row) const noexcept -> int {
    return origin_y + (row - kHiddenRows);
  }
  [[nodiscard]] constexpr auto row_visible(int row) const noexcept -> bool {
    return row >= kHiddenRows && row < kRows;
  }
};

// screen_cols/screen_rows are the whole Screen: a running game owns all of it.
[[nodiscard]] constexpr auto compute_layout(int screen_cols,
                                            int screen_rows) noexcept -> Layout {
  Layout out;
  // Status row first and hint row last whether or not the well fits — a player
  // on a too-small terminal still needs to be told what is wrong and which keys
  // change it.
  out.status_y = 0;
  out.hint_y = screen_rows - 1;

  out.fits = screen_cols >= kNeedCols && screen_rows >= kNeedRows;
  if (!out.fits) return out;

  const int block_w = kNeedCols;
  const int left = (screen_cols - block_w) / 2;
  out.well_x = left;
  out.panel_x = left + kWellCols + kPanelGap;

  // Centred in the band between the status row and the hint row.
  const int band_top = 1;
  const int band_h = screen_rows - kChromeRows;
  out.well_y = band_top + ((band_h - kWellRows) / 2);
  out.panel_y = out.well_y;

  out.origin_x = out.well_x + 1;
  out.origin_y = out.well_y + 1;
  return out;
}

}  // namespace termgame::tetris
