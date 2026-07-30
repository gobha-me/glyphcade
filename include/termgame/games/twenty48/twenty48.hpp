#pragma once

// 2048 as a termgame::Game.
//
// ── The only file here that knows a terminal exists ─────────────────────────
//
// board.hpp has the rules, anim.hpp the tween, layout.hpp the geometry, glyphs.hpp
// the tables — and none of them includes a termforge header. This file is where
// Screen, Event and GameContext appear, which is what makes the split load-bearing
// rather than decorative: test/22twenty48 exercises the rules and the tween with
// no terminal because it *cannot* reach one.
//
// Same four-plus-one shape as games/minesweeper/, with anim.hpp as the piece
// minesweeper had no need for.

#include <chrono>
#include <cstdint>
#include <span>

#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>
#include <termforge/widgets/frame.hpp>

#include <termgame/arcade/context.hpp>
#include <termgame/arcade/game.hpp>
#include <termgame/arcade/game_meta.hpp>
#include <termgame/games/twenty48/anim.hpp>
#include <termgame/games/twenty48/board.hpp>
#include <termgame/games/twenty48/layout.hpp>

namespace termgame {

class Twenty48 final : public Game {
 public:
  // ⚠ The slug is "2048" and the namespace is twenty48, and they disagree on
  // purpose. A slug is a user-visible stable id — it keys the menu and will key
  // the score file — and "2048" is what the game is called. A C++ namespace may
  // not start with a digit. Same class of deliberate split as term-game vs
  // termgame; recorded in AGENTS.md so nobody "fixes" it.
  //
  // The icon is U+1F522 INPUT SYMBOL FOR NUMBERS: one code point, no variation
  // selector, measures two columns and draws two. all_games.cpp static_asserts
  // that via icon_is_safe(), which is the check that catches the ⚒️ family.
  static constexpr GameMeta kMeta{
      .slug = "2048",
      .title = "2048",
      // ⚠ 7-bit ASCII only, enforced by a static_assert in all_games.cpp. The
      // selector prints this on the no-colour tier, which cannot render anything
      // else — the first draft had an em dash here and it reached a bare pty.
      .description =
          "Slide the board with the arrow keys. Tiles of equal value merge into "
          "their sum, and every move spawns a new tile. Reach 2048, then keep "
          "going as long as you can. One level of undo.",
      .tag = "Puzzle Sliding",
      .icon = "\U0001F522",
  };

  Twenty48();

  [[nodiscard]] auto meta() const -> const GameMeta& override { return kMeta; }
  auto start(GameContext& ctx) -> void override;
  auto tick(std::chrono::duration<double> dt) -> void override;
  auto on_event(const termforge::Event& ev) -> bool override;
  auto draw(termforge::Screen& screen) -> void override;
  [[nodiscard]] auto done() const -> bool override { return m_done; }

  // Test observability, the same set minesweeper exposes. The non-const board()
  // is for reading state a case is about to assert on; to INSTALL a board, use
  // load() below rather than board().load(), for the reason documented there.
  [[nodiscard]] auto ticks() const noexcept -> int { return m_ticks; }
  [[nodiscard]] auto elapsed() const noexcept -> std::chrono::duration<double> {
    return m_elapsed;
  }
  [[nodiscard]] auto board() const noexcept -> const twenty48::Board& {
    return m_board;
  }
  [[nodiscard]] auto board() noexcept -> twenty48::Board& { return m_board; }

  // Install a fixture board, keeping the animation in step with it.
  //
  // ⚠ Use THIS rather than board().load(). Because draw() renders only from the
  // Anim — deliberately, so there is no second path that could drift — loading
  // straight into the Board leaves the Anim holding the PREVIOUS position list,
  // and the next frame paints the old board. That is not hypothetical: it is how
  // this method came to exist, after a rendering case asserted on a tile it had
  // just installed and found the screen still showing the constructor's board.
  //
  // The invariant "the Anim reflects the Board" belongs to the Game, which is why
  // the seam does too.
  auto load(std::span<const int> cells, int score = 0) -> void;
  [[nodiscard]] auto anim() const noexcept -> const twenty48::Anim& {
    return m_anim;
  }
  [[nodiscard]] auto layout() const noexcept -> const twenty48::Layout& {
    return m_layout;
  }

  // Drives a move the way a key would, including the snap-the-animation rule, so
  // a test can assert the input policy without synthesising key events.
  auto apply(twenty48::Dir d) -> bool;

  // Re-seeds and restarts. Public because `n` is bound to it and because a test
  // wants a known board without going through the constructor's clock read.
  auto new_game() -> void;

 private:
  auto handle_key(const termforge::KeyEvent& key) -> bool;

  // Sound is a function of the board's state transition, never of a return flag
  // — see the long note at the definition.
  auto announce(twenty48::State before, int moves_before, int score_before)
      -> void;

  // Persist the two records this game keeps. Called after every applied move,
  // which is safe and sufficient because Store::record() is monotone — see the
  // note at the definition for why undo does not need a guard here.
  auto record_best() -> void;

  // The stored best score, or 0 when there is none. Zero is the honest identity
  // for a Higher record and also a real minimum score, so the status row needs
  // no "unset" spelling — unlike minesweeper's best TIME, where 0 would be an
  // unbeatable lie.
  [[nodiscard]] auto best_score() const -> int;

  auto draw_status(termforge::Screen& screen) -> void;
  auto draw_hints(termforge::Screen& screen) -> void;
  auto draw_grid(termforge::Screen& screen) -> void;
  auto draw_tile(termforge::Screen& screen, const twenty48::DrawTile& tile,
                 bool ascii) -> void;
  auto draw_too_small(termforge::Screen& screen) -> void;

  GameContext* m_ctx{nullptr};
  twenty48::Board m_board;
  twenty48::Anim m_anim;
  twenty48::Layout m_layout{};
  std::uint64_t m_seed;
  bool m_done{false};
  int m_ticks{0};
  std::chrono::duration<double> m_elapsed{0.0};
  termforge::Frame m_frame{"2048"};
};

}  // namespace termgame
