#include <glyphcade/games/twenty48/twenty48.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>

#include <termforge/widgets/theme.hpp>

#include <glyphcade/arcade/hud.hpp>
#include <glyphcade/games/twenty48/glyphs.hpp>

namespace glyphcade {

namespace {

// Reading a clock in a CONSTRUCTOR is fine; reading one inside Game::tick() is
// not, and that is the rule AGENTS.md states — dt is the only time a game may
// see, which is what makes it drivable by N ticks with no TTY. Same helper, same
// reason, as minesweeper.cpp.
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

constexpr termforge::Rgb kWinFg{0xF5, 0x9E, 0x0B};
constexpr termforge::Rgb kLoseFg{0xEF, 0x44, 0x44};

}  // namespace

Twenty48::Twenty48() : m_board(entropy()), m_seed(entropy()) {
  m_anim.rest(m_board.cells());
}

auto Twenty48::start(GameContext& ctx) -> void {
  m_ctx = &ctx;
  m_frame.set_style(ctx.border_style());
  // Nothing is reset here, deliberately: the Shell builds a fresh Game per menu
  // entry, so freshness is structural rather than something start() has to
  // remember. See arcade/game.hpp.
}

auto Twenty48::new_game() -> void {
  // Advance the seed from the existing one rather than re-reading the clock, so a
  // test that fixes the first seed gets a reproducible *sequence* of boards.
  Rng r(m_seed);
  m_seed = r.next();
  m_board.reset(m_seed);
  m_anim.rest(m_board.cells());
  if (m_ctx != nullptr) {
    // Click, not announce(): reset moves Playing -> Playing with a fresh board,
    // which announce() would read as neither progress nor an outcome. Minesweeper
    // makes the same call at the same place for the same reason.
    m_ctx->audio().play(audio::SfxId::Click);
  }
}

auto Twenty48::load(std::span<const int> cells, int score) -> void {
  m_board.load(cells, score);
  // The half that board().load() cannot do for itself — see the note in the
  // header. Resting the Anim is what makes the next frame show what was loaded.
  m_anim.rest(m_board.cells());
}

auto Twenty48::tick(std::chrono::duration<double> dt) -> void {
  ++m_ticks;
  m_elapsed += dt;
  m_anim.advance(dt);
}

auto Twenty48::apply(twenty48::Dir d) -> bool {
  if (m_board.finished()) {
    // Lost. The board cannot change, so a direction key is a no-op — but it is
    // still consumed, because Escape is the only way out and the Shell owns it.
    return true;
  }

  // ⚠ Input is never queued and never blocked. A direction arriving mid-slide
  // snaps the running animation to its resting state and then resolves the new
  // move, so N moves produce the same board whether they arrive one per frame or
  // all within one frame. Queueing would make the animation a participant in the
  // rules, which AGENTS.md forbids; blocking would make the game feel laggy under
  // fast input, which is the thing 2048 is actually judged on.
  //
  // test/22twenty48 pins this: ten moves with zero ticks between them equal ten
  // moves with a full animation between them.
  if (!m_anim.done()) {
    m_anim.finish();
  }

  const auto before = m_board.state();
  const int moves_before = m_board.moves();
  const int score_before = m_board.score();

  const auto result = m_board.move(d);
  m_anim.begin(result, m_board.cells());
  announce(before, moves_before, score_before);
  record_best();
  return true;
}

// ⚠ Called here and NOT from announce(), even though announce() is looking at the
// same transition one line up. announce() returns early on the Lost branch and
// again on the Won branch, so a record placed inside it would lose the losing and
// the winning move — the two scores a player most wants kept. Sound and
// persistence share a trigger by coincidence, not by rule.
//
// ⚠ And NOT from the undo path either, which is the interesting half. Undo
// restores the pre-move score, so Board::score() genuinely goes DOWN — but
// Store::record() is monotone, so a lower value cannot displace the record it
// already holds. A call in undo would be unremovable-looking decoration: delete
// it and no test changes. The monotonicity is doing the work, which is why the
// test for it asserts on the store after an undo rather than on this call site.
auto Twenty48::record_best() -> void {
  if (m_ctx == nullptr) {
    return;
  }
  m_ctx->scores().record(kMeta.slug, "best_score", m_board.score(),
                         scores::Better::Higher);
  // Persisted but not shown: the status row has no width for a fifth field, and
  // "best" there is already the LIVE maximum tile. Kept anyway because two keys
  // under one slug is the concrete form of "a record is not one integer", and
  // because it costs a line — not because anything reads it yet.
  m_ctx->scores().record(kMeta.slug, "best_tile", m_board.best_tile(),
                         scores::Better::Higher);
}

auto Twenty48::best_score() const -> int {
  if (m_ctx == nullptr) {
    return 0;
  }
  return static_cast<int>(
      m_ctx->scores().get(kMeta.slug, "best_score").value_or(0));
}

auto Twenty48::announce(twenty48::State before, int moves_before,
                        int score_before) -> void {
  if (m_ctx == nullptr) {
    return;
  }

  // ⚠ Every decision below is read from the BOARD, not from MoveResult, and that
  // is deliberate rather than roundabout. minesweeper.cpp's announce() carried the
  // verb's bool until mutation testing showed the state comparisons already
  // subsumed it — a second, weaker statement of the same fact, which later reads
  // as load-bearing. So:
  //
  //   * "did anything happen"  ->  moves() only increments on a legal move;
  //   * "did anything merge"   ->  score() only increases on a merge.
  //
  // Both are exact, not approximations. Passing result.moved and result.merges
  // would be that same redundancy reintroduced.
  const auto now = m_board.state();

  if (now == twenty48::State::Lost && before != twenty48::State::Lost) {
    m_ctx->audio().play(audio::SfxId::Lose);
    return;
  }

  if (now == twenty48::State::Won && before != twenty48::State::Won) {
    // Win alone, not Win over the merge that caused it. The winning move is
    // necessarily a merge, so Merge would always be masked by Win a millisecond
    // later — and minesweeper's Win path is a single sound too.
    m_ctx->audio().play(audio::SfxId::Win);
    return;
  }

  if (m_board.moves() == moves_before) {
    // A direction that changed nothing is SILENT, matching minesweeper: there is
    // no deny blip in the bank and inventing one is a feel decision nobody who
    // cannot hear it should make.
    return;
  }

  m_ctx->audio().play(m_board.score() > score_before ? audio::SfxId::Merge
                                                     : audio::SfxId::Slide);
}

auto Twenty48::on_event(const termforge::Event& ev) -> bool {
  if (const auto* key = std::get_if<termforge::KeyEvent>(&ev)) {
    return handle_key(*key);
  }
  // No mouse gesture. 2048 is four directions and an undo; there is nothing a
  // click could mean that a key does not already say, and inventing one
  // (click-a-column-to-slide?) is a feel decision with no reference behind it.
  // Declining is how a game says "not mine" — the Shell ignores the return value
  // in-game, but the honest answer is still false.
  //
  // Resize needs no handling: the layout is recomputed from screen.cols()/rows()
  // in every draw().
  return false;
}

auto Twenty48::handle_key(const termforge::KeyEvent& key) -> bool {
  using termforge::Key;

  // ⚠ Escape and 'p' are never bound here, and that absence is load-bearing:
  // Escape is the Shell's quit-to-menu and 'p' is its pause. A game that consumed
  // either would strand the player inside it. Same rule, same comment, as
  // minesweeper.cpp.
  switch (key.key) {
    case Key::Left:
      return apply(twenty48::Dir::Left);
    case Key::Right:
      return apply(twenty48::Dir::Right);
    case Key::Up:
      return apply(twenty48::Dir::Up);
    case Key::Down:
      return apply(twenty48::Dir::Down);
    default:
      break;
  }

  switch (key.ch) {
    // hjkl for vi hands, wasd for the reference's bindings (game.js:337). Both,
    // because both cost one line and neither is obviously the right one.
    case 'h':
    case 'a':
      return apply(twenty48::Dir::Left);
    case 'l':
    case 'd':
      return apply(twenty48::Dir::Right);
    case 'k':
    case 'w':
      return apply(twenty48::Dir::Up);
    case 'j':
    case 's':
      return apply(twenty48::Dir::Down);

    case 'u': {
      // The reference binds Ctrl+Z and matches lowercase 'z' only, so Ctrl+Shift+Z
      // silently does nothing there. A bare 'u' has no modifier to get wrong, and
      // this is a terminal — Ctrl+Z is the shell's job.
      if (!m_board.undo()) {
        return true;  // consumed; nothing to undo is not the Shell's business
      }
      m_anim.rest(m_board.cells());
      if (m_ctx != nullptr) {
        m_ctx->audio().play(audio::SfxId::Click);
      }
      return true;
    }

    case 'n':
      new_game();
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

auto Twenty48::draw(termforge::Screen& screen) -> void {
  // ONE Layout per frame, used by this draw and by any hit-testing, so the two
  // cannot derive coordinates separately. See layout.hpp.
  m_layout = twenty48::compute_layout(screen.cols(), screen.rows());

  draw_status(screen);

  if (m_layout.fits) {
    m_frame.set_style(m_ctx != nullptr ? m_ctx->border_style()
                                       : termforge::BorderStyle::Ascii);
    m_frame.set_geometry(
        {m_layout.frame_x, m_layout.frame_y, m_layout.frame_w, m_layout.frame_h});
    m_frame.draw(screen);
    draw_grid(screen);
  } else {
    draw_too_small(screen);
  }

  draw_hints(screen);
}

auto Twenty48::draw_status(termforge::Screen& screen) -> void {
  const auto bg = termforge::theme::kBg;

  // ⚠ The outcome is a WORD, not a colour. FallbackDriver discards colour, so
  // "you won" cannot be said by painting a tile amber — the same reason
  // minesweeper writes "YOU WIN" and "BOOM" rather than relying on the mine
  // colour. Colour below is reinforcement on top of the word.
  std::string_view word = "PLAYING";
  auto fg = termforge::theme::kDim;
  switch (m_board.state()) {
    case twenty48::State::Won:
      word = "2048 REACHED";  // and play continues — see State in board.hpp
      fg = kWinFg;
      break;
    case twenty48::State::Lost:
      word = "NO MOVES LEFT";
      fg = kLoseFg;
      break;
    case twenty48::State::Playing:
      break;
  }

  // ⚠ THE BUDGET is what stops the two halves of this row colliding, and it is
  // the load-bearing part. Screen::write_text clips at the screen edge but NOT
  // against text already on the row, so an unbounded left-hand string produced
  // `movesPLAYING` at 40 columns — observed in a headless render, not theorised.
  //
  // Fields are appended only while they still fit inside word_x, so the row
  // degrades by dropping WHOLE fields rather than truncating a number. That
  // distinction matters: a missing field reads as a narrow terminal, while a
  // half-written one reads as a wrong score.
  //
  // ⚠ Drawing the word AFTER the counters is deliberate but is NOT the fix, and
  // mutation testing is what established the difference — swapping the two draws
  // changes nothing, because the budget already guarantees they cannot overlap.
  // The order only decides which text loses if the budget arithmetic is ever
  // wrong, and the word must win: at the bottom tier it is the ONLY carrier of
  // win and loss, whereas a clipped counter is merely ugly. So this is a chosen
  // failure mode, not a second guard — do not read it as one.
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
      "score " + num(m_board.score()),
      "best " + num(m_board.best_tile()),
      "moves " + num(m_board.moves()),
      "record " + num(best_score()),
  };
  hud::draw_status_row(screen, m_layout.status_y, fields, word,
                       termforge::theme::kFg, fg, bg);
}

auto Twenty48::draw_hints(termforge::Screen& screen) -> void {
  const auto bg = termforge::theme::kBg;
  const int y = m_layout.hint_y;
  if (y <= m_layout.status_y) {
    return;
  }

  // Three widths. write_text clips, but a hint clipped mid-word reads like a bug,
  // so each width gets its own string — same approach as minesweeper's.
  std::string_view hint;
  if (screen.cols() >= 72) {
    hint = "Arrows/hjkl/wasd move  u undo  n new  q menu  p pause  Esc menu";
  } else if (screen.cols() >= 40) {
    hint = "Arrows move  u undo  n new  Esc menu";
  } else {
    hint = "Arrows  u undo  Esc";
  }
  screen.write_text(0, y, hint, termforge::theme::kDim, bg);
}

auto Twenty48::draw_grid(termforge::Screen& screen) -> void {
  const bool ascii =
      m_ctx == nullptr || termforge::is_ascii(m_ctx->border_style());
  const auto& lat = twenty48::lattice_for(ascii);
  const auto bg = termforge::theme::kBg;

  // ── The lattice first, then the tiles over it ──────────────────────────────
  //
  // At the colour tier an empty cell is a filled block, so the grid is legible
  // from the fills alone. At the ASCII tier there are no fills, and four blank
  // cells with blank gaps between them is an empty rectangle — the player cannot
  // see where the board is. So the gaps carry rule glyphs, which cost nothing at
  // the colour tier and are the only thing making the bottom tier playable.
  for (int r = 0; r < twenty48::kSize; ++r) {
    for (int c = 0; c < twenty48::kSize; ++c) {
      const int tx = m_layout.tile_x(c);
      const int ty = m_layout.tile_y(r);

      if (!ascii) {
        constexpr termforge::Rgb kEmpty{twenty48::kEmptyBg.r, twenty48::kEmptyBg.g,
                                        twenty48::kEmptyBg.b};
        screen.fill_rect(tx, ty, twenty48::kTileCols, twenty48::kTileRows,
                         termforge::theme::kFg, kEmpty);
      }

      // The gap column to this tile's right, and the gap row below it.
      if (c + 1 < twenty48::kSize) {
        for (int dy = 0; dy < twenty48::kTileRows; ++dy) {
          screen.write_text(tx + twenty48::kTileCols, ty + dy, lat.vertical,
                            termforge::theme::kDim, bg);
        }
      }
      if (r + 1 < twenty48::kSize) {
        for (int dx = 0; dx < twenty48::kTileCols; ++dx) {
          screen.write_text(tx + dx, ty + twenty48::kTileRows, lat.horizontal,
                            termforge::theme::kDim, bg);
        }
        if (c + 1 < twenty48::kSize) {
          screen.write_text(tx + twenty48::kTileCols, ty + twenty48::kTileRows,
                            lat.cross, termforge::theme::kDim, bg);
        }
      }
    }
  }

  // ⚠ Tiles come from the ANIMATION, always — never from m_board directly, not
  // even at rest. A finished Anim holds exactly the resting board at integer
  // positions, so there is one code path and it cannot disagree with itself. Two
  // paths to the same pixels drift; see the note on Anim::tiles().
  for (const auto& t : m_anim.tiles()) {
    draw_tile(screen, t, ascii);
  }
}

auto Twenty48::draw_tile(termforge::Screen& screen,
                         const twenty48::DrawTile& tile, bool ascii) -> void {
  const int x = m_layout.tile_x(tile.col);
  const int y = m_layout.tile_y(tile.row);
  const auto label = twenty48::label_for(tile.value);

  const auto colors = twenty48::color_for(tile.value);
  const termforge::Rgb bg{colors.bg.r, colors.bg.g, colors.bg.b};
  const termforge::Rgb fg{colors.fg.r, colors.fg.g, colors.fg.b};

  if (ascii) {
    // No fill: the tier has no colour, so a "filled" tile would just be blanks
    // that erase the lattice. The number alone is the tile, centred in the field,
    // and the lattice around it says where the cell is.
    const int pad = (twenty48::kTileCols - label.len) / 2;
    screen.write_text(x + pad, y + twenty48::kTileRows / 2, label.view(),
                      termforge::theme::kFg, termforge::theme::kBg);
    return;
  }

  // ⚠ The pop is expressed in COLOUR ONLY, and that is a deliberate limit rather
  // than an oversight. A character cell cannot scale a glyph, and the obvious
  // alternatives — brackets around the number, a wider field — either shift the
  // layout or do not fit: a merge can produce a 6-digit label in a 6-column tile,
  // leaving no room for decoration. So a merged or spawned tile flashes its
  // foreground and background inverted at the peak, and at the ASCII tier the pop
  // is simply absent. Nothing is lost there: the number changed, which is the
  // information. The pop is emphasis.
  const bool flash = tile.pop > 0.5;
  screen.fill_rect(x, y, twenty48::kTileCols, twenty48::kTileRows,
                   flash ? bg : fg, flash ? fg : bg);

  const int pad = (twenty48::kTileCols - label.len) / 2;
  screen.write_text(x + pad, y + twenty48::kTileRows / 2, label.view(),
                    flash ? bg : fg, flash ? fg : bg);
}

auto Twenty48::draw_too_small(termforge::Screen& screen) -> void {
  const auto bg = termforge::theme::kBg;
  const auto fg = termforge::theme::kFg;

  // Same answer minesweeper gives. term-game#15 landed and did NOT retire this:
  // GameMeta now carries the size and the selector warns about it, but it never
  // refuses, so a player who presses Enter anyway still arrives here and the
  // game still has to say so itself.
  const std::string need = "2048 needs " + num(twenty48::needed_cols()) + "x" +
                           num(twenty48::needed_rows());
  const int mid = screen.rows() / 2;
  const int nx = std::max(0, (screen.cols() - static_cast<int>(need.size())) / 2);
  if (mid > m_layout.status_y) {
    screen.write_text(nx, mid, need, fg, bg);
  }
}

}  // namespace glyphcade
