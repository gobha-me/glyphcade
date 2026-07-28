#include <termgame/games/minesweeper/minesweeper.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <variant>

#include <termforge/widgets/theme.hpp>

#include <termgame/games/minesweeper/glyphs.hpp>

namespace termgame {
namespace {

using minesweeper::Coord;
using minesweeper::Level;
using minesweeper::State;

constexpr termforge::Rgb kMineFg{0xEF, 0x44, 0x44};
constexpr termforge::Rgb kFlagFg{0xF5, 0x9E, 0x0B};
constexpr termforge::Rgb kCursorFg{0x40, 0xE0, 0xFF};

// Three columns, sign-aware. No <format> and no stringstream — this file is on
// the render path and the repo has no formatting dependency (the same reason
// StubGame hand-rolled fixed2()).
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
}

auto Minesweeper::move_cursor(int dr, int dc) -> void {
  // Clamped, not wrapped: on a grid you are reasoning about spatially, wrapping
  // from the last column to the first is a teleport.
  m_cursor.row = std::clamp(m_cursor.row + dr, 0, m_board.rows() - 1);
  m_cursor.col = std::clamp(m_cursor.col + dc, 0, m_board.cols() - 1);
}

auto Minesweeper::on_event(const termforge::Event& ev) -> bool {
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
      m_board.reveal(m_cursor);
      return true;
    case Key::Char: break;
    default: return false;
  }

  switch (key.ch) {
    case U' ':
      m_board.reveal(m_cursor);
      return true;
    case U'k': case U'K': move_cursor(-1, 0); return true;
    case U'j': case U'J': move_cursor(1, 0); return true;
    case U'h': case U'H': move_cursor(0, -1); return true;
    case U'l': case U'L': move_cursor(0, 1); return true;
    case U'f': case U'F': m_board.cycle_mark(m_cursor); return true;
    case U'c': case U'C': m_board.chord(m_cursor); return true;
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
      if (m_board.at(*hit).revealed) {
        m_board.chord(*hit);
      } else {
        m_board.reveal(*hit);
      }
      return true;
    case 1:
      m_board.chord(*hit);
      return true;
    case 2:
      m_board.cycle_mark(*hit);
      return true;
    default:
      return false;
  }
}

auto Minesweeper::draw(termforge::Screen& screen) -> void {
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
  std::string left = "MINES " + pad3(m_board.mines_remaining()) + "   TIME " +
                     pad3(m_board.seconds()) + "   ";
  left += std::string(m_board.name());
  screen.write_text(0, m_layout.status_y, left, termforge::theme::kFg, bg);

  // The state word is required, not decorative: at the no-colour tier "you
  // lost" cannot be said by painting the mines red.
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
  const int x = screen.cols() - static_cast<int>(word.size());
  if (x > static_cast<int>(left.size())) {
    screen.write_text(x, m_layout.status_y, word, fg, bg);
  }
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
  // tier this repo promises always works — the same failure the Shell's
  // selector gutter marker exists to work around (termforge #72). Both brackets
  // land inside the frame by construction: the furthest is
  // gutter_x(cols-1) + 2 == origin_x + 2*cols, which is the trailing column.
  const int cy = m_layout.row_y(m_cursor.row);
  screen.write_text(m_layout.gutter_x(m_cursor.col), cy, "[", kCursorFg, bg);
  screen.write_text(m_layout.gutter_x(m_cursor.col + 1), cy, "]", kCursorFg, bg);
}

}  // namespace termgame
