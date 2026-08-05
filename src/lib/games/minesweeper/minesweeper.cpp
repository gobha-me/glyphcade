#include <glyphcade/games/minesweeper/minesweeper.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <variant>

#include <termforge/widgets/theme.hpp>

#include <glyphcade/arcade/hud.hpp>
#include <glyphcade/games/minesweeper/glyphs.hpp>

namespace glyphcade {
namespace {

using minesweeper::Coord;
using minesweeper::Level;
using minesweeper::State;

constexpr termforge::Rgb kMineFg{0xEF, 0x44, 0x44};
constexpr termforge::Rgb kFlagFg{0xF5, 0x9E, 0x0B};
constexpr termforge::Rgb kCursorFg{0x40, 0xE0, 0xFF};

// Three columns, sign-aware. No <format> and no stringstream — this file is on
// the render path and the repo has no formatting dependency (the same reason
// the retired StubGame hand-rolled fixed2()).
auto pad3(int v) -> std::string {
  if (v < 0) {
    const int a = std::min(-v, 99);
    return "-" + std::string(a < 10 ? "0" : "") + std::to_string(a);
  }
  const int a = std::min(v, 999);
  std::string s = std::to_string(a);
  while (s.size() < 3) s.insert(s.begin(), '0');
  return s;
}

// The store key for a level's best time.
//
// ⚠ Switched on the Level ENUM, never built by lowercasing preset(level).name.
// That name is display text — "EASY" on the status row — and deriving a storage
// key from it would couple the on-disk format to UI copy, so renaming a label
// would silently orphan every record already written, with nothing to catch it at
// compile time.
//
// ⚠ No `default:` inside the switch, on purpose. A fourth Level then fails to
// build under -Werror instead of quietly filing its records under Easy.
[[nodiscard]] auto time_key(Level level) -> std::string_view {
  switch (level) {
    case Level::Easy:
      return "best_time_easy";
    case Level::Medium:
      return "best_time_medium";
    case Level::Hard:
      return "best_time_hard";
  }
  return "best_time_easy";  // unreachable; the switch above is exhaustive
}

// A per-entry seed. Reading a clock here is fine and is NOT the thing AGENTS.md
// forbids — that rule is about Game::tick, where dt must be the only time that
// exists. A constructor runs once, outside the simulation.
[[nodiscard]] auto entropy() -> std::uint64_t {
  return static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
}

}  // namespace

Minesweeper::Minesweeper()
    : m_board(minesweeper::preset(Level::Easy), entropy()), m_seed(entropy()) {}

auto Minesweeper::start(GameContext& ctx) -> void {
  // Storing the address is safe: the Shell owns one GameContext for its whole
  // life and never recreates it per game (arcade/context.hpp).
  //
  // Nothing is reset here, deliberately. This object was constructed moments
  // ago by the registry factory; freshness is structural, not a routine.
  m_ctx = &ctx;
  m_frame.set_style(ctx.border_style());

  // term-game#38: ask before starting rather than starting on Easy and making the
  // player throw the board away. The constructor already built an Easy board,
  // so the screen is a chance to change that, not a prerequisite for having one
  // — a resize or a stray frame before the first Enter still draws something.
  m_options.open(kMeta.title, kMeta.options, &ctx);
}

auto Minesweeper::tick(std::chrono::duration<double> dt) -> void {
  // ⚠ Two accumulators, and they are not the same thing. See the warning on
  // ticks()/elapsed() in the header before merging them.
  ++m_ticks;
  m_elapsed += dt;
  m_min_dt = std::min(m_min_dt, dt);

  // The game clock, which Board gates on "has the player opened anything yet".
  m_board.advance(dt);
}

auto Minesweeper::new_game(Level level) -> void {
  m_level = level;
  // Advance the seed rather than re-reading a clock: two new games inside one
  // coarse clock tick would otherwise deal the same board.
  minesweeper::Rng r(m_seed);
  m_seed = r.next();
  m_board.reset(minesweeper::preset(level), m_seed);
  m_cursor = Coord{.row = m_board.rows() / 2, .col = m_board.cols() / 2};

  // ⚠ NOT routed through announce(), and not because it was forgotten: reset()
  // moves the state Playing -> Ready, which announce() would read as neither
  // progress nor an outcome. A new board is an acknowledgement, so it clicks.
  if (m_ctx != nullptr) m_ctx->audio().play(audio::SfxId::Click);
}

auto Minesweeper::move_cursor(int dr, int dc) -> void {
  // Clamped, not wrapped: on a grid you are reasoning about spatially, wrapping
  // from the last column to the first is a teleport.
  m_cursor.row = std::clamp(m_cursor.row + dr, 0, m_board.rows() - 1);
  m_cursor.col = std::clamp(m_cursor.col + dc, 0, m_board.cols() - 1);
}

auto Minesweeper::on_event(const termforge::Event& ev) -> bool {
  // ⚠ FIRST, before the game's own keys. While the pre-start screen is up the
  // board is not being played, so nothing below should see input.
  if (m_options.is_open()) {
    switch (m_options.on_event(ev)) {
      case OptionsScreen::Reply::Ignored:
        // Escape and 'p' land here. Returning false is what lets the Shell
        // quit to menu and pause from the options screen.
        return false;
      case OptionsScreen::Reply::Consumed:
        return true;
      case OptionsScreen::Reply::Dismissed:
        // ⚠ UNCONDITIONAL, even when nothing was changed. "The player accepted
        // Easy" and "the player picked Hard" take the same path, so there is no
        // special case to drift and no branch a mutation can delete. new_game()
        // also re-seeds, which is what makes the board you get the one you
        // chose rather than the one the constructor guessed.
        new_game(static_cast<Level>(m_options.selected(0)));
        return true;
    }
  }
  if (const auto* key = std::get_if<termforge::KeyEvent>(&ev)) {
    return handle_key(*key);
  }
  if (const auto* mouse = std::get_if<termforge::MouseEvent>(&ev)) {
    return handle_mouse(*mouse);
  }
  // Resize, paste and error events are declined. The layout is recomputed from
  // the Screen's own size every frame, so a resize needs no handling here.
  return false;
}

auto Minesweeper::announce(minesweeper::State before,
                           int revealed_before) -> void {
  if (m_ctx == nullptr) return;

  // ⚠ The verb's bool is deliberately NOT a parameter, and that is a finding
  // rather than an oversight. An earlier version took it and returned early on
  // false; mutation testing showed removing that guard changed nothing, because
  // the two comparisons below already subsume it — a verb that did nothing
  // moved neither the state nor the revealed count, so it falls through to
  // silence on its own. Carrying the bool as well would be a second, weaker
  // statement of the same fact, and the kind of redundancy that later reads as
  // load-bearing.
  //
  // A verb that did nothing IS silent, and that matters: there is no deny blip
  // in the bank and inventing one is a feel decision nobody who cannot hear it
  // should make. Revealing an open cell, pressing space on a flag, and chording
  // with the wrong flag count all arrive here and all leave quietly.
  const auto now = m_board.state();

  if (now == minesweeper::State::Lost && before != minesweeper::State::Lost) {
    // Two sounds, one line apart, on purpose: Explode is the detonation
    // impulse and Lose is the falling tone after it. The mixer has eight
    // voices and this is what an arcade does.
    m_ctx->audio().play(audio::SfxId::Explode);
    m_ctx->audio().play(audio::SfxId::Lose);
    return;
  }

  if (now == minesweeper::State::Won && before != minesweeper::State::Won) {
    m_ctx->audio().play(audio::SfxId::Win);
    // ⚠ THE ONLY PLACE A BEST TIME CAN BE RECORDED, and the reason it lives in
    // announce() rather than in the Board: this is the one function that sees
    // both the WIN TRANSITION and m_level. The Board holds only a Preset, never
    // a Level, and board.hpp is explicit that it must not learn one. All five
    // verb paths — three keyboard, two mouse — route through here, so a win by
    // click and a win by keypress cannot record differently.
    //
    // elapsed(), not seconds(): the stored record must not inherit the HUD's
    // 999-second clamp. Better::Lower, which is the direction a naive design gets
    // backwards and the reason a record carries one at all.
    m_ctx->scores().record(
        kMeta.slug, time_key(m_level),
        static_cast<long long>(m_board.elapsed().count()),
        scores::Better::Lower);
    return;
  }

  // Ordinary progress. Guarded on the count rather than on `changed` alone,
  // because cycle_mark() also reports a change and has its own sound.
  if (m_board.revealed_count() > revealed_before) {
    m_ctx->audio().play(audio::SfxId::Reveal);
  }
}

// A marking verb's sound, chosen by what the cell became. Flag is its own
// sound; clearing back to None, or stepping to Question, is a plain click —
// placing a flag is a decision and the other two are undoing one.
//
// ⚠ HERE the bool IS load-bearing, unlike in announce() above, and the
// asymmetry is the whole reason the two are separate functions. cycle_mark()
// refuses a revealed cell and leaves the mark at None — so without this guard
// that refusal would read as "became not-a-flag" and click. There is nothing
// else to compare against, because nothing about the board moved.
//
// test/15minesweeper-ui pins it ("marking a revealed cell is silent"); it was
// added after mutation testing showed the guard could be deleted with the suite
// still green.
auto Minesweeper::announce_mark(minesweeper::Coord at, bool changed) -> void {
  if (m_ctx == nullptr || !changed) return;
  m_ctx->audio().play(m_board.at(at).mark == minesweeper::Mark::Flag
                          ? audio::SfxId::Flag
                          : audio::SfxId::Click);
}

auto Minesweeper::handle_key(const termforge::KeyEvent& key) -> bool {
  using termforge::Key;

  // ⚠ Escape and 'p' are NOT bound, and must never be. Escape is the Shell's
  // quit-to-menu and 'p' is its pause; a game that consumes them strands the
  // player in it. Declining is how every game gets both for free.
  switch (key.key) {
    case Key::Up: move_cursor(-1, 0); return true;
    case Key::Down: move_cursor(1, 0); return true;
    case Key::Left: move_cursor(0, -1); return true;
    case Key::Right: move_cursor(0, 1); return true;
    case Key::Home: m_cursor.col = 0; return true;
    case Key::End: m_cursor.col = m_board.cols() - 1; return true;
    case Key::PageUp: m_cursor.row = 0; return true;
    case Key::PageDown: m_cursor.row = m_board.rows() - 1; return true;
    case Key::Enter:
      // On a finished board Enter accepts the result and hands control back —
      // the done() exit path. The hint line says so, because a key that means
      // two things has to be signposted.
      if (m_board.finished()) {
        m_done = true;
        return true;
      }
      {
        const auto before = m_board.state();
        const int seen = m_board.revealed_count();
        m_board.reveal(m_cursor);
        announce(before, seen);
      }
      return true;
    case Key::Char: break;
    default: return false;
  }

  switch (key.ch) {
    case U' ': {
      const auto before = m_board.state();
      const int seen = m_board.revealed_count();
      m_board.reveal(m_cursor);
      announce(before, seen);
      return true;
    }
    case U'k': case U'K': move_cursor(-1, 0); return true;
    case U'j': case U'J': move_cursor(1, 0); return true;
    case U'h': case U'H': move_cursor(0, -1); return true;
    case U'l': case U'L': move_cursor(0, 1); return true;
    case U'f': case U'F':
      announce_mark(m_cursor, m_board.cycle_mark(m_cursor));
      return true;
    case U'c': case U'C': {
      const auto before = m_board.state();
      const int seen = m_board.revealed_count();
      m_board.chord(m_cursor);
      announce(before, seen);
      return true;
    }
    // ⚠ NOT routed through announce(). new_game() resets the board, so the
    // state goes Playing -> Ready, which announce() would have to special-case
    // to avoid reading as progress.
    case U'n': case U'N': new_game(m_level); return true;
    case U'1': new_game(Level::Easy); return true;
    case U'2': new_game(Level::Medium); return true;
    case U'3': new_game(Level::Hard); return true;
    case U'q': case U'Q':
      // The quit_to_menu() exit path, called from INSIDE an event handler. That
      // placement is the point: it is the exact shape a synchronous callback
      // design would turn into a use-after-free, so CI's ASan and UBSan arms
      // execute it every run.
      if (m_ctx != nullptr) m_ctx->quit_to_menu();
      return true;
    default: return false;
  }
}

auto Minesweeper::handle_mouse(const termforge::MouseEvent& mouse) -> bool {
  // ⚠ Presses only. The terminal is in ?1002h button-event tracking, so a
  // RELEASE arrives as the same button with pressed == false, and so does
  // motion while a button is held. Without this guard a drag across the board
  // reveals a swath of cells and every click fires twice.
  if (!mouse.pressed) return false;
  if (mouse.button < 0) return false;  // wheel

  const auto hit = m_layout.cell_at(mouse.x, mouse.y);
  if (!hit) return false;

  // Any accepted click moves the keyboard cursor too. One line, and it is what
  // makes mouse and keyboard one input mode rather than two that disagree about
  // where you are.
  m_cursor = *hit;

  switch (mouse.button) {
    case 0:
      // ⚠ DIVERGENCE: left-click on a revealed number chords. The reference
      // (minesweeper/js/game.js) binds chording to auxclick alone, and most
      // trackpads have no middle button — which would make chording, a core
      // part of playing well, unreachable for most players. Left-click on a
      // revealed cell is otherwise a dead gesture, so binding it costs nothing.
      {
        const auto before = m_board.state();
        const int seen = m_board.revealed_count();
        if (m_board.at(*hit).revealed) {
          m_board.chord(*hit);
        } else {
          m_board.reveal(*hit);
        }
        announce(before, seen);
      }
      return true;
    case 1: {
      const auto before = m_board.state();
      const int seen = m_board.revealed_count();
      m_board.chord(*hit);
      announce(before, seen);
      return true;
    }
    case 2:
      announce_mark(*hit, m_board.cycle_mark(*hit));
      return true;
    default:
      return false;
  }
}

auto Minesweeper::draw(termforge::Screen& screen) -> void {
  // ⚠ Before the layout is computed, and before anything else is drawn. The
  // pre-start screen owns the whole Screen exactly as draw_too_small() does —
  // this is the same arm, not a new concept. Returning here is also what keeps
  // the status and hint rows off it: they describe a board that is not in play.
  if (m_options.is_open()) {
    m_options.draw(screen);
    return;
  }

  m_layout = minesweeper::compute_layout(screen.cols(), screen.rows(),
                                         m_board.rows(), m_board.cols());

  draw_status(screen);
  if (m_layout.fits) {
    m_frame.set_style(m_ctx != nullptr ? m_ctx->border_style()
                                       : termforge::BorderStyle::Ascii);
    m_frame.set_geometry({m_layout.frame_x, m_layout.frame_y, m_layout.frame_w,
                          m_layout.frame_h});
    m_frame.draw(screen);
    draw_grid(screen);
  } else {
    draw_too_small(screen);
  }
  draw_hints(screen);
}

auto Minesweeper::draw_status(termforge::Screen& screen) -> void {
  const auto bg = termforge::theme::kBg;

  // The state word is required, not decorative: at the no-colour tier "you
  // lost" cannot be said by painting the mines red. So it is measured and
  // reserved FIRST, and the counters are fitted into what is left.
  std::string_view word;
  termforge::Rgb fg = termforge::theme::kDim;
  switch (m_board.state()) {
    case State::Won:
      word = "YOU WIN";
      fg = termforge::theme::kFg;
      break;
    case State::Lost:
      word = "BOOM";
      fg = kMineFg;
      break;
    case State::Ready:
    case State::Playing:
      word = "PLAYING";
      break;
  }

  // ⚠ THIS BUDGET IS 2048's, adopted here rather than re-argued — the reasoning
  // is at the top of twenty48.cpp's draw_status() and it applies unchanged:
  // write_text clips at the screen edge but NOT against text already on the row,
  // so fields are appended only while they still fit and the row degrades by
  // dropping WHOLE fields rather than truncating a number.
  //
  // ⚠ It REPLACES a guard that had the priority inverted. This row used to build
  // its left half unconditionally and then skip the WORD when the two would
  // collide — so adding a fourth field here would have made "YOU WIN" disappear
  // on a narrow terminal, which is the one thing on this row that must survive.
  // The word now wins by construction instead of by luck about widths.
  // ⚠ The budget arithmetic that used to live here is hud::draw_status_row.
  // Extracted for COVERAGE, not tidiness: killing the "delete the budget"
  // mutation needs a sweep of widths narrower than this game's own floor, and
  // writing that four times is why it went green in two consecutive epics.
  // test/33options sweeps it once, against the helper.
  //
  // ⚠ The ORDER of this list is still the priority order -- the helper appends
  // until a field does not fit and then stops -- and no label may be a
  // substring of another, because the whole-fields checks key off find(label).
  const std::array<std::string, 4> fields{
      "MINES " + pad3(m_board.mines_remaining()),
      "TIME " + pad3(m_board.seconds()),
      std::string(m_board.name()),
      "BEST " + best_time(),
  };
  hud::draw_status_row(screen, m_layout.status_y, fields, word,
                       termforge::theme::kFg, fg, bg);
}

// ⚠ "---" for no record, NOT "000", and the asymmetry with 2048's `record 0` is
// forced rather than stylistic. This is a Better::Lower record, so zero is the
// MAXIMAL value: "BEST 000" would read as a nonexistent 0-second win that no
// honest game can ever beat. 2048's Higher record has 0 as its identity and a
// real minimum score, so there it needs no unset spelling at all.
auto Minesweeper::best_time() const -> std::string {
  if (m_ctx == nullptr) return "---";
  const auto held = m_ctx->scores().get(kMeta.slug, time_key(m_level));
  // pad3 clamps at 999, which is the same freeze TIME already has — a record
  // above the cap is stored in full and displayed frozen.
  return held ? pad3(static_cast<int>(*held)) : "---";
}

auto Minesweeper::draw_hints(termforge::Screen& screen) -> void {
  const auto fg = termforge::theme::kDim;
  const auto bg = termforge::theme::kBg;
  const int y = m_layout.hint_y;
  if (y <= m_layout.status_y) return;

  // Three widths. write_text clips, but a hint clipped mid-word reads like a
  // rendering bug rather than a narrow terminal.
  std::string_view hint;
  if (m_board.finished()) {
    hint = "Enter leave  N new  1/2/3 level  Esc menu";
  } else if (screen.cols() >= 72) {
    hint = "Arrows move  Space reveal  F flag  C chord  N new  1/2/3 level  Esc menu";
  } else if (screen.cols() >= 40) {
    hint = "Arrows/Space/F/C  N new  Esc menu";
  } else {
    hint = "Space F C  Esc menu";
  }
  screen.write_text(0, y, hint, fg, bg);
}

auto Minesweeper::draw_too_small(termforge::Screen& screen) -> void {
  const auto fg = termforge::theme::kFg;
  const auto bg = termforge::theme::kBg;
  const int mid = screen.rows() / 2;

  // Name both numbers. "Too small" without the requirement leaves the player
  // resizing blind, and the level keys stay live so they can pick one that fits
  // instead — the board is NOT silently downgraded for them.
  const std::string need =
      std::string(m_board.name()) + " needs " +
      std::to_string(minesweeper::needed_cols(m_board.cols())) + "x" +
      std::to_string(minesweeper::needed_rows(m_board.rows())) + ", this is " +
      std::to_string(screen.cols()) + "x" + std::to_string(screen.rows());
  const std::string alt = "1 easy 21x13   2 medium 35x20   3 hard 63x20";

  const int nx = std::max(0, (screen.cols() - static_cast<int>(need.size())) / 2);
  const int ax = std::max(0, (screen.cols() - static_cast<int>(alt.size())) / 2);
  if (mid > m_layout.status_y) screen.write_text(nx, mid, need, fg, bg);
  if (mid + 1 < m_layout.hint_y) {
    screen.write_text(ax, mid + 1, alt, termforge::theme::kDim, bg);
  }
}

auto Minesweeper::draw_grid(termforge::Screen& screen) -> void {
  const bool ascii = m_ctx == nullptr ||
                     termforge::is_ascii(m_ctx->border_style());
  const minesweeper::TileGlyphs& tiles = minesweeper::tiles_for(ascii);
  const auto bg = termforge::theme::kBg;

  for (int r = 0; r < m_board.rows(); ++r) {
    const int y = m_layout.row_y(r);
    for (int c = 0; c < m_board.cols(); ++c) {
      const Coord p{.row = r, .col = c};
      const minesweeper::Tile tile = minesweeper::tile_for(m_board, p);
      const std::string_view glyph = minesweeper::glyph_for(tiles, tile);

      termforge::Rgb fg = termforge::theme::kFg;
      if (!ascii) {
        // Colour only ever reinforces the glyph — it never carries information
        // the glyph does not already carry. See the distinctness static_assert
        // in glyphs.hpp.
        switch (tile) {
          case minesweeper::Tile::Mine:
          case minesweeper::Tile::Exploded:
          case minesweeper::Tile::WrongFlag:
            fg = kMineFg;
            break;
          case minesweeper::Tile::Flag:
            fg = kFlagFg;
            break;
          case minesweeper::Tile::Hidden:
            fg = termforge::theme::kDim;
            break;
          default: {
            const auto n = static_cast<int>(tile);
            const auto first = static_cast<int>(minesweeper::Tile::N1);
            if (n >= first && n <= static_cast<int>(minesweeper::Tile::N8)) {
              const minesweeper::Rgb8 col =
                  minesweeper::kNumberColors[static_cast<std::size_t>(n - first)];
              fg = termforge::Rgb{col.r, col.g, col.b};
            }
            break;
          }
        }
      }

      screen.write_text(m_layout.gutter_x(c), y, " ", termforge::theme::kFg, bg);
      screen.write_text(m_layout.glyph_x(c), y, glyph, fg, bg);
    }
    // The trailing bracket column, blanked so a cursor that has moved away does
    // not leave its ']' behind.
    screen.write_text(m_layout.gutter_x(m_board.cols()), y, " ",
                      termforge::theme::kFg, bg);
  }

  // ⚠ The cursor is a PAIR OF CHARACTERS, not a colour. FallbackDriver discards
  // colour entirely, so a highlight-based cursor is invisible at exactly the
  // tier this repo promises always works — the same reasoning termforge applied
  // to ListWidget's own selection marker in v0.1.11 (#72).
  //
  // This one is not a workaround and has no deletion condition: a cursor over a
  // grid of cells is Minesweeper's own affordance, not a missing framework
  // feature. The Shell used to carry a marker that WAS a workaround, and it is
  // gone (term-game#17) — do not read the two as the same thing and delete this.
  //
  // Both brackets land inside the frame by construction: the furthest is
  // gutter_x(cols-1) + 2 == origin_x + 2*cols, which is the trailing column.
  const int cy = m_layout.row_y(m_cursor.row);
  screen.write_text(m_layout.gutter_x(m_cursor.col), cy, "[", kCursorFg, bg);
  screen.write_text(m_layout.gutter_x(m_cursor.col + 1), cy, "]", kCursorFg, bg);
}

}  // namespace glyphcade
