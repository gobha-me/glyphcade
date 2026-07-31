#include <termgame/games/tetris/tetris.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>

#include <termforge/widgets/theme.hpp>

namespace termgame {

namespace {

using tetris::Piece;
using tetris::Shift;
using tetris::StartLevel;

// Reading a clock in a CONSTRUCTOR is fine; reading one inside Game::tick() is
// not. Same helper, same reason, as the other three games.
[[nodiscard]] auto entropy() -> std::uint64_t {
  return static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
}

[[nodiscard]] auto num(long long v) -> std::string {
  if (v <= 0) return "0";
  std::string out;
  for (long long n = v; n > 0; n /= 10) {
    out.push_back(static_cast<char>('0' + (n % 10)));
  }
  std::ranges::reverse(out);
  return out;
}

// ⚠ Switched on the ENUM, never derived by lowercasing the UI label. Rename a
// label and a derived key orphans every record a player has earned —
// minesweeper's time_key() and snake's score_key() make the same point.
//
// ⚠ And the key carries the START LEVEL, for Snake's wrap reason: beginning at
// level 10 hands the player the score multiplier immediately, so one shared key
// would let a level-10 run permanently outrank every level-1 one.
[[nodiscard]] auto score_key(StartLevel s) -> std::string_view {
  switch (s) {
    case StartLevel::One: return "best_score_start1";
    case StartLevel::Five: return "best_score_start5";
    case StartLevel::Ten: return "best_score_start10";
  }
  return "best_score_start1";
}

// ⚠ TWO records, where Snake has one. Snake refused best_length because length
// is kStartLen + eaten and score is kFoodScore * eaten — an affine restatement,
// two numbers that can never disagree. Here they genuinely can: a tetris scores
// four times a single for the same four rows, and both drops pay points that no
// line clear is involved in. A patient endurance run and a short high-scoring
// one are different achievements, which is 2048's best_score + best_tile
// argument. ⚠ Level is NOT persisted: it is lines/10, which IS the restatement.
[[nodiscard]] auto lines_key(StartLevel s) -> std::string_view {
  switch (s) {
    case StartLevel::One: return "best_lines_start1";
    case StartLevel::Five: return "best_lines_start5";
    case StartLevel::Ten: return "best_lines_start10";
  }
  return "best_lines_start1";
}

[[nodiscard]] auto level_label(StartLevel s) -> std::string_view {
  switch (s) {
    case StartLevel::One: return "1";
    case StartLevel::Five: return "5";
    case StartLevel::Ten: return "10";
  }
  return "1";
}

}  // namespace

Tetris::Tetris()
    : m_board(StartLevel::One, tetris::HoldSupport::Discrete, entropy()),
      m_seed(entropy()) {
  m_panel.set_title("");
}

auto Tetris::start(GameContext& ctx) -> void {
  m_ctx = &ctx;
  m_well.set_style(ctx.border_style());
  m_panel.set_style(ctx.border_style());

  // ⚠ THE DEGRADATION DECISION, made once and pushed into the model.
  //
  // capabilities().kitty_keyboard is what the TERMINAL answered, not what we
  // asked for — App::keyboard_mode() is a mirror of the Shell's own setter and
  // is true everywhere. Without the protocol no Release ever arrives, so "held"
  // and "pressed again" are the same event and DAS is not expressible. The
  // model takes HoldSupport and behaves differently rather than auto-repeating
  // on a key that may already be up.
  //
  // Nothing is raised here: the Shell already raised the ErrorEvent when it
  // could not get the tier it set on our behalf (gitea #32). What this file
  // adds is that the player can SEE which arm they are on, in the hint row —
  // an event in a footer is not the same as knowing why the controls feel
  // different.
  const auto support = ctx.capabilities().kitty_keyboard
                           ? tetris::HoldSupport::Held
                           : tetris::HoldSupport::Discrete;
  m_board.reset(m_board.start_level(), support);

  // ⚠ THE ORDER OF THESE TWO LINES DOES NOT MATTER, and that is worth stating
  // because it looks like it should. new_game() rebuilds the board as
  // `tetris::Board(level, m_board.hold_support(), m_seed)` — it reads
  // hold_support back OFF the board the reset above installed it on — so the
  // obvious worry is that opening the screen first would carry the
  // constructor's default Discrete through the dismissal and silently lose DAS
  // on a kitty terminal.
  //
  // It cannot. open() does not touch the board, and both calls are in start(),
  // so the reset has always happened by the time any dismissal can. Swapping
  // them was mutation-tested and changes nothing — including on the Held arm,
  // which test/28tetris-ui now reaches by handing a Tetris a hand-built
  // GameContext with kitty_keyboard set, rather than through the Shell.
  //
  // What WOULD break is moving the reset out of start(), or reading
  // hold_support() before it. Neither is what this ordering protects, so do not
  // read the ordering as a guard.
  m_options.open(kMeta.title, kMeta.options, &ctx);
}

auto Tetris::new_game(StartLevel level) -> void {
  // Advance the seed rather than re-reading the clock, so a case that fixes the
  // first seed gets a reproducible SEQUENCE of bags.
  m_seed = m_seed * 6364136223846793005ULL + 1442695040888963407ULL;
  m_board = tetris::Board(level, m_board.hold_support(), m_seed);
  if (m_ctx != nullptr) m_ctx->audio().play(audio::SfxId::Click);
}

auto Tetris::tick(std::chrono::duration<double> dt) -> void {
  // ⚠ Above the gate: a diagnostic of the Shell's tick routing, not of
  // simulated time. Same rule as Snake's.
  ++m_ticks;

  // ⚠ Gravity runs on its own, so without this the piece is falling behind the
  // pre-start screen and can lock — or top out — before the player has chosen a
  // start level. Every enter_tetris() dismisses before its first step(), so
  // deleting this leaves the suite green; the case that catches it ticks with
  // the screen open, far enough to cross the gravity interval.
  if (m_options.is_open()) return;

  const tetris::TickResult r = m_board.tick(dt);
  announce(r);
  if (r.lines > 0) record_best();
}

auto Tetris::announce(const tetris::TickResult& r) -> void {
  if (m_ctx == nullptr) return;

  if (r.topped_out) {
    m_ctx->audio().play(audio::SfxId::Lose);
    return;
  }
  // One sound per event, most significant first. A tick that cleared four rows
  // AND levelled up is one moment to the player, not two.
  if (r.tetris) {
    m_ctx->audio().play(audio::SfxId::Tetris);
    return;
  }
  if (r.lines > 0) {
    m_ctx->audio().play(audio::SfxId::Merge);
    return;
  }
  if (r.leveled) {
    m_ctx->audio().play(audio::SfxId::LevelUp);
    return;
  }
  if (r.locked) {
    m_ctx->audio().play(audio::SfxId::Lock);
    return;
  }
  // ⚠ GRAVITY AND AUTO-SHIFT ARE SILENT. A piece falls several times a second
  // with no input at all, and DAS fires every 50 ms while a key is held — a
  // sound on either is a metronome rather than feedback. Exactly the argument
  // that kept Spawn out of 2048 and Step out of Snake, and here it applies to
  // r.steps and r.shifts both.
}

auto Tetris::record_best() -> void {
  if (m_ctx == nullptr) return;
  // Store::record() is monotone, so recording after every clear needs no
  // end-of-run hook and no "is this final" flag.
  m_ctx->scores().record(kMeta.slug, score_key(m_board.start_level()),
                         m_board.score(), scores::Better::Higher);
  m_ctx->scores().record(kMeta.slug, lines_key(m_board.start_level()),
                         m_board.lines(), scores::Better::Higher);
}

auto Tetris::best_score() const -> long long {
  if (m_ctx == nullptr) return 0;
  return m_ctx->scores()
      .get(kMeta.slug, score_key(m_board.start_level()))
      .value_or(0);
}

auto Tetris::best_lines() const -> long long {
  if (m_ctx == nullptr) return 0;
  return m_ctx->scores()
      .get(kMeta.slug, lines_key(m_board.start_level()))
      .value_or(0);
}

// ── Input ───────────────────────────────────────────────────────────────────

auto Tetris::on_event(const termforge::Event& ev) -> bool {
  if (m_options.is_open()) {
    switch (m_options.on_event(ev)) {
      case OptionsScreen::Reply::Ignored:
        return false;  // Escape and 'p' stay the Shell's
      case OptionsScreen::Reply::Consumed:
        return true;
      case OptionsScreen::Reply::Dismissed:
        new_game(static_cast<tetris::StartLevel>(m_options.selected(0)));
        return true;
    }
  }

  // Mouse and resize are declined, not swallowed. Tetris hit-tests nothing, and
  // a resize is answered by recomputing the layout in draw().
  if (const auto* key = std::get_if<termforge::KeyEvent>(&ev)) {
    return handle_key(*key);
  }
  return false;
}

auto Tetris::handle_key(const termforge::KeyEvent& key) -> bool {
  // ⚠ Escape and 'p' are never bound here, and that absence is load-bearing:
  // Escape is the Shell's quit-to-menu and 'p' is its pause. A game that
  // consumed either would strand the player inside it.

  // ── Releases ──────────────────────────────────────────────────────────────
  //
  // ⚠ THE REASON THIS GAME ASKED FOR KeyboardMode::Enhanced. Under Legacy this
  // branch is unreachable, which is exactly the problem: without it the model
  // never learns a key came up, so a hold never ends and the piece slides until
  // it hits a wall. Under Discrete the model never starts a repeat in the first
  // place, so a release that never arrives costs nothing.
  if (key.action == termforge::KeyAction::Release) {
    switch (key.key) {
      case termforge::Key::Left: m_board.release_shift(Shift::Left); return true;
      case termforge::Key::Right:
        m_board.release_shift(Shift::Right);
        return true;
      case termforge::Key::Down: m_board.release_soft_drop(); return true;
      case termforge::Key::Char:
        switch (key.ch) {
          case U'h': case U'a': m_board.release_shift(Shift::Left); return true;
          case U'l': case U'd': m_board.release_shift(Shift::Right); return true;
          case U'j': case U's': m_board.release_soft_drop(); return true;
          default: return false;
        }
      default: return false;
    }
  }

  // ⚠ Repeat is treated as a press, which is upstream's stated contract: the
  // protocol sends Repeat INSTEAD OF a second press. For the shift keys the
  // model's own auto-repeat has already taken over by then and press_shift is
  // idempotent on direction; for rotate and hard drop, treating a held key as
  // repeated presses is what a player expects.

  // ── Presses ───────────────────────────────────────────────────────────────
  //
  // ⚠ Every bound key returns true whether or not the move was accepted.
  // "Consumed" means "this game handled it", not "it changed something" — a
  // refused shift handed back to the Shell would be a key that does nothing
  // here and something else there. Same rule Snake's handle_key states.
  switch (key.key) {
    case termforge::Key::Left:
      static_cast<void>(m_board.press_shift(Shift::Left));
      return true;
    case termforge::Key::Right:
      static_cast<void>(m_board.press_shift(Shift::Right));
      return true;
    case termforge::Key::Down:
      static_cast<void>(m_board.press_soft_drop());
      return true;
    case termforge::Key::Up:
      if (m_board.rotate(1) && m_ctx != nullptr) {
        m_ctx->audio().play(audio::SfxId::Click);
      }
      return true;
    case termforge::Key::Char: break;
    default: return false;
  }

  switch (key.ch) {
    case U'h': case U'H': case U'a': case U'A':
      static_cast<void>(m_board.press_shift(Shift::Left));
      return true;
    case U'l': case U'L': case U'd': case U'D':
      static_cast<void>(m_board.press_shift(Shift::Right));
      return true;
    case U'j': case U'J': case U's': case U'S':
      static_cast<void>(m_board.press_soft_drop());
      return true;
    case U'k': case U'K': case U'w': case U'W': case U'x': case U'X':
      if (m_board.rotate(1) && m_ctx != nullptr) {
        m_ctx->audio().play(audio::SfxId::Click);
      }
      return true;
    case U'z': case U'Z':
      if (m_board.rotate(-1) && m_ctx != nullptr) {
        m_ctx->audio().play(audio::SfxId::Click);
      }
      return true;
    case U' ':
      if (m_board.hard_drop() && m_ctx != nullptr) {
        m_ctx->audio().play(audio::SfxId::Slide);
      }
      // A hard drop locks, which may clear rows and may top out. Both are worth
      // recording immediately: record() is monotone, so an extra call is free.
      record_best();
      return true;
    case U'c': case U'C':
      if (m_board.hold() && m_ctx != nullptr) {
        m_ctx->audio().play(audio::SfxId::Click);
      }
      return true;
    // ⚠ A start-level change RESTARTS, exactly as minesweeper's and snake's
    // 1/2/3 do. Applying it mid-run would let a player bank an easy opening and
    // finish on a record whose key does not describe how it was earned.
    case U'1': new_game(StartLevel::One); return true;
    case U'2': new_game(StartLevel::Five); return true;
    case U'3': new_game(StartLevel::Ten); return true;
    case U'n': case U'N': new_game(m_board.start_level()); return true;
    case U'q': case U'Q':
      if (m_ctx != nullptr) m_ctx->quit_to_menu();
      return true;
    default: return false;
  }
}

// ── Rendering ───────────────────────────────────────────────────────────────

auto Tetris::draw(termforge::Screen& screen) -> void {
  if (m_options.is_open()) {
    m_options.draw(screen);
    return;
  }

  // ONE Layout per frame. See layout.hpp.
  m_layout = tetris::compute_layout(screen.cols(), screen.rows());
  draw_status(screen);
  if (m_layout.fits) {
    const auto style = m_ctx != nullptr ? m_ctx->border_style()
                                        : termforge::BorderStyle::Ascii;
    m_well.set_style(style);
    m_well.set_geometry({m_layout.well_x, m_layout.well_y, tetris::kWellCols,
                         tetris::kWellRows});
    m_well.draw(screen);
    m_panel.set_style(style);
    m_panel.set_geometry({m_layout.panel_x, m_layout.panel_y,
                          tetris::kPanelCols, tetris::kWellRows});
    m_panel.draw(screen);
    draw_well(screen);
    draw_panel(screen);
  } else {
    draw_too_small(screen);
  }
  draw_hints(screen);
}

auto Tetris::draw_status(termforge::Screen& screen) -> void {
  const auto fg = termforge::theme::kFg;
  const auto bg = termforge::theme::kBg;

  // ⚠ The outcome is a WORD, not a colour. FallbackDriver discards colour, so a
  // red "game over" is an invisible game over.
  const std::string word =
      m_board.state() == tetris::State::ToppedOut ? "TOPPED OUT" : "PLAYING";
  const int word_x = std::max(0, screen.cols() - static_cast<int>(word.size()));
  screen.write_text(word_x, m_layout.status_y, word, fg, bg);

  // ⚠ THE BUDGET is what stops the two halves of this row colliding, and it is
  // the load-bearing part: Screen::write_text clips at the screen edge but NOT
  // against text already on the row. Fields are appended only while they still
  // fit inside word_x, so the row degrades by dropping WHOLE fields rather than
  // truncating a number mid-digit.
  //
  // ⚠ This exact mutation has gone green in TWO consecutive epics, because the
  // loop stops appending long before the left text can reach the right-aligned
  // word at any width the game itself fits on. The case that catches it has to
  // sweep widths NARROWER than kNeedCols — the status row is drawn whether or
  // not the well fits.
  //
  // ⚠ No label here may be a substring of another. The whole-fields check keys
  // off find(label), so "line" alongside "lines" would match the wrong field
  // and pass while the row was broken.
  std::string left;
  const int budget = word_x - 2;
  for (const std::string& field :
       {"score " + num(m_board.score()), "lines " + num(m_board.lines()),
        "level " + num(m_board.level()),
        "start " + std::string(level_label(m_board.start_level())),
        "record " + num(best_score()), "longest " + num(best_lines())}) {
    const std::string sep = left.empty() ? "" : "   ";
    if (static_cast<int>(left.size() + sep.size() + field.size()) > budget) {
      break;
    }
    left += sep + field;
  }
  screen.write_text(0, m_layout.status_y, left, fg, bg);
}

auto Tetris::draw_well(termforge::Screen& screen) -> void {
  const bool ascii =
      m_ctx == nullptr || termforge::is_ascii(m_ctx->border_style());
  const tetris::CellGlyphs& g = tetris::cells_for(ascii);
  const auto bg = termforge::theme::kBg;

  const auto clearing = m_board.clearing();
  const auto is_clearing = [&clearing](int row) {
    return std::ranges::find(clearing, row) != clearing.end();
  };

  const tetris::Active& a = m_board.active();
  const int ghost = m_board.ghost_y();
  const auto in_piece = [&a](int px, int py, int col, int row) {
    for (int r = 0; r < tetris::kBoxMax; ++r) {
      for (int c = 0; c < tetris::kBoxMax; ++c) {
        if (!tetris::cell_at(a.piece, a.rot, r, c)) continue;
        if (px + c == col && py + r == row) return true;
      }
    }
    return false;
  };

  for (int row = tetris::kHiddenRows; row < tetris::kRows; ++row) {
    for (int col = 0; col < tetris::kCols; ++col) {
      // ⚠ ORDER IS THE DESIGN. The active piece wins over its own ghost (they
      // overlap when the piece is already resting) and over the stack; a
      // clearing row wins over the stack it is made of; and the ghost is drawn
      // only where nothing else is, so it never hides a real block.
      tetris::Cell cell = tetris::Cell::Empty;
      if (in_piece(a.x, a.y, col, row)) {
        cell = tetris::Cell::Active;
      } else if (is_clearing(row)) {
        cell = tetris::Cell::Clearing;
      } else if (m_board.filled(col, row)) {
        cell = tetris::Cell::Stack;
      } else if (in_piece(a.x, ghost, col, row)) {
        cell = tetris::Cell::Ghost;
      }
      if (cell == tetris::Cell::Empty) continue;

      auto fg = termforge::theme::kFg;
      if (!ascii) {
        if (cell == tetris::Cell::Active || cell == tetris::Cell::Ghost) {
          fg = tetris::colour_for(a.piece);
        } else if (const auto p = m_board.piece_at(col, row); p.has_value()) {
          fg = tetris::colour_for(*p);
        }
      }
      screen.write_text(m_layout.cell_x(col), m_layout.cell_y(row),
                        std::string(tetris::glyph_for(g, cell)), fg, bg);
    }
  }
}

auto Tetris::draw_piece_box(termforge::Screen& screen, int x, int y,
                            const Piece* p) -> void {
  const bool ascii =
      m_ctx == nullptr || termforge::is_ascii(m_ctx->border_style());
  const tetris::CellGlyphs& g = tetris::cells_for(ascii);
  const auto bg = termforge::theme::kBg;
  if (p == nullptr) return;

  const auto fg = ascii ? termforge::theme::kFg : tetris::colour_for(*p);
  for (int r = 0; r < 2; ++r) {
    for (int c = 0; c < tetris::kBoxMax; ++c) {
      // Rotation 0 of every piece lives in the top two rows of its box, which
      // is why a preview is two rows rather than four. I is the exception in
      // shape but not in extent: its single row is row 1.
      if (!tetris::cell_at(*p, 0, r, c)) continue;
      screen.write_text(x + (tetris::kCellCols * c), y + r,
                        std::string(g.stack), fg, bg);
    }
  }
}

auto Tetris::draw_panel(termforge::Screen& screen) -> void {
  const auto fg = termforge::theme::kFg;
  const auto dim = termforge::theme::kDim;
  const auto bg = termforge::theme::kBg;
  const int x = m_layout.panel_x + 1;
  int y = m_layout.panel_y + 1;

  screen.write_text(x, y, "HOLD", dim, bg);
  ++y;
  const Piece* h = m_board.held();
  if (h != nullptr) {
    draw_piece_box(screen, x, y, h);
  } else {
    screen.write_text(x, y, "-", dim, bg);
  }
  y += 3;

  screen.write_text(x, y, "NEXT", dim, bg);
  ++y;
  for (const Piece p : m_board.preview()) {
    draw_piece_box(screen, x, y, &p);
    y += 3;
  }

  screen.write_text(x, y, "score", dim, bg);
  screen.write_text(x, y + 1, num(m_board.score()), fg, bg);
  screen.write_text(x, y + 2, "lines " + num(m_board.lines()), fg, bg);
  screen.write_text(x, y + 3, "level " + num(m_board.level()), fg, bg);
}

auto Tetris::draw_hints(termforge::Screen& screen) -> void {
  const auto dim = termforge::theme::kDim;
  const auto bg = termforge::theme::kBg;

  // ⚠ THE DEGRADED ARM IS VISIBLE, not merely logged. The Shell raises an
  // ErrorEvent on a terminal without the keyboard protocol, but that lands on
  // the selector's footer, which the player is not looking at while playing —
  // and "the controls feel wrong and nothing said why" is the outcome the
  // degradation contract exists to prevent. It is also what makes the contract
  // checkable at the bottom tier by test/28tetris-ui, since every headless
  // frame runs on FallbackDriver and therefore on this arm.
  const std::string_view hold =
      m_board.hold_support() == tetris::HoldSupport::Held
          ? "hold to shift"
          : "no key-release: single steps";

  // write_text clips, but a hint clipped mid-word reads like a bug, so each
  // width gets its own string.
  std::string line;
  if (screen.cols() >= 78) {
    line = "arrows/hjkl move  z/x rotate  space drop  c hold  1/2/3 start  n "
           "new  q menu  [";
    line += std::string(hold) + "]";
  } else if (screen.cols() >= 46) {
    line = "hjkl move  zx rotate  space drop  c hold  q menu";
  } else {
    line = "q menu";
  }
  screen.write_text(0, m_layout.hint_y, line, dim, bg);
}

auto Tetris::draw_too_small(termforge::Screen& screen) -> void {
  const auto fg = termforge::theme::kFg;
  const auto bg = termforge::theme::kBg;
  const std::string want = "needs " + num(tetris::kNeedCols) + "x" +
                           num(tetris::kNeedRows);
  const int y = std::max(1, screen.rows() / 2);
  screen.write_text(0, y, "terminal too small", fg, bg);
  screen.write_text(0, std::min(screen.rows() - 2, y + 1), want, fg, bg);
}

}  // namespace termgame
