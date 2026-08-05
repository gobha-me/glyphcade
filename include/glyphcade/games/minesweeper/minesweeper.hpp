#pragma once

// glyphcade — Minesweeper: the Game the Shell hosts.
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
// SFX bind in THIS file and nowhere lower (Epic 2, gitea #3). Board's verbs
// return "did anything change", and announce() turns that plus a state
// comparison into a sound. Board itself learns nothing about audio — audio is
// presentation, and AGENTS.md keeps game logic clear of presentation for the
// same reason it keeps it clear of rendering.
//
// A best time PER LEVEL persists through GameContext::scores() (gitea #14). It
// records on the win transition only, keyed off the Level enum rather than the
// level's display name, and it is the Better::Lower half of that store's design —
// 2048 wanting a best score and this wanting a best time is what proved a record
// is a keyed value with a direction rather than one integer. See announce().

#include <chrono>
#include <cstdint>
#include <string>

#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>
#include <termforge/widgets/frame.hpp>

#include <glyphcade/arcade/context.hpp>
#include <glyphcade/arcade/game.hpp>
#include <glyphcade/arcade/game_meta.hpp>
#include <glyphcade/arcade/options_screen.hpp>
#include <glyphcade/games/minesweeper/board.hpp>
#include <glyphcade/games/minesweeper/layout.hpp>

namespace glyphcade {

// ⚠ Namespace scope, not inline in kMeta. OptionSpec::choices is a span, and a
// choices array written inside the initialiser below would be a temporary the
// span outlives — compiling cleanly and dangling for the whole run. See
// OptionSpec in arcade/game_meta.hpp.
inline constexpr OptionSpec kMinesweeperOptions[]{
    {.label = "Level",
     .choices = minesweeper::kLevelNames,
     // Easy, which is exactly what the constructor already picked. The screen
     // changes WHEN you choose, not what you get by default.
     .default_index = 0},
};

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
      .options = kMinesweeperOptions,
      // ⚠ EASY'S 21x13, NOT HARD'S 63x20, and this game is the reason
      // GameGeometry's contract is "playable at all" rather than "playable at
      // every setting". On a 40x15 terminal you can play Minesweeper; you just
      // cannot play it on Hard. Declaring 63x20 here would have the selector
      // warn about a game that is about to work fine.
      //
      // ⚠ DERIVED FROM THE OPTION'S OWN DEFAULT, not from a typed `Level::Easy`.
      // Which level a player actually starts on is decided in exactly one place
      // — kMinesweeperOptions[0].default_index, which the options screen hands
      // back and the game casts straight to a Level (board.hpp says why that
      // cast is safe). Writing Easy here as well would be a second copy of that
      // decision: change the default to Medium and the menu would go on
      // advertising 21x13 and go on staying silent at 30x18, while the game
      // that starts needs 35x20 and lands on its own too-small screen. Every
      // test would stay green, because they would all re-derive from the same
      // hardcoded Easy.
      .geometry = {.cols = minesweeper::needed_cols(
                       minesweeper::default_preset(kMinesweeperOptions).cols),
                   .rows = minesweeper::needed_rows(
                       minesweeper::default_preset(kMinesweeperOptions).rows),
                   .floor = SizeFloor::Drawable},
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

  // The audible consequence of a player verb, decided by comparing the board
  // ACROSS the call.
  //
  // ⚠ This exists because Board cannot answer the question and must not learn
  // to. reveal() returns the same `true` for "opened some cells" and "stood on
  // a mine" (board.cpp), and chord() reaches lose() and win() transitively
  // through reveal() — so the sound is a function of the STATE TRANSITION, not
  // of the return value. Teaching Board to report which one happened would be
  // teaching game logic about presentation, which AGENTS.md forbids for exactly
  // the reason it forbids Board knowing about rendering.
  //
  // One helper rather than two because chord needs precisely the same
  // comparison reveal does. It deliberately does NOT take the verb's bool —
  // see the note at its definition for why that turned out to be redundant.
  auto announce(minesweeper::State before, int revealed_before) -> void;

  // The same idea for the marking verb, which needs a different question asked
  // (what did the cell become) rather than a different answer to the same one.
  auto announce_mark(minesweeper::Coord at, bool changed) -> void;

  auto new_game(minesweeper::Level level) -> void;
  auto move_cursor(int dr, int dc) -> void;

  // The stored best time for the CURRENT level, three columns, or "---" when
  // there is none — see the note at the definition for why an unset Lower record
  // cannot be spelled 000.
  [[nodiscard]] auto best_time() const -> std::string;
  auto draw_status(termforge::Screen& screen) -> void;
  auto draw_hints(termforge::Screen& screen) -> void;
  auto draw_grid(termforge::Screen& screen) -> void;
  auto draw_too_small(termforge::Screen& screen) -> void;

  GameContext* m_ctx{nullptr};
  // The pre-start screen (gitea #38). A member the game consults, NOT a Shell
  // state: Shell::state() is InGame from the moment Enter is pressed in the
  // menu, which is why entering a game still means what it did.
  OptionsScreen m_options{};
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

}  // namespace glyphcade
