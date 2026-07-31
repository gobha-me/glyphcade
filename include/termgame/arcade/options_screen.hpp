#pragma once

// term-game — OptionsScreen: the settings a game asks about before its first
// frame of play. gitea #38.
//
// ── WHAT THIS IS NOT, because both alternatives were considered and rejected ─
//
// ⚠ It is NOT a Shell state. Shell::State stays {Selector, InGame, Paused}, and
// state() is InGame from the moment Enter is pressed in the menu. That is what
// keeps ~110 existing test call sites correct without touching one of them, and
// it is also what makes pause and quit-to-menu work here for free — the Shell
// is already in the state where it handles both.
//
// ⚠ It is NOT a Game-interface change. arcade/game.hpp is untouched. This is a
// member a game holds and consults from its own draw() and on_event(), in
// exactly the arm where every game already branches on m_layout.fits to draw
// draw_too_small(). A game that wants a settings screen composes one; a game
// that does not never learns this header exists, and 2048 does not.
//
// ⚠ IN CORE, by src/lib/CMakeLists.txt's rule: a game may call it, so it sits
// below the games. It reads the SAME std::span<const OptionSpec> the Shell
// advertises in the selector's detail pane, which is what stops the menu
// promising an option the game does not have.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>

#include <termgame/arcade/context.hpp>
#include <termgame/arcade/game_meta.hpp>

namespace termgame {

class OptionsScreen {
 public:
  // ⚠ 20x8 — Shell::kMinCols x kMinRows, NOT any game's own floor, and the
  // difference is the whole reason this constant is written down. Shell's
  // on_render refuses to call Game::draw at all below 20x8, so that is the
  // smallest screen this class can ever be handed. It is SMALLER than every
  // game's playfield floor, which means "the board does not fit" and "the
  // options do not fit" are different questions with different answers, and a
  // screen that only survives the game's own minimum has not been tested.
  static constexpr int kMinCols = 20;
  static constexpr int kMinRows = 8;

  enum class Reply : std::uint8_t {
    Ignored,    // not ours — the caller MUST return false so the Shell sees it
    Consumed,   // handled; the screen is still up
    Dismissed,  // the player started the game; read selected() and apply it
  };

  // Opens iff `options` is non-empty; with an empty span this is a no-op and
  // is_open() stays false, so a game may call it unconditionally.
  //
  // `title` is the game's own meta title, already static_asserted ASCII.
  //
  // `ctx` may be null. A game constructed outside a Shell draws at the Ascii
  // tier and makes no sound — the same `m_ctx != nullptr ? ... : Ascii`
  // fallback every game's draw() already writes.
  auto open(std::string_view title, std::span<const OptionSpec> options,
            GameContext* ctx) -> void;

  // Override one option's starting choice, replacing its default_index.
  //
  // ⚠ THIS IS NOT A CONVENIENCE, and the distinction it draws is load-bearing.
  // A default is not always constexpr and GameMeta cannot hold one that is not:
  // Sokoban's default is "the first level you have not solved", which is a
  // function of the score store and is computed in Sokoban::start(). So
  // default_index means WHAT THE SELECTOR ADVERTISES — it is read by the detail
  // pane, before any game exists — and this means WHAT THE GAME STARTS ON.
  // Without the split, either the pane would have to lie or the schema would
  // have to stop being constexpr.
  //
  // Out-of-range values are CLAMPED, not rejected, for the same reason
  // Sokoban::load() clamps: the caller's index may come from a persisted score
  // file that was written when the level pack was a different size.
  auto preselect(std::size_t option, int choice) -> void;

  [[nodiscard]] auto is_open() const noexcept -> bool { return m_open; }

  // Index into OptionSpec::choices for one option. Valid while open and after a
  // Dismissed reply — the game reads it during dismissal, so it must outlive
  // the close. Returns 0 for an out-of-range option.
  [[nodiscard]] auto selected(std::size_t option) const noexcept -> int;

  // Test seams. cursor() is which option row is highlighted; it is what
  // distinguishes "preselect worked" from "the game happened to load the right
  // level anyway", which no other observable does.
  [[nodiscard]] auto cursor() const noexcept -> std::size_t { return m_row; }
  [[nodiscard]] auto option_count() const noexcept -> std::size_t {
    return m_options.size();
  }

  // Up/Down/j/k move between options; Left/Right/h/l change the current one;
  // Enter and Space start the game.
  //
  // ⚠ Values CLAMP rather than wrap. Wrapping a three-choice cycler means one
  // extra press silently takes Hard back to Easy, and the player who was
  // holding the key does not see it happen.
  //
  // ⚠ Escape and 'p' return Ignored ON PURPOSE. Escape is the Shell's
  // quit-to-menu and 'p' is its pause (see arcade/game.hpp's "do not consume
  // Key::Escape"); consuming either would strand the player on a screen with no
  // way out but starting a game they did not want. The caller must translate
  // Ignored into `return false` from Game::on_event.
  //
  // ⚠ EVERY KeyAction::Release returns Ignored, and that is NOT defensive
  // symmetry with Shell::shell_may_act. Tetris declares KeyboardMode::Enhanced,
  // and the Shell sets the tier INSIDE enter_selected_game — before the Enter
  // that entered the game has come back up. So on a terminal that granted the
  // kitty protocol, the RELEASE of that very keystroke is delivered here, and
  // acting on it dismisses this screen before a single frame is drawn. The
  // player sees the options flash and vanish.
  //
  // ⚠ Nothing in CI or in this container can see that: test_run_frames installs
  // a FallbackDriver whose capabilities are all false, and script(1) is not a
  // kitty terminal, so kitty_keyboard is never true here. Its only coverage is
  // a SYNTHESISED Release event in test/33options. Do not "simplify" this guard
  // on the evidence that everything is green.
  [[nodiscard]] auto on_event(const termforge::Event& ev) -> Reply;

  // Full repaint, immediate mode — the same contract as Game::draw.
  auto draw(termforge::Screen& screen) -> void;

 private:
  auto draw_cycler(termforge::Screen& screen, std::size_t i, int y, int cols,
                   bool ascii) -> void;
  auto draw_list(termforge::Screen& screen, int top, int rows, int cols,
                 bool ascii) -> void;
  auto move_row(int delta) -> Reply;
  auto move_choice(int delta) -> Reply;
  auto click() -> void;

  // ⚠ True iff the single option renders as a windowed vertical list rather
  // than a row of `< value >` cyclers. all_games.cpp static_asserts that such
  // an option is the ONLY one a game declares, which is what lets this be a
  // property of the screen rather than of each row.
  [[nodiscard]] auto is_list_mode() const noexcept -> bool;

  std::span<const OptionSpec> m_options{};
  std::string_view m_title{};
  GameContext* m_ctx{nullptr};

  // ⚠ Fixed array, not a vector: draw() and on_event() are on the render path,
  // and the audio rule's "no allocation" instinct applies to a 60 Hz loop too.
  // kMaxGameOptions is static_asserted against every registered game.
  int m_choice[kMaxGameOptions]{};

  std::size_t m_row{0};
  int m_scroll{0};  // list mode only: first visible choice
  bool m_open{false};
};

}  // namespace termgame
