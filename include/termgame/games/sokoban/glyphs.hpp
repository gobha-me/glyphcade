#pragma once

// term-game — Sokoban: the two rendering tiers.
//
// Same shape as snake/glyphs.hpp and tetris/glyphs.hpp — a table per tier, a
// selector, and three static_asserts that make the mistakes here compile
// errors. It names termforge only for Rgb and the width helper, exactly as the
// other three do; the tables themselves become a termforge::TileSet in
// sokoban.cpp, which is the only file that knows MapWidget exists.
//
// ── Why player-on-goal is its own glyph ─────────────────────────────────────
//
// Because in the reference it is invisible. Its `.cell.target::after` and
// `.cell.player::after` rules (style.css:248 and :311) decorate the SAME
// pseudo-element with equal specificity, so source order wins and the player
// silently erases the goal marker they are standing on. In a game whose whole
// activity is lining crates up with goals, "which square am I on" is not
// decoration.
//
// The distinctness static_assert below is what stops that happening here: two
// live glyphs that are equal do not render ambiguously, they fail to build.

#include <cstdint>
#include <string_view>

#include <termforge/core/types.hpp>
#include <termforge/widgets/detail/width.hpp>

#include <termgame/games/sokoban/layout.hpp>  // kTileCols — and still no widget

namespace termgame::sokoban {

// ⚠ Tile ids start at ONE, not zero. termforge's MapWidget reserves id 0 as
// "empty" — the value that lets a lower layer show through — and it is a
// private constant (kEmptyId in map_widget.hpp), so "0 means transparent" is a
// convention a consumer must simply know. Numbering an enum from 0 here would
// make Floor invisible on the terrain layer and every entity punch a hole
// through to the background. Reported upstream; see the note in sokoban.cpp.
enum class Tile : int {
  None = 0,
  Floor = 1,
  Wall,
  Goal,
  Box,
  BoxOnGoal,
  Player,
  PlayerOnGoal,
  DeadBox,
};

inline constexpr int kTileCount = 9;  // None plus the eight drawn ids

struct TileGlyphs {
  std::string_view floor;
  std::string_view wall;
  std::string_view goal;
  std::string_view box;
  std::string_view box_on_goal;
  std::string_view player;
  std::string_view player_on_goal;
  std::string_view dead_box;
};

// The bottom tier is the standard Sokoban charset, doubled to fill a two-column
// tile. That is not whimsy: a player who has ever seen a .sok file can read the
// screen without a legend, and every character is 7-bit by construction.
inline constexpr TileGlyphs kAsciiTiles{
    .floor = "  ",
    .wall = "##",
    .goal = "..",
    .box = "$$",
    .box_on_goal = "**",
    .player = "@@",
    .player_on_goal = "++",
    .dead_box = "XX",
};

inline constexpr TileGlyphs kUnicodeTiles{
    .floor = "  ",
    .wall = "██",              // FULL BLOCK
    .goal = "▫▫",              // WHITE SMALL SQUARE
    .box = "▓▓",               // DARK SHADE
    .box_on_goal = "▩▩",       // SQUARE WITH DIAGONAL CROSSHATCH
    .player = "◆◆",            // BLACK DIAMOND
    .player_on_goal = "◈◈",    // WHITE DIAMOND CONTAINING BLACK
    .dead_box = "▚▚",          // QUADRANT UPPER LEFT AND LOWER RIGHT
};

[[nodiscard]] constexpr auto tiles_for(bool ascii) noexcept
    -> const TileGlyphs& {
  return ascii ? kAsciiTiles : kUnicodeTiles;
}

[[nodiscard]] constexpr auto glyph_for(const TileGlyphs& g, Tile t) noexcept
    -> std::string_view {
  switch (t) {
    case Tile::None: return {};
    case Tile::Floor: return g.floor;
    case Tile::Wall: return g.wall;
    case Tile::Goal: return g.goal;
    case Tile::Box: return g.box;
    case Tile::BoxOnGoal: return g.box_on_goal;
    case Tile::Player: return g.player;
    case Tile::PlayerOnGoal: return g.player_on_goal;
    case Tile::DeadBox: return g.dead_box;
  }
  return {};
}

// Colour tier only. FallbackDriver discards colour outright, which is why the
// glyph tables above carry all the information on their own.
[[nodiscard]] constexpr auto colour_for(Tile t) noexcept -> termforge::Rgb {
  switch (t) {
    case Tile::Wall: return {0x8A, 0x8A, 0x92};
    case Tile::Goal: return {0xE0, 0xA8, 0x30};
    case Tile::Box: return {0xC0, 0x8A, 0x4A};
    case Tile::BoxOnGoal: return {0x60, 0xC0, 0x60};
    case Tile::Player: return {0x60, 0xC8, 0xE0};
    case Tile::PlayerOnGoal: return {0x90, 0xE0, 0xF0};
    case Tile::DeadBox: return {0xD0, 0x50, 0x50};
    case Tile::Floor:
    case Tile::None:
      break;
  }
  return {0x50, 0x50, 0x58};
}

// ── The three checks ───────────────────────────────────────────────────────

constexpr auto ascii_is_seven_bit() noexcept -> bool {
  const std::string_view all[] = {
      kAsciiTiles.floor,       kAsciiTiles.wall,           kAsciiTiles.goal,
      kAsciiTiles.box,         kAsciiTiles.box_on_goal,    kAsciiTiles.player,
      kAsciiTiles.player_on_goal, kAsciiTiles.dead_box,
  };
  for (const auto s : all) {
    for (const char c : s) {
      if (static_cast<unsigned char>(c) >= 0x80) return false;
    }
  }
  return true;
}
static_assert(ascii_is_seven_bit(),
              "the ASCII tile tier must be 7-bit; it is the only thing a "
              "no-colour terminal can draw");

constexpr auto every_glyph_is_one_tile_wide() noexcept -> bool {
  const TileGlyphs* tiers[] = {&kAsciiTiles, &kUnicodeTiles};
  for (const auto* g : tiers) {
    const std::string_view all[] = {
        g->floor, g->wall,   g->goal,           g->box,
        g->box_on_goal, g->player, g->player_on_goal, g->dead_box,
    };
    for (const auto s : all) {
      if (termforge::detail::display_width(s) != kTileCols) return false;
    }
  }
  return true;
}
// ⚠ A glyph measured narrower than the tile leaves a background column inside
// the tile; a glyph measured wider spills into the tile to its right and every
// cell after it on that row is off by one for the rest of the frame.
static_assert(every_glyph_is_one_tile_wide(),
              "every tile glyph must measure exactly kTileCols columns");

constexpr auto live_glyphs_are_distinct() noexcept -> bool {
  const TileGlyphs* tiers[] = {&kAsciiTiles, &kUnicodeTiles};
  for (const auto* g : tiers) {
    // Floor is excluded: it is blank on purpose, and it is the one tile whose
    // meaning is "nothing to see".
    const std::string_view live[] = {
        g->wall, g->goal, g->box, g->box_on_goal,
        g->player, g->player_on_goal, g->dead_box,
    };
    for (std::size_t i = 0; i < std::size(live); ++i) {
      for (std::size_t j = i + 1; j < std::size(live); ++j) {
        if (live[i] == live[j]) return false;
      }
    }
  }
  return true;
}
// This is the one that would have caught the reference's invisible
// player-on-goal at compile time. See the header note.
static_assert(live_glyphs_are_distinct(),
              "two live tile glyphs are identical, so two different things "
              "render the same — see the player-on-goal note in this header");

}  // namespace termgame::sokoban
