#pragma once

// 2048's rules. Ported from HTML-Games' 2048/js/game.js, classic edition.
//
// ── This file includes no termforge header, and that is the point ────────────
//
// Same discipline as games/minesweeper/board.hpp: the rules cannot construct a
// Screen, so test/22twenty48 is *unable* to test them through a terminal rather
// than merely choosing not to. If a termforge include ever appears here, the
// model and the view have grown together and the test is no longer proving what
// it claims.
//
// ── The model is instantaneous. Animation is not a participant ───────────────
//
// AGENTS.md: "Game logic must not know about rendering. Animation is a
// presentation layer over already-resolved state." move() resolves the whole
// board immediately. What it *also* does is report the motion facts — which tile
// travelled from where to where — because that is the one thing a tween cannot
// reconstruct afterwards, and the reference throws it away.
//
// ⚠ Worth knowing before "simplifying" MoveResult: the reference has tile ids
// (game.js:32 issues them, the spread at :101 preserves them across a move) and
// NOTHING EVER READS THEM. render() rebuilds every DOM node from scratch, so
// identity dies at the view boundary and the CSS transition never fires. Our
// Motion list is that discarded information, made explicit — the reason
// anim.hpp can exist at all.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <termgame/arcade/rng.hpp>

namespace termgame::twenty48 {

// 4x4 and 2048 are the game's identity, not tuning knobs — a "5x5 2048" is a
// different game with a different difficulty curve. Named rather than literal so
// the layout and glyph tables can agree with the model at compile time.
inline constexpr int kSize = 4;
inline constexpr int kCells = kSize * kSize;
inline constexpr int kWinTile = 2048;

// The largest tile a kSize x kSize board can physically reach: fill every cell
// and merge everything, twice per cell. 2^(kCells+1) = 131072 on 4x4.
//
// glyphs.hpp asserts its label fits a tile, so this constant is what stops the
// widest possible number from being one column too wide to draw. It is not
// reachable in practice; it is reachable in principle, which is the only bar a
// rendering guarantee can be held to.
inline constexpr int kMaxTile = 131072;

struct Coord {
  int row{0};
  int col{0};
  auto operator==(const Coord&) const -> bool = default;
};

enum class Dir : std::uint8_t { Left, Right, Up, Down };

// For tests that sweep every direction. Same shape as minesweeper's kLevels.
inline constexpr Dir kDirs[]{Dir::Left, Dir::Right, Dir::Up, Dir::Down};

// Won is a LATCH, not a terminal state, and Lost can override it.
//
// The reference shows a win overlay that does not gate input (only `gameOver`
// does), so play continues behind it — a modal that lies about whether the game
// is over. Ours says the same thing without the lie: reaching 2048 latches Won,
// the game keeps accepting moves, and filling the board still reaches Lost.
// `finished()` is Lost alone.
enum class State : std::uint8_t { Playing, Won, Lost };

// One tile's journey through a single move.
//
// `value` is the tile's value BEFORE the move, which is what the tween draws
// while it slides. A merge emits TWO motions sharing one `to`; the destination's
// doubled value is in the board, not here, because the doubled tile does not
// exist until the slide finishes.
struct Motion {
  Coord from{};
  Coord to{};
  int value{0};
  bool merged{false};
};

struct Spawn {
  Coord at{};
  int value{0};
};

struct MoveResult {
  bool moved{false};
  int score_delta{0};
  int merges{0};
  std::vector<Motion> motions{};
  std::optional<Spawn> spawn{};
};

class Board {
 public:
  explicit Board(std::uint64_t seed);

  auto reset(std::uint64_t seed) -> void;

  // 0 means empty. Out of bounds reads 0 rather than being UB, same sentinel
  // policy as minesweeper's Board::at().
  [[nodiscard]] auto at(Coord p) const -> int;

  // Row-major, kCells long. This is what anim.hpp consumes, so the tween needs
  // no Board type of its own to talk about — and cannot reach the rules.
  [[nodiscard]] auto cells() const noexcept -> std::span<const int>;

  [[nodiscard]] auto state() const noexcept -> State { return m_state; }
  [[nodiscard]] auto finished() const noexcept -> bool {
    return m_state == State::Lost;
  }
  [[nodiscard]] auto score() const noexcept -> int { return m_score; }
  [[nodiscard]] auto best_tile() const noexcept -> int;
  [[nodiscard]] auto empty_count() const noexcept -> int;

  // The reference's moveCount was vestigial once the power-tile mechanic went
  // (its only reader was `moveCount % POWER_INTERVAL`). Kept because the status
  // line shows it and because it gives a test a cheap way to say "that gesture
  // was a real move".
  [[nodiscard]] auto moves() const noexcept -> int { return m_moves; }

  // "Is any move still legal": any empty cell, or any orthogonally adjacent
  // equal pair. Loss is !can_move(), evaluated after the spawn.
  [[nodiscard]] auto can_move() const noexcept -> bool;

  // Resolves the whole move, then spawns, then re-evaluates win and loss. A move
  // that changes nothing returns `moved == false`, spawns nothing, scores
  // nothing, and — see below — does NOT consume the undo.
  auto move(Dir d) -> MoveResult;

  [[nodiscard]] auto can_undo() const noexcept -> bool { return m_has_undo; }

  // One level, like the reference. Restores cells, score, state and the move
  // count; returns false when there is nothing to undo.
  //
  // ⚠ Two reference bugs deliberately not ported (game.js:112, :179, :307):
  // there, saveState() runs unconditionally at move() entry and an ILLEGAL move
  // then sets prevState = null — so making a valid move and then pressing a
  // direction that does nothing silently destroys your undo, while leaving the
  // button enabled. Here the snapshot is taken only when the move actually
  // changed something, so a no-op move cannot consume it. The reference also
  // force-sets gameOver = false on undo; we restore the recorded state instead,
  // which is right for the same reason.
  auto undo() -> bool;

  // Fixture seam, the analogue of minesweeper's load_mines(): drop in an exact
  // board so a test pins a RULE instead of the RNG. Row-major, kCells long,
  // 0 for empty. Lands in State::Playing with no undo available; win and loss
  // are then evaluated by the next move(), not by this call.
  auto load(std::span<const int> cells, int score = 0) -> void;

 private:
  // The travel order for one line: the kSize board indices of line `i` under
  // direction `d`, ordered so that index 0 is the destination edge. This is the
  // whole of the direction handling — the reference spends getRow/setRow plus
  // four reverse/transpose cases on it (game.js:118-138), and a mistake in any
  // one of them is a bug in exactly one direction.
  [[nodiscard]] static auto line(Dir d, int i) -> std::array<int, kSize>;

  [[nodiscard]] auto index(Coord p) const noexcept -> int;
  [[nodiscard]] static auto coord_of(int idx) noexcept -> Coord;

  auto spawn_tile() -> std::optional<Spawn>;
  auto snapshot() -> void;

  std::vector<int> m_cells;
  Rng m_rng;
  State m_state{State::Playing};
  int m_score{0};
  int m_moves{0};

  bool m_has_undo{false};
  std::vector<int> m_undo_cells;
  State m_undo_state{State::Playing};
  int m_undo_score{0};
  int m_undo_moves{0};
};

}  // namespace termgame::twenty48
