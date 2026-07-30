#pragma once

// term-game — Sokoban: the rules. No terminal, no clock, no randomness.
//
// ⚠ NO TERMFORGE HEADER. Same discipline as snake/board.hpp and
// tetris/board.hpp: test/31sokoban drives every rule here and *cannot* reach a
// Screen, because nothing in this translation unit knows one exists.
//
// ── The first game since Minesweeper with no clock at all ───────────────────
//
// Snake has one accumulator, 2048 has one tween, Tetris has five. Sokoban has
// none: nothing advances unless the player presses a key, so there is no
// tick(), no dt, and no interval to get wrong. That is worth stating rather
// than leaving as an absence, because the three games before this one all
// shipped a bug in exactly that arithmetic and the reviewer's instinct will be
// to look for it here.
//
// What replaces it as the thing most likely to be wrong is UNDO, and the answer
// is below.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <termgame/games/sokoban/level.hpp>

namespace termgame::sokoban {

enum class Dir : std::uint8_t { Up, Down, Left, Right };

[[nodiscard]] constexpr auto delta(Dir d) noexcept -> Pos {
  switch (d) {
    case Dir::Up: return {0, -1};
    case Dir::Down: return {0, 1};
    case Dir::Left: return {-1, 0};
    case Dir::Right: return {1, 0};
  }
  return {0, 0};
}

// What one attempted step actually did. The Game turns this into sound and
// into a status line; nothing here knows about either.
struct MoveResult {
  bool moved{false};    // the player changed square
  bool pushed{false};   // ...and a box came with them
  bool seated{false};   // ...and that box landed ON a goal
  bool unseated{false}; // ...or left one
  bool won{false};      // every box is on a goal, as of this move
};

// ⚠ UNDO IS A MOVE RECORD, NOT A BOARD COPY.
//
// The reference deep-copies the whole grid on every single move
// (game.js:216-224) and keeps them all, because its board and its entities are
// the same array — there is nothing smaller to record. Here a step is exactly
// invertible from three facts, because a push is reversible by construction:
// the player walks back the way they came, and if a box came with them it goes
// back to the square the player is standing on.
//
// That makes "unlimited undo" cost three bytes a move instead of a grid, which
// is the difference between a promise and a leak.
struct Move {
  Dir dir{Dir::Up};
  bool pushed{false};
};

class Board {
 public:
  // Takes the parsed level by value and keeps it: reset() needs the starting
  // occupants, and nothing else owns them.
  explicit Board(Level level);

  // Attempt one step. A blocked step returns a default MoveResult and records
  // nothing, so undo never replays a move that did not happen.
  auto step(Dir d) -> MoveResult;

  // Undo the most recent step. False when there is nothing to undo.
  //
  // ⚠ Available even after the level is solved, deliberately. The reference
  // disables undo AND reset the moment levelComplete goes true
  // (game.js:158-159, 227, 239) and only ever clears that flag inside
  // loadLevel() — so dismissing its celebration overlay by clicking the
  // backdrop (game.js:304-306) leaves a frozen board with both buttons greyed
  // out, and on the last level there is no Next to escape with either. A flag
  // set true on one path and cleared on another is the same defect this repo
  // has now met in three separate references.
  auto undo() -> bool;

  // Back to the level's starting position. Clears the history and the counters.
  auto reset() -> void;

  [[nodiscard]] auto level() const noexcept -> const Level& { return m_level; }
  [[nodiscard]] auto player() const noexcept -> Pos { return m_player; }
  [[nodiscard]] auto boxes() const noexcept -> const std::vector<Pos>& {
    return m_boxes;
  }
  [[nodiscard]] auto moves() const noexcept -> int { return m_moves; }
  [[nodiscard]] auto pushes() const noexcept -> int { return m_pushes; }
  [[nodiscard]] auto history_size() const noexcept -> std::size_t {
    return m_history.size();
  }

  // ⚠ Evaluated in the constructor as well as after every step. The reference
  // only ever calls checkWin() from move() (game.js:209), so a level authored
  // already solved can never be won there. Ours answers correctly at move zero.
  [[nodiscard]] auto won() const noexcept -> bool { return m_won; }

  [[nodiscard]] auto boxes_on_goals() const noexcept -> int;
  [[nodiscard]] auto has_box(int x, int y) const noexcept -> bool;

  // ── Deadlock ──────────────────────────────────────────────────────────────
  //
  // gitea #8 asks for this and the reference has NOTHING: push a box into a
  // corner and it lets you keep playing a level that can no longer be solved.
  //
  // ⚠ Scope, stated so it is not mistaken for something larger. This is NOT a
  // solvability oracle — that is PSPACE-complete and a game has no business
  // trying. It answers one decidable question: is some box that is not on a
  // goal FROZEN, i.e. unable to move on either axis, ever again, by anyone.
  //
  // A box is blocked on an axis when either neighbour along it is a wall, or is
  // another box that is itself frozen. That recursion is what catches a pair of
  // boxes braced against each other in the middle of a room, which the
  // corner-only test everyone writes first does not.
  //
  // Sound, not complete: every position it flags is genuinely lost, and it will
  // miss lost positions of other kinds. That is the right direction for the
  // error to go — a false alarm would be telling a player to give up on a level
  // they can still win.
  [[nodiscard]] auto deadlocked() const noexcept -> bool;
  [[nodiscard]] auto is_frozen(Pos box) const noexcept -> bool;

 private:
  [[nodiscard]] auto box_index(int x, int y) const noexcept -> std::size_t;
  [[nodiscard]] auto frozen(Pos box, std::vector<Pos>& assumed) const noexcept
      -> bool;
  [[nodiscard]] auto blocked_on_axis(Pos box, Pos axis,
                                     std::vector<Pos>& assumed) const noexcept
      -> bool;
  auto recompute_won() noexcept -> void;

  Level m_level;
  std::vector<Pos> m_boxes;
  Pos m_player{};
  std::vector<Move> m_history;
  int m_moves{0};
  int m_pushes{0};
  bool m_won{false};
};

// Sentinel for "not a box here". box_index() returns it rather than an
// optional, because every caller immediately branches on it anyway.
inline constexpr std::size_t kNoBox = static_cast<std::size_t>(-1);

}  // namespace termgame::sokoban
