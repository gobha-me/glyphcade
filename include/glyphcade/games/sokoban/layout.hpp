#pragma once

// glyphcade — Sokoban: where things are on screen. Integers only.
//
// ⚠ NO TERMFORGE HEADER, like board.hpp and level.hpp — this takes a screen
// size as two ints and hands back coordinates, so test/31sokoban can sweep the
// geometry exhaustively over a size matrix with no Screen in the process.
//
// ── This is the first layout in the suite that does NOT size itself to the
//    board ──────────────────────────────────────────────────────────────────
//
// Minesweeper needs 63x20 because Hard is 30 cells wide; Snake needs 58x20
// because its field is 28x16; Tetris needs 35x24 because a well is twenty cells
// tall. In all three the required size is arithmetic: the board has one size,
// and the terminal either has room for it or does not.
//
// Sokoban has twenty boards of nine different sizes, and a CAMERA. A level
// larger than the window scrolls instead of being refused, so there is no size
// the game must have in order to be *drawn*. What is left is a floor on being
// *playable*: below some window you cannot see enough of a room to plan a push,
// and that is a judgement rather than a consequence.
//
// ⚠ That distinction is what gitea #15 was stuck on for six deferrals, and it
// is now RESOLVED rather than still outstanding — this comment used to end
// "#15 is still the wrong shape". A field called `min_cols` alone would have
// put Minesweeper's 63, a number you can derive, next to Sokoban's 34, which is
// an opinion, and invited the selector to treat them as the same kind of fact.
// So the kind travels with the number: `GameGeometry` carries a `SizeFloor`,
// this game is the roster's only `Playable`, and the selector prints it as
// "recommended" where every other game says "minimum". The objection was right
// and it is what shaped the field — see arcade/game_meta.hpp.

namespace glyphcade::sokoban {

// A tile is TWO columns wide and one row tall. Same argument snake/layout.hpp
// makes: a terminal cell is roughly 1:2, so a one-column tile renders a square
// room as a tall thin one. termforge's MapWidget names this case explicitly —
// tile size is declared in CELLS, and {2,1} is what a visually square tile
// wants.
inline constexpr int kTileCols = 2;
inline constexpr int kTileRows = 1;

// Frame border only.
inline constexpr int kChromeCols = 2;
// Frame border (2) + the status row (1) + the hint row (1).
inline constexpr int kChromeRows = 4;

// The playability floor. Sixteen tiles across and eight down is enough of a
// room to see a push and its consequence; below that the camera is scrolling
// faster than the player can read.
//
// ⚠ These are NOT derived from the level pack, and must not be "corrected" to
// fit it. The widest bundled level is 12x11 tiles (26x15 cells with chrome), so
// a floor that fitted every level would be larger than this and would refuse to
// draw levels the camera can perfectly well scroll.
inline constexpr int kMinTilesW = 16;
inline constexpr int kMinTilesH = 8;

inline constexpr int kNeedCols = (kTileCols * kMinTilesW) + kChromeCols;
inline constexpr int kNeedRows = (kTileRows * kMinTilesH) + kChromeRows;

static_assert(kNeedCols == 34);
static_assert(kNeedRows == 12);

struct Layout {
  bool fits{false};
  int frame_x{0};
  int frame_y{0};
  int frame_w{0};
  int frame_h{0};
  // The MapWidget's rect, in cells: the frame interior minus the two text rows.
  int view_x{0};
  int view_y{0};
  int view_w{0};
  int view_h{0};
  int status_y{0};
  int hint_y{0};

  // The window in TILES, floored — the same arithmetic MapWidget does
  // privately in viewport_tiles(). ⚠ We have to redo it because the widget does
  // not expose it, and a click cannot be turned into a tile without it. That
  // asymmetry is reported upstream; see the note in sokoban.cpp.
  [[nodiscard]] constexpr auto view_tiles_w() const noexcept -> int {
    return view_w / kTileCols;
  }
  [[nodiscard]] constexpr auto view_tiles_h() const noexcept -> int {
    return view_h / kTileRows;
  }
};

// One Layout per frame, the discipline every other game here keeps.
//
// ⚠ It takes the LEVEL SIZE, in tiles, which no other game's layout does — the
// other four have one board size compiled in. It is needed for one reason:
// MapWidget's camera clamps to the map, so when the window is larger than the
// level the camera pins to 0,0 and the level draws hard against the widget's
// top-left corner. Found on a real pty, not reasoned about: level 1 is 8x5
// tiles and every bundled level is smaller than a normal terminal, so this is
// the case that ALWAYS happens rather than an edge one.
//
// The fix is app-side and belongs here: shrink the widget's rect to what the
// level actually needs and centre THAT. Growing the camera's reach would be the
// alternative and it is wrong — there is nothing out there to look at.
[[nodiscard]] constexpr auto compute_layout(int cols, int rows, int map_w,
                                            int map_h) noexcept -> Layout {
  Layout l;
  l.fits = cols >= kNeedCols && rows >= kNeedRows;
  if (!l.fits) return l;

  // The frame takes the whole screen: more window is more room seen, up to the
  // level's own size.
  l.frame_x = 0;
  l.frame_y = 0;
  l.frame_w = cols;
  l.frame_h = rows;

  const int avail_w = cols - kChromeCols;
  const int avail_h = rows - kChromeRows;
  const int want_w = map_w * kTileCols;
  const int want_h = map_h * kTileRows;

  l.view_w = want_w > 0 && want_w < avail_w ? want_w : avail_w;
  l.view_h = want_h > 0 && want_h < avail_h ? want_h : avail_h;
  l.view_x = 1 + ((avail_w - l.view_w) / 2);
  l.view_y = 1 + ((avail_h - l.view_h) / 2);

  l.status_y = rows - 3;
  l.hint_y = rows - 2;
  return l;
}

}  // namespace glyphcade::sokoban
