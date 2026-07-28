#pragma once

// term-game — Minesweeper: the Game the Shell hosts.
//
// This is the only file in the game that knows Screen, Event or GameContext
// exist. board.hpp holds the rules, layout.hpp the geometry, glyphs.hpp the
// look; all three are termforge-free and separately testable. What is left here
// is the wiring: keys and clicks in, a repaint out.
//
// Two divergences from the HTML-Games reference are deliberate and documented
// at their implementation sites — do not "restore" either:
//
//  * Left-clicking a revealed number CHORDS. The reference binds chording to
//    auxclick only, and most trackpads have no middle button.
//  * There is a question-mark mark state. The reference has none.
//
// SFX are absent because Epic 2 does not exist yet (gitea #3); Board's verbs
// already return "did anything change", which is where they will bind. There is
// no high-score persistence because GameContext has no seam for it and STATUS.md
// defers that to the second scoring game (gitea #14).

#include <chrono>
#include <cstdint>

#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>
#include <termforge/widgets/frame.hpp>

#include <termgame/arcade/context.hpp>
#include <termgame/arcade/game.hpp>
#include <termgame/arcade/game_meta.hpp>
#include <termgame/games/minesweeper/board.hpp>
#include <termgame/games/minesweeper/layout.hpp>

namespace termgame {

class Minesweeper final : public Game {
 public:
  static constexpr GameMeta kMeta{
      .slug = "minesweeper",
      .title = "Minesweeper",
      .description =
          "Clear the minefield without detonating one. Numbers count the mines "
          "touching a square; flag what you are sure of and chord what you have "
          "already worked out. The first square you open is always safe.",
      .tag = "Puzzle Classic",
      // U+1F4A3 BOMB: a single code point inside termforge's wide table, with
      // no variation selector, so measured width and drawn width agree. The
      // registry static_asserts on this via icon_is_safe().
      .icon = "\U0001F4A3",
  };

  Minesweeper();

  [[nodiscard]] auto meta() const -> const GameMeta& override { return kMeta; }

  auto start(GameContext& ctx) -> void override;
  auto tick(std::chrono::duration<double> dt) -> void override;
  auto on_event(const termforge::Event& ev) -> bool override;
  auto draw(termforge::Screen& screen) -> void override;
  [[nodiscard]] auto done() const -> bool override { return m_done; }

  // ── Observables for the tests ────────────────────────────────────────────
  //
  // ⚠ m_ticks / m_elapsed count EVERY tick, unconditionally, and are NOT the
  // game clock. Board::advance() runs only while the timer is running, i.e.
  // after the first reveal. test/13tick enters this game and ticks WITHOUT ever
  // clicking, so if these two are ever "simplified" into one accumulator,
  // elapsed() reports zero for that whole test and every tick-routing assertion
  // silently stops meaning anything while staying green.
  [[nodiscard]] auto ticks() const noexcept -> int { return m_ticks; }
  [[nodiscard]] auto elapsed() const noexcept -> std::chrono::duration<double> {
    return m_elapsed;
  }
  [[nodiscard]] auto min_dt() const noexcept -> std::chrono::duration<double> {
    return m_min_dt;
  }

  [[nodiscard]] auto board() const noexcept -> const minesweeper::Board& {
    return m_board;
  }
  // Non-const, so a test can install an exact layout with load_mines() and then
  // drive the real input path over it. A game with a known board is the only
  // way to assert what a click does without asserting the RNG too.
  [[nodiscard]] auto board() noexcept -> minesweeper::Board& { return m_board; }
  [[nodiscard]] auto cursor() const noexcept -> minesweeper::Coord {
    return m_cursor;
  }
  [[nodiscard]] auto level() const noexcept -> minesweeper::Level {
    return m_level;
  }
  [[nodiscard]] auto layout() const noexcept -> const minesweeper::Layout& {
    return m_layout;
  }

 private:
  auto handle_key(const termforge::KeyEvent& key) -> bool;
  auto handle_mouse(const termforge::MouseEvent& mouse) -> bool;
  auto new_game(minesweeper::Level level) -> void;
  auto move_cursor(int dr, int dc) -> void;
  auto draw_status(termforge::Screen& screen) -> void;
  auto draw_hints(termforge::Screen& screen) -> void;
  auto draw_grid(termforge::Screen& screen) -> void;
  auto draw_too_small(termforge::Screen& screen) -> void;

  GameContext* m_ctx{nullptr};
  minesweeper::Level m_level{minesweeper::Level::Easy};
  minesweeper::Board m_board;
  minesweeper::Coord m_cursor{};
  minesweeper::Layout m_layout{};
  std::uint64_t m_seed;
  bool m_done{false};

  int m_ticks{0};
  std::chrono::duration<double> m_elapsed{0.0};
  std::chrono::duration<double> m_min_dt{1.0e9};

  termforge::Frame m_frame{"Minesweeper"};
};

}  // namespace termgame
