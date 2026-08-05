#pragma once

// glyphcade — Tetris: the Game.
//
// board.hpp has the rules and all five clocks, layout.hpp the geometry,
// glyphs.hpp the tables — and none of them includes a termforge header. This
// file is where Screen, Event and GameContext appear, which is what makes the
// split load-bearing rather than decorative: test/27tetris exercises the rules,
// the gravity clock and DAS with no terminal because it *cannot* reach one.
//
// ── What this file does NOT do ───────────────────────────────────────────────
//
// It does not implement pause, and it does not implement quit-to-menu. The
// Shell binds 'p' and Escape on anything a game declines, so term-game#7's "pause"
// is satisfied by NOT writing it. Same as the other three games.
//
// ── This is the first game to ask for a keyboard tier ────────────────────────
//
// kMeta declares KeyboardMode::Enhanced (term-game#32). The Shell sets it on
// entry, restores Legacy on the way out, and raises an ErrorEvent{Info} when
// the terminal cannot deliver it. What THIS file does with that is read
// ctx.capabilities().kitty_keyboard once and hand the answer to the model as a
// HoldSupport, because the difference is a rule and not a rendering detail: you
// cannot reconstruct "held" from press-only input, and pretending otherwise is
// how you ship a Tetris that feels broken and says nothing.

#include <chrono>
#include <cstdint>
#include <string>

#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>
#include <termforge/widgets/frame.hpp>

#include <glyphcade/arcade/game.hpp>
#include <glyphcade/arcade/options_screen.hpp>
#include <glyphcade/games/tetris/board.hpp>
#include <glyphcade/games/tetris/glyphs.hpp>
#include <glyphcade/games/tetris/layout.hpp>

namespace glyphcade {

// ⚠ Namespace scope, not inline in kMeta. See arcade/game_meta.hpp.
inline constexpr OptionSpec kTetrisOptions[]{
    {.label = "Start level",
     .choices = tetris::kStartLevelNames,
     .default_index = 0},
};

class Tetris final : public Game {
 public:
  static constexpr GameMeta kMeta{
      .slug = "tetris",
      .title = "Tetris",
      // ⚠ 7-bit ASCII only, enforced by a static_assert in all_games.cpp. The
      // selector prints this on the no-colour tier, which by definition cannot
      // render anything else — 2048's first draft had an em dash here and it
      // reached a bare pty.
      .description =
          "Stack the falling tetrominoes and clear lines. Full SRS rotation "
          "with wall kicks, a seven-bag randomiser, hold, a three-piece "
          "preview and a ghost. Hold left or right to auto-shift, if your "
          "terminal can tell us the key came back up.",
      .tag = "Arcade Classic",
      .icon = "\U0001F9F1",
      // The first non-Legacy declaration in the repo. See the header note.
      .keyboard = termforge::KeyboardMode::Enhanced,
      // ⚠ AFTER .keyboard, because designated initialisers must follow
      // declaration order and `options` is declared last in GameMeta. Tetris is
      // the game that made that ordering matter.
      .options = kTetrisOptions,
      // 35x24, and the 24 is the tallest ask on the roster — four rows more
      // than any other game, fitting a classic 80x24 terminal exactly with
      // nothing to spare. Which makes Tetris the game most likely to trip this
      // warning on a real terminal, and the reason the footer names what the
      // player actually has beside what the game wants.
      .geometry = {.cols = tetris::kNeedCols,
                   .rows = tetris::kNeedRows,
                   .floor = SizeFloor::Drawable},
  };

  Tetris();

  [[nodiscard]] auto meta() const -> const GameMeta& override { return kMeta; }
  auto start(GameContext& ctx) -> void override;
  auto tick(std::chrono::duration<double> dt) -> void override;
  auto on_event(const termforge::Event& ev) -> bool override;
  auto draw(termforge::Screen& screen) -> void override;

  // ── Test seams ─────────────────────────────────────────────────────────────
  [[nodiscard]] auto board() const noexcept -> const tetris::Board& {
    return m_board;
  }
  [[nodiscard]] auto board() noexcept -> tetris::Board& { return m_board; }
  [[nodiscard]] auto layout() const noexcept -> const tetris::Layout& {
    return m_layout;
  }
  [[nodiscard]] auto ticks() const noexcept -> int { return m_ticks; }
  [[nodiscard]] auto hold_support() const noexcept -> tetris::HoldSupport {
    return m_board.hold_support();
  }

  auto new_game(tetris::StartLevel level) -> void;

 private:
  auto handle_key(const termforge::KeyEvent& key) -> bool;
  auto announce(const tetris::TickResult& r) -> void;
  auto record_best() -> void;
  [[nodiscard]] auto best_score() const -> long long;
  [[nodiscard]] auto best_lines() const -> long long;

  auto draw_status(termforge::Screen& screen) -> void;
  auto draw_well(termforge::Screen& screen) -> void;
  auto draw_panel(termforge::Screen& screen) -> void;
  auto draw_hints(termforge::Screen& screen) -> void;
  auto draw_too_small(termforge::Screen& screen) -> void;
  auto draw_piece_box(termforge::Screen& screen, int x, int y,
                      const tetris::Piece* p) -> void;

  // The pre-start screen (term-game#38). A member the game consults, not a Shell
  // state — see arcade/options_screen.hpp.
  OptionsScreen m_options{};
  GameContext* m_ctx{nullptr};
  tetris::Board m_board;
  tetris::Layout m_layout{};
  std::uint64_t m_seed;
  int m_ticks{0};
  termforge::Frame m_well{"Tetris"};
  termforge::Frame m_panel{};
};

}  // namespace glyphcade
