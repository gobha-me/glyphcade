#pragma once

// 2048's geometry: integer arithmetic, header-only, no termforge.
//
// Same contract as games/minesweeper/layout.hpp, and the same reason it is its
// own file: ONE Layout value is computed per frame and consumed by BOTH draw()
// and on_event(), so drawing and hit-testing cannot drift apart. If a coordinate
// is derived twice, the two derivations disagree eventually.
//
// The tween needs one thing more than minesweeper did — a *fractional* position
// between two cells — so tile_x/tile_y take a double. tile_x(2) and
// tile_x(2.0) are the same column by construction, which is what keeps a
// finished animation landing exactly on the resting grid.

#include <cmath>
#include <optional>

#include <termgame/games/twenty48/board.hpp>

namespace termgame::twenty48 {

// A tile is 6 columns by 3 rows with a one-cell gap.
//
// 6 columns is not a look-nice number: kMaxTile is 131072, six digits, so a
// narrower tile could not draw the widest legal board. glyphs.hpp asserts that.
//
// ⚠ THAT IS A FLOOR, AND THIS IS WHERE A READER LOOKS FOR A CEILING — so here
// is the other half, which gitea #42 asked for. Nothing caps kTileCols. A wider
// tile is a legal change: under the suite's rule (AGENTS.md) the board extent
// is the game and may not move, while the CELL is presentation and may. 2048 is
// 4x4 and stays 4x4 (board.hpp says why); the six columns each of those tiles
// gets are a minimum with no maximum beside it. Nothing wants a larger one
// today, so nothing derives one — but a future reader wondering whether they
// are allowed to has an answer here rather than an argument in the other
// direction.
//
// 3 rows puts the number on a middle row with a blank row above and below, which
// is what makes a colour-filled tile read as a block rather than a line. It also
// gives the vertical tween 4 rows of travel per cell instead of 2.
inline constexpr int kTileCols = 6;
inline constexpr int kTileRows = 3;
inline constexpr int kGap = 1;

// Frame border (2) + the grid + status row (1) + hint row (1).
inline constexpr int kChromeCols = 2;
inline constexpr int kChromeRows = 4;

[[nodiscard]] constexpr auto grid_cols() noexcept -> int {
  return kSize * kTileCols + (kSize - 1) * kGap;
}
[[nodiscard]] constexpr auto grid_rows() noexcept -> int {
  return kSize * kTileRows + (kSize - 1) * kGap;
}

// 29x19. Above the Shell's 20x8 floor, so the selector can still launch a board
// this terminal cannot draw. Since gitea #15 it at least SAYS so first — kMeta
// declares this pair as its geometry and the menu warns — but the warning is
// not a refusal, so the in-game too-small screen below is still the thing that
// catches it, exactly as before.
[[nodiscard]] constexpr auto needed_cols() noexcept -> int {
  return grid_cols() + kChromeCols;
}
[[nodiscard]] constexpr auto needed_rows() noexcept -> int {
  return grid_rows() + kChromeRows;
}

struct Layout {
  bool fits{false};

  int frame_x{0};
  int frame_y{0};
  int frame_w{0};
  int frame_h{0};

  // Interior origin: the top-left cell of tile (0,0).
  int origin_x{0};
  int origin_y{0};

  int status_y{0};
  int hint_y{0};

  // Integer tile origins. The `double` overloads are the tween's entry point;
  // they round rather than truncate so a tile crossing a cell boundary does not
  // linger a frame longer on the low side.
  [[nodiscard]] constexpr auto tile_x(int col) const noexcept -> int {
    return origin_x + col * (kTileCols + kGap);
  }
  [[nodiscard]] constexpr auto tile_y(int row) const noexcept -> int {
    return origin_y + row * (kTileRows + kGap);
  }
  [[nodiscard]] auto tile_x(double col) const noexcept -> int {
    return origin_x +
           static_cast<int>(std::lround(col * (kTileCols + kGap)));
  }
  [[nodiscard]] auto tile_y(double row) const noexcept -> int {
    return origin_y +
           static_cast<int>(std::lround(row * (kTileRows + kGap)));
  }

  // The inverse of tile_x/tile_y, for hit-testing. Returns nullopt for the
  // border, the gaps, the status row and the hint row — anywhere that is not a
  // tile. Round-tripped over every cell at every size by test/23twenty48-ui.
  [[nodiscard]] constexpr auto cell_at(int x, int y) const noexcept
      -> std::optional<Coord> {
    if (!fits) {
      return std::nullopt;
    }
    const int dx = x - origin_x;
    const int dy = y - origin_y;
    if (dx < 0 || dy < 0) {
      return std::nullopt;
    }

    const int col = dx / (kTileCols + kGap);
    const int row = dy / (kTileRows + kGap);
    if (col >= kSize || row >= kSize) {
      return std::nullopt;
    }
    // Landing in a gap is not landing on a tile. Without these two checks the
    // gap column would report the tile to its left, and a click between tiles
    // would act on one of them.
    if (dx % (kTileCols + kGap) >= kTileCols) {
      return std::nullopt;
    }
    if (dy % (kTileRows + kGap) >= kTileRows) {
      return std::nullopt;
    }
    return Coord{row, col};
  }
};

// Centres the board in the screen. status_y and hint_y are set even when the
// board does NOT fit, so the too-small screen can still show its message and the
// hint line — same policy as minesweeper's compute_layout.
[[nodiscard]] constexpr auto compute_layout(int screen_cols,
                                            int screen_rows) noexcept -> Layout {
  Layout out{};
  out.status_y = 0;
  out.hint_y = screen_rows > 0 ? screen_rows - 1 : 0;

  out.fits = screen_cols >= needed_cols() && screen_rows >= needed_rows();
  if (!out.fits) {
    return out;
  }

  out.frame_w = grid_cols() + kChromeCols;
  out.frame_h = grid_rows() + kChromeCols;  // border only; status/hint are outside
  out.frame_x = (screen_cols - out.frame_w) / 2;

  // Vertically: one row for status at the top, one for hints at the bottom.
  const int body_rows = screen_rows - 2;
  out.frame_y = 1 + (body_rows - out.frame_h) / 2;

  out.origin_x = out.frame_x + 1;
  out.origin_y = out.frame_y + 1;
  return out;
}

}  // namespace termgame::twenty48
