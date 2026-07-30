#pragma once

// Snake as a termgame::Game.
//
// ── The only file here that knows a terminal exists ─────────────────────────
//
// board.hpp has the rules and the step clock, layout.hpp the geometry,
// glyphs.hpp the tables — and none of them includes a termforge header. This
// file is where Screen, Event and GameContext appear, which is what makes the
// split load-bearing rather than decorative: test/25snake exercises the rules
// with no terminal because it *cannot* reach one.
//
// Minesweeper's four-file shape exactly. There is no anim.hpp, and that is a
// decision rather than an omission: 2048 needed one because sliding IS the
// mechanic, whereas Snake occupies whole cells and a sub-cell tween in a
// character grid is a feel question with no reference behind it to answer it.
//
// ── What this file does NOT do ──────────────────────────────────────────────
//
// It does not implement pause, and it does not implement quit-to-menu. The Shell
// binds 'p' and Escape on anything a game declines, so gitea #6's "pause" scope
// item is satisfied by NOT writing it — which is exactly the property Epic 1
// built the Shell to have.

#include <chrono>
#include <cstdint>
#include <span>

#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>
#include <termforge/widgets/frame.hpp>

#include <termgame/arcade/context.hpp>
#include <termgame/arcade/game.hpp>
#include <termgame/arcade/game_meta.hpp>
#include <termgame/games/snake/board.hpp>
#include <termgame/games/snake/layout.hpp>

namespace termgame {

class Snake final : public Game {
 public:
  // Slug and namespace agree here, unlike 2048 — "snake" is a legal C++
  // identifier, so the deliberate split that file documents does not arise.
  //
  // The icon is U+1F40D SNAKE: one code point, no variation selector, measures
  // two columns and draws two. all_games.cpp static_asserts that via
  // icon_is_safe(), which is the check that catches the variation-selector
  // family.
  static constexpr GameMeta kMeta{
      .slug = "snake",
      .title = "Snake",
      // ⚠ 7-bit ASCII only, enforced by a static_assert in all_games.cpp. The
      // selector prints this on the no-colour tier, which by definition cannot
      // render anything else — 2048's first draft had an em dash here and it
      // reached a bare pty.
      .description =
          "Steer the snake with the arrow keys. Every piece of food makes it "
          "longer and the whole game faster, and the speed curve is the point. "
          "Three difficulties, and walls that either kill you or wrap you "
          "around to the far side.",
      .tag = "Arcade Classic",
      .icon = "\U0001F40D",
  };

  Snake();

  [[nodiscard]] auto meta() const -> const GameMeta& override { return kMeta; }
  auto start(GameContext& ctx) -> void override;
  auto tick(std::chrono::duration<double> dt) -> void override;
  auto on_event(const termforge::Event& ev) -> bool override;
  auto draw(termforge::Screen& screen) -> void override;

  // Test observability, the same set minesweeper and 2048 expose.
  [[nodiscard]] auto board() const noexcept -> const snake::Board& {
    return m_board;
  }
  [[nodiscard]] auto board() noexcept -> snake::Board& { return m_board; }
  [[nodiscard]] auto layout() const noexcept -> const snake::Layout& {
    return m_layout;
  }
  [[nodiscard]] auto ticks() const noexcept -> int { return m_ticks; }

  // Re-seeds and restarts at the given settings. Public because 1/2/3, 'm' and
  // 'n' are bound to it and because a case wants a known board without going
  // through the constructor's clock read.
  auto new_game(snake::Level level, snake::Walls walls) -> void;

  // Drives a turn the way a key would, so a case can assert the input policy
  // without synthesising key events. Returns what Board::turn() returned.
  auto steer(snake::Dir d) -> bool;

 private:
  auto handle_key(const termforge::KeyEvent& key) -> bool;

  // Sound is a function of what the tick actually did. Unlike minesweeper and
  // 2048 this reads a TickResult rather than comparing board state across a
  // verb, because here the verb is time: a single tick may contain no steps, or
  // several, and "compare before and after" cannot tell those apart.
  auto announce(const snake::TickResult& r) -> void;

  auto record_best() -> void;
  [[nodiscard]] auto best_score() const -> int;

  auto draw_status(termforge::Screen& screen) -> void;
  auto draw_hints(termforge::Screen& screen) -> void;
  auto draw_field(termforge::Screen& screen) -> void;
  auto draw_too_small(termforge::Screen& screen) -> void;

  GameContext* m_ctx{nullptr};
  snake::Board m_board;
  snake::Layout m_layout{};
  std::uint64_t m_seed;
  int m_ticks{0};
  termforge::Frame m_frame{"Snake"};
};

}  // namespace termgame
