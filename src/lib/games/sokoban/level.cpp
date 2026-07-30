// term-game — Sokoban: the level parser.

#include <termgame/games/sokoban/level.hpp>

#include <algorithm>
#include <cstddef>

namespace termgame::sokoban {

auto describe(ParseError e) noexcept -> std::string_view {
  // ⚠ 7-bit ASCII, fixed literals. These reach the screen on the no-colour
  // tier, which test/11selector sweeps cell by cell for any byte >= 0x80 — the
  // same constraint scores.hpp's diagnostics carry, and for the same reason.
  switch (e) {
    case ParseError::Empty: return "level is empty";
    case ParseError::TooLarge: return "level is too large";
    case ParseError::BadChar: return "level has an unknown character";
    case ParseError::NoPlayer: return "level has no player";
    case ParseError::ManyPlayers: return "level has more than one player";
    case ParseError::NoGoals: return "level has no goals";
    case ParseError::CountMismatch: return "level has boxes and goals unequal";
  }
  return "level is malformed";
}

auto parse(std::span<const std::string_view> rows, std::string_view name,
           int par) -> std::expected<Level, ParseError> {
  if (rows.empty()) return std::unexpected(ParseError::Empty);

  int width = 0;
  for (const auto& r : rows) {
    width = std::max(width, static_cast<int>(r.size()));
  }
  const int height = static_cast<int>(rows.size());
  if (width == 0) return std::unexpected(ParseError::Empty);
  if (width > kMaxCols || height > kMaxRows) {
    return std::unexpected(ParseError::TooLarge);
  }

  Level lv;
  lv.w = width;
  lv.h = height;
  lv.name = name;
  lv.par = par;
  // Padded to a rectangle here, once, rather than bounds-checked at every read
  // for the rest of the level's life. See the note on Level::at().
  lv.terrain.assign(static_cast<std::size_t>(width) *
                        static_cast<std::size_t>(height),
                    Terrain::Floor);

  int goals = 0;
  int players = 0;

  for (int y = 0; y < height; ++y) {
    const std::string_view row = rows[static_cast<std::size_t>(y)];
    for (int x = 0; x < width; ++x) {
      // Short rows pad with floor, not with wall: a trimmed trailing space in a
      // published level is floor that happens to be outside the wall ring, and
      // making it wall would silently change the puzzle.
      const char ch =
          x < static_cast<int>(row.size()) ? row[static_cast<std::size_t>(x)]
                                           : kChFloor;
      const auto i = static_cast<std::size_t>((y * width) + x);

      switch (ch) {
        case kChWall:
          lv.terrain[i] = Terrain::Wall;
          break;
        case kChFloor:
          break;
        case kChGoal:
          lv.terrain[i] = Terrain::Goal;
          ++goals;
          break;
        case kChBox:
          lv.boxes.push_back({x, y});
          break;
        case kChBoxOnGoal:
          lv.terrain[i] = Terrain::Goal;
          ++goals;
          lv.boxes.push_back({x, y});
          break;
        case kChPlayer:
          lv.player = {x, y};
          ++players;
          break;
        case kChPlayerOnGoal:
          lv.terrain[i] = Terrain::Goal;
          ++goals;
          lv.player = {x, y};
          ++players;
          break;
        default:
          return std::unexpected(ParseError::BadChar);
      }
    }
  }

  if (players == 0) return std::unexpected(ParseError::NoPlayer);
  if (players > 1) return std::unexpected(ParseError::ManyPlayers);
  if (goals == 0) return std::unexpected(ParseError::NoGoals);
  if (static_cast<int>(lv.boxes.size()) != goals) {
    return std::unexpected(ParseError::CountMismatch);
  }

  return lv;
}

}  // namespace termgame::sokoban
