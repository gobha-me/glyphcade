#include <termgame/games/snake/snake.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>

#include <termforge/widgets/theme.hpp>

#include <termgame/games/snake/glyphs.hpp>

namespace termgame {

namespace {

using snake::Dir;
using snake::Level;
using snake::Walls;

// Reading a clock in a CONSTRUCTOR is fine; reading one inside Game::tick() is
// not, and that is the rule AGENTS.md states — dt is the only time a game may
// see. Same helper, same reason, as minesweeper.cpp and twenty48.cpp.
[[nodiscard]] auto entropy() -> std::uint64_t {
  return static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
}

// The status line's numbers. Hand-rolled for the same reason minesweeper has
// pad3(): this is the render path and the repo has no formatting dependency.
[[nodiscard]] auto num(int v) -> std::string {
  if (v == 0) {
    return "0";
  }
  std::string out;
  for (int n = v; n > 0; n /= 10) {
    out.push_back(static_cast<char>('0' + (n % 10)));
  }
  std::ranges::reverse(out);
  return out;
}

// ⚠ Switched on the ENUMS, never derived by lowercasing the UI labels below.
// minesweeper's time_key() makes the same point: derive a key from display copy
// and renaming a label silently orphans every record a player has earned.
//
// ⚠ And the key carries BOTH settings. Wrap is materially easier than Solid — it
// removes four of the five ways to die — so a single per-difficulty record would
// let a wrap run permanently outrank every solid one.
[[nodiscard]] auto score_key(Level l, Walls w) -> std::string_view {
  switch (l) {
    case Level::Easy:
      switch (w) {
        case Walls::Solid: return "best_score_easy_solid";
        case Walls::Wrap: return "best_score_easy_wrap";
      }
      break;
    case Level::Normal:
      switch (w) {
        case Walls::Solid: return "best_score_normal_solid";
        case Walls::Wrap: return "best_score_normal_wrap";
      }
      break;
    case Level::Hard:
      switch (w) {
        case Walls::Solid: return "best_score_hard_solid";
        case Walls::Wrap: return "best_score_hard_wrap";
      }
      break;
  }
  return "best_score_normal_solid";  // unreachable; both switches are exhaustive
}

[[nodiscard]] auto level_label(Level l) -> std::string_view {
  switch (l) {
    case Level::Easy: return "easy";
    case Level::Normal: return "normal";
    case Level::Hard: return "hard";
  }
  return "normal";
}

[[nodiscard]] auto walls_label(Walls w) -> std::string_view {
  switch (w) {
    case Walls::Solid: return "solid";
    case Walls::Wrap: return "wrap";
  }
  return "solid";
}

constexpr termforge::Rgb kWinFg{0xF5, 0x9E, 0x0B};
constexpr termforge::Rgb kLoseFg{0xEF, 0x44, 0x44};
// The reference's board colour (snake/css/style.css), used only as the playfield
// fill at the colour tier.
constexpr termforge::Rgb kFieldBg{0x16, 0x21, 0x3E};

[[nodiscard]] constexpr auto rgb(snake::Rgb8 c) noexcept -> termforge::Rgb {
  return termforge::Rgb{c.r, c.g, c.b};
}

}  // namespace

Snake::Snake()
    : m_board(Level::Normal, Walls::Solid, entropy()), m_seed(entropy()) {}

auto Snake::start(GameContext& ctx) -> void {
  m_ctx = &ctx;
  m_frame.set_style(ctx.border_style());
  // Nothing is reset here, deliberately: the Shell builds a fresh Game per menu
  // entry, so freshness is structural rather than something start() has to
  // remember. See arcade/game.hpp.

  // gitea #38: ask which level and which walls before the snake starts moving,
  // rather than starting on Normal/Solid and making 1/2/3/m throw the run away.
  m_options.open(kMeta.title, kMeta.options, &ctx);
}

auto Snake::new_game(Level level, Walls walls) -> void {
  // Advance the seed from the existing one rather than re-reading the clock, so
  // a case that fixes the first seed gets a reproducible *sequence* of boards.
  Rng r(m_seed);
  m_seed = r.next();
  m_board.reset(level, walls, m_seed);
  if (m_ctx != nullptr) {
    // Click, not announce(): a reset is Running -> Running with a fresh board,
    // which announce() would read as neither progress nor an outcome. Both other
    // games make the same call at the same place for the same reason.
    m_ctx->audio().play(audio::SfxId::Click);
  }
}

auto Snake::steer(Dir d) -> bool {
  const bool accepted = m_board.turn(d);
  // ⚠ A REFUSED turn is silent. There is no deny blip in the bank and inventing
  // one is a feel decision nobody who cannot hear it should make — the same
  // argument 2048's announce() guard makes, and the reason Board::turn() returns
  // a bool at all.
  if (accepted && m_ctx != nullptr) {
    m_ctx->audio().play(audio::SfxId::Click);
  }
  return accepted;
}

auto Snake::tick(std::chrono::duration<double> dt) -> void {
  // ⚠ ABOVE the gate. m_ticks is a diagnostic that counts every tick the Shell
  // routed here, not a measure of simulated time — test/13tick's routing
  // assertions read it, and gating it would make them measure the options
  // screen instead of the Shell's tick routing.
  ++m_ticks;

  // ⚠ THE GATE, and Snake is the game where its absence is visible. The snake
  // steps several times a second with no input at all, so without this it is
  // slithering behind the pre-start screen and can hit a wall — ending a run
  // the player has not begun — before they have chosen anything.
  //
  // ⚠ Every enter_snake() helper dismisses the screen before its first step(),
  // so deleting this line leaves the whole suite green. The case that catches
  // it must tick with the screen OPEN, and must tick far enough to cross the
  // step interval: a one-tick version passes against the mutant.
  if (m_options.is_open()) return;

  const snake::TickResult r = m_board.tick(dt);
  announce(r);
  if (r.eaten > 0) {
    record_best();
  }
}

auto Snake::announce(const snake::TickResult& r) -> void {
  if (m_ctx == nullptr) {
    return;
  }

  if (r.died) {
    m_ctx->audio().play(audio::SfxId::Lose);
    return;
  }
  if (r.won) {
    // Win alone, not Win over the Eat that caused it. Filling the board is
    // necessarily an eat, so Eat would always be masked a millisecond later —
    // the same call 2048's win path makes over its merge.
    m_ctx->audio().play(audio::SfxId::Win);
    return;
  }
  if (r.eaten > 0) {
    // ONE sound, even in the vanishingly rare tick that contained two steps and
    // two foods. One event, one blip.
    m_ctx->audio().play(audio::SfxId::Eat);
    return;
  }
  // ⚠ Plain movement is SILENT, and this is the one place Snake's soundscape
  // differs in kind from the other two games: it steps several times a second
  // with no input at all, so a per-step sound is not feedback, it is a metronome
  // nobody asked for. gitea #6 lists "eat, turn, die" and that list is exactly
  // right — a turn is a player gesture, a step is not.
}

auto Snake::record_best() -> void {
  if (m_ctx == nullptr) {
    return;
  }
  // Store::record() is monotone, so recording after every food needs no
  // end-of-run hook and no "is this final" flag.
  //
  // ⚠ ONE key, not two. 2048 keeps best_score AND best_tile because they are
  // genuinely independent — a lucky board can score high with a small maximum.
  // Here length is kStartLen + eaten and score is kFoodScore * eaten, so a
  // best_length record would be an affine restatement of this one: two numbers
  // that can never disagree, which is a format inviting a future reader to
  // wonder which is authoritative.
  m_ctx->scores().record(kMeta.slug, score_key(m_board.level(), m_board.walls()),
                         m_board.score(), scores::Better::Higher);
}

auto Snake::best_score() const -> int {
  if (m_ctx == nullptr) {
    return 0;
  }
  // Zero is the honest identity for a Higher record and also a real minimum
  // score, so the status row needs no "unset" spelling — unlike minesweeper's
  // best TIME, where 0 would be an unbeatable lie.
  return static_cast<int>(
      m_ctx->scores()
          .get(kMeta.slug, score_key(m_board.level(), m_board.walls()))
          .value_or(0));
}

auto Snake::on_event(const termforge::Event& ev) -> bool {
  if (m_options.is_open()) {
    switch (m_options.on_event(ev)) {
      case OptionsScreen::Reply::Ignored:
        return false;  // Escape and 'p' stay the Shell's
      case OptionsScreen::Reply::Consumed:
        return true;
      case OptionsScreen::Reply::Dismissed:
        // ⚠ Unconditional: accepting the defaults and choosing Hard/Wrap take
        // the same path, so there is no branch for a mutation to delete.
        new_game(static_cast<Level>(m_options.selected(0)),
                 static_cast<Walls>(m_options.selected(1)));
        return true;
    }
  }

  if (const auto* key = std::get_if<termforge::KeyEvent>(&ev)) {
    return handle_key(*key);
  }
  // No mouse gesture, the same answer 2048 gives: the game's only verb is a
  // direction, and a click has nothing to say that a key does not.
  //
  // Resize needs no handling: the layout is recomputed from screen.cols()/rows()
  // in every draw().
  return false;
}

auto Snake::handle_key(const termforge::KeyEvent& key) -> bool {
  using termforge::Key;

  // ⚠ Escape and 'p' are never bound here, and that absence is load-bearing:
  // Escape is the Shell's quit-to-menu and 'p' is its pause. A game that
  // consumed either would strand the player inside it. Same rule, same comment,
  // as minesweeper.cpp and twenty48.cpp.
  //
  // ⚠ Every direction key returns true whether or not the turn was accepted.
  // "Consumed" means "this game handled it", not "it changed something" — a
  // reversal handed back to the Shell would be a key that does nothing here and
  // something else there.
  switch (key.key) {
    case Key::Left:
      steer(Dir::Left);
      return true;
    case Key::Right:
      steer(Dir::Right);
      return true;
    case Key::Up:
      steer(Dir::Up);
      return true;
    case Key::Down:
      steer(Dir::Down);
      return true;
    default:
      break;
  }

  switch (key.ch) {
    // hjkl for vi hands, wasd for the reference's own bindings (main.js:176).
    // Both, because both cost one line and neither is obviously the right one.
    case 'h':
    case 'a':
      steer(Dir::Left);
      return true;
    case 'l':
    case 'd':
      steer(Dir::Right);
      return true;
    case 'k':
    case 'w':
      steer(Dir::Up);
      return true;
    case 'j':
    case 's':
      steer(Dir::Down);
      return true;

    // ⚠ A difficulty or wall-mode change RESTARTS, exactly as minesweeper's
    // 1/2/3 do. Applying either mid-run would let a player bank an easy opening
    // and finish on a record that the key it is stored under does not describe.
    case '1':
      new_game(Level::Easy, m_board.walls());
      return true;
    case '2':
      new_game(Level::Normal, m_board.walls());
      return true;
    case '3':
      new_game(Level::Hard, m_board.walls());
      return true;

    case 'm':
    case 'M':
      new_game(m_board.level(), m_board.walls() == Walls::Solid ? Walls::Wrap
                                                                : Walls::Solid);
      return true;

    case 'n':
      new_game(m_board.level(), m_board.walls());
      return true;

    case 'q':
      if (m_ctx != nullptr) {
        m_ctx->quit_to_menu();
      }
      return true;

    default:
      return false;
  }
}

auto Snake::draw(termforge::Screen& screen) -> void {
  // ⚠ Same arm as draw_too_small(): the pre-start screen owns the whole Screen,
  // and returning keeps the status and hint rows off it — they describe a run
  // that has not started.
  if (m_options.is_open()) {
    m_options.draw(screen);
    return;
  }

  // ONE Layout per frame. See layout.hpp.
  m_layout = snake::compute_layout(screen.cols(), screen.rows());

  draw_status(screen);

  if (m_layout.fits) {
    m_frame.set_style(m_ctx != nullptr ? m_ctx->border_style()
                                       : termforge::BorderStyle::Ascii);
    m_frame.set_geometry(
        {m_layout.frame_x, m_layout.frame_y, m_layout.frame_w, m_layout.frame_h});
    m_frame.draw(screen);
    draw_field(screen);
  } else {
    draw_too_small(screen);
  }

  draw_hints(screen);
}

auto Snake::draw_status(termforge::Screen& screen) -> void {
  const auto bg = termforge::theme::kBg;

  // ⚠ The outcome is a WORD, not a colour. FallbackDriver discards colour, so
  // "you died" cannot be said by painting the head red — which is precisely what
  // the reference does (renderer.js separates head, body and food by colour
  // alone). Colour here is reinforcement on top of the word.
  std::string_view word = "PLAYING";
  auto fg = termforge::theme::kDim;
  switch (m_board.state()) {
    case snake::State::Lost:
      word = "GAME OVER";
      fg = kLoseFg;
      break;
    case snake::State::Won:
      word = "BOARD FULL";
      fg = kWinFg;
      break;
    case snake::State::Running:
      break;
  }
  const int word_x = std::max(0, screen.cols() - static_cast<int>(word.size()));

  // ⚠ THE BUDGET is what stops the two halves of this row colliding, and it is
  // the load-bearing part — Screen::write_text clips at the screen edge but NOT
  // against text already on the row. Fields are appended only while they still
  // fit inside word_x, so the row degrades by dropping WHOLE fields rather than
  // truncating a number: a missing field reads as a narrow terminal, a
  // half-written one reads as a wrong score. See the long note in twenty48.cpp,
  // including why drawing the word last is a chosen failure mode and not a
  // second guard.
  //
  // ⚠ No label here may be a substring of another. test/26's whole-fields check
  // keys off find(label), so "len" alongside "level" would make that assertion
  // match the wrong field and pass while the row was broken. That is why the
  // second field is spelled "length".
  std::string left;
  const int budget = word_x - 2;  // one blank column between the two
  // The ORDER is the priority order: the loop appends until a field does not fit
  // and then stops. "record" goes last for the same reason it does in 2048.
  for (const std::string& field :
       {"score " + num(m_board.score()), "length " + num(m_board.length()),
        "level " + std::string(level_label(m_board.level())),
        "walls " + std::string(walls_label(m_board.walls())),
        "record " + num(best_score())}) {
    const std::string sep = left.empty() ? "" : "   ";
    if (static_cast<int>(left.size() + sep.size() + field.size()) > budget) {
      break;
    }
    left += sep + field;
  }
  screen.write_text(0, m_layout.status_y, left, termforge::theme::kFg, bg);
  screen.write_text(word_x, m_layout.status_y, word, fg, bg);
}

auto Snake::draw_hints(termforge::Screen& screen) -> void {
  const auto bg = termforge::theme::kBg;
  const int y = m_layout.hint_y;
  if (y <= m_layout.status_y) {
    return;
  }

  // Three widths. write_text clips, but a hint clipped mid-word reads like a
  // bug, so each width gets its own string — the same approach both other games
  // take.
  std::string_view hint;
  if (screen.cols() >= 76) {
    hint =
        "Arrows/hjkl/wasd steer  1/2/3 level  m walls  n new  q menu  p pause  "
        "Esc menu";
  } else if (screen.cols() >= 44) {
    hint = "Arrows steer  1/2/3 level  m walls  Esc menu";
  } else {
    hint = "Arrows  m walls  Esc";
  }
  screen.write_text(0, y, hint, termforge::theme::kDim, bg);
}

auto Snake::draw_field(termforge::Screen& screen) -> void {
  const bool ascii =
      m_ctx == nullptr || termforge::is_ascii(m_ctx->border_style());
  const auto& glyphs = snake::cells_for(ascii);
  const auto bg = termforge::theme::kBg;

  // At the colour tier the playfield is a filled rectangle, which is what makes
  // the board's extent visible at a glance. At the ASCII tier there is no
  // colour, so the Frame's border is the only thing that can say where the board
  // is — and the Shell has already cleared the screen, so the interior is blank
  // without our help.
  if (!ascii) {
    screen.fill_rect(m_layout.origin_x, m_layout.origin_y,
                     snake::kCellCols * snake::kCols, snake::kRows,
                     termforge::theme::kFg, kFieldBg);
  }

  const auto put = [&](snake::Coord p, snake::Cell c, termforge::Rgb fg) {
    screen.write_text(m_layout.cell_x(p.x), m_layout.cell_y(p.y),
                      snake::glyph_for(glyphs, c), fg, ascii ? bg : kFieldBg);
  };

  // Food before the snake: on a full board there is no free cell to respawn
  // into, so the last food sits under the head and the head must win.
  put(m_board.food(), snake::Cell::Food, rgb(snake::kFoodColor));

  const auto& body = m_board.body();
  for (std::size_t i = 1; i < body.size(); ++i) {
    put(body[i], snake::Cell::Body, rgb(snake::kBodyColor));
  }

  // The head last, and a DERIVED glyph when the game is lost — see glyphs.hpp
  // for why dying is a rule but the corpse is not.
  const bool dead = m_board.state() == snake::State::Lost;
  put(m_board.head(), dead ? snake::Cell::Dead : snake::Cell::Head,
      dead ? rgb(snake::kDeadColor) : rgb(snake::kHeadColor));
}

auto Snake::draw_too_small(termforge::Screen& screen) -> void {
  const auto bg = termforge::theme::kBg;
  const auto fg = termforge::theme::kFg;

  // Same answer both other games give, and the same open issue behind it:
  // GameMeta carries no minimum size, so the selector will happily launch a
  // board this terminal cannot draw (gitea #15). Until it does, the game says so
  // itself.
  const std::string need =
      "Snake needs " + num(snake::kNeedCols) + "x" + num(snake::kNeedRows);
  const int mid = screen.rows() / 2;
  const int nx = std::max(0, (screen.cols() - static_cast<int>(need.size())) / 2);
  if (mid > m_layout.status_y) {
    screen.write_text(nx, mid, need, fg, bg);
  }
}

}  // namespace termgame
