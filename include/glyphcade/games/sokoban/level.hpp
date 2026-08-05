#pragma once

// glyphcade — Sokoban: the level format, and the parser that is the whole
// reason the format was chosen.
//
// ⚠ NO TERMFORGE HEADER, and none may be added. Like snake/board.hpp and
// tetris/board.hpp, this file names no terminal type, which is what lets
// test/31sokoban parse and validate all twenty bundled levels with no Screen
// anywhere in the process.
//
// ── Why the standard charset ─────────────────────────────────────────────────
//
// `#` `@` `$` `.` `*` `+` and space is the encoding every Sokoban level pack
// published since the 1980s uses, and term-game#8 asks for it by name so the
// existing corpus loads directly. Nothing here is invented: the reference's
// js/levels.js uses the same seven characters, and so does every .sok file.
//
// ── Why terrain and occupant are SEPARATE, when the charset does not separate
//    them ──────────────────────────────────────────────────────────────────
//
// The charset overloads one character with two facts: `*` means "there is a
// goal here" AND "there is a box here", and `+` says the same about the player.
// The reference keeps the grid in exactly that form and mutates it in place
// (game.js:91, and every assignment in move()), which is why its move() needs
// six conditional rewrites to push one box, each of them re-deriving the
// terrain underneath from the character that was there before.
//
// Terrain never changes during play; occupants are the only thing that moves.
// Splitting them makes a push two position updates and no character algebra at
// all, and it makes "which cells are goals" a question with one answer rather
// than one per frame.

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace glyphcade::sokoban {

// The standard Sokoban character set. These are the file format, not a display
// choice — what the player actually sees is glyphs.hpp's business.
inline constexpr char kChWall = '#';
inline constexpr char kChFloor = ' ';
inline constexpr char kChGoal = '.';
inline constexpr char kChBox = '$';
inline constexpr char kChPlayer = '@';
inline constexpr char kChBoxOnGoal = '*';
inline constexpr char kChPlayerOnGoal = '+';

// What a cell IS. Never changes once parsed.
enum class Terrain : std::uint8_t { Floor, Wall, Goal };

struct Pos {
  int x{0};
  int y{0};

  [[nodiscard]] friend constexpr auto operator==(Pos, Pos) noexcept
      -> bool = default;
};

// Every way a level can be malformed, as a value rather than an exception.
//
// ⚠ The reference validates NONE of these. Its checkWin() (game.js:251-256)
// scans only for a remaining `$`, so a level with more goals than boxes is won
// the moment the boxes it does have are seated, with empty goals still on
// screen; and a level authored with every box already on a goal can never be
// won at all, because checkWin() is unreachable except from move()
// (game.js:209). Both are level-data bugs the parser can refuse outright, so
// it does.
enum class ParseError : std::uint8_t {
  Empty,           // no rows, or no non-blank row
  TooLarge,        // beyond kMaxCols/kMaxRows
  BadChar,         // something outside the seven-character set
  NoPlayer,        // the reference silently takes the LAST of several
  ManyPlayers,     // (game.js:97-104) rather than saying anything
  NoGoals,         // a level with nothing to solve
  CountMismatch,   // boxes != goals, so "all boxes seated" is not "solved"
};

[[nodiscard]] auto describe(ParseError e) noexcept -> std::string_view;

// A level as parsed: terrain, the starting occupants, and the metadata the
// selector-side UI shows. Owning and copyable — a Board takes one by value and
// keeps it for reset().
struct Level {
  int w{0};
  int h{0};
  std::vector<Terrain> terrain;  // row-major, w*h
  std::vector<Pos> boxes;        // starting box positions
  Pos player{};                  // starting player position
  std::string_view name;         // from the pack; not parsed from the map
  int par{0};                    // optimal move count — see levels.hpp

  [[nodiscard]] auto at(int x, int y) const noexcept -> Terrain {
    if (x < 0 || y < 0 || x >= w || y >= h) return Terrain::Wall;
    return terrain[static_cast<std::size_t>((y * w) + x)];
  }

  // Off-grid reads answer Wall, which is what makes every rule in board.cpp a
  // plain lookup with no bounds test in front of it. ⚠ This is the fix for the
  // reference's one genuine movement bug: isValid() bounds columns with
  // board[0].length (game.js:213) while render() iterates board[r].length
  // (:124), so on a ragged level — which is what the published corpus looks
  // like, because trailing spaces get trimmed — a short row yields `undefined`,
  // `undefined !== WALL` passes, and the player walks out of the level. We pad
  // to a rectangle at parse time AND answer Wall off-grid, so neither half of
  // that can happen.
  [[nodiscard]] auto is_goal(int x, int y) const noexcept -> bool {
    return at(x, y) == Terrain::Goal;
  }
  [[nodiscard]] auto is_wall(int x, int y) const noexcept -> bool {
    return at(x, y) == Terrain::Wall;
  }
};

// Generous, and only here so a corrupt or hostile pack cannot ask for an
// enormous allocation. The published corpus tops out well inside this; the
// twenty bundled levels are at most 12x11.
inline constexpr int kMaxCols = 200;
inline constexpr int kMaxRows = 200;

// Total. Never throws, never asserts, and returns the FIRST problem it finds.
//
// Rows are padded to the widest row, so the Level is always rectangular even
// when the input is not.
[[nodiscard]] auto parse(std::span<const std::string_view> rows,
                         std::string_view name = {}, int par = 0)
    -> std::expected<Level, ParseError>;

}  // namespace glyphcade::sokoban
