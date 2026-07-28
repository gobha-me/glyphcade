// Minesweeper's RENDERING and INPUT, through a real Shell into an offscreen
// Screen. The rules live in test/14minesweeper and are not re-tested here.
//
// ⚠ test_run_frames installs a FallbackDriver, whose capabilities() reports
// all-false — so the Shell syncs to BorderStyle::Ascii and EVERY case in this
// file exercises the bottom tier. That is deliberate and it is the tier that
// matters: it is what AGENTS.md promises always works, it is the only tier CI
// can reach, and it is the tier where colour does not exist and a glyph is the
// only thing telling a flag from a mine.
//
// The pattern (Probe, set_frame_ms(0), read back screen()) is test/10render's.
// Boards are installed with load_mines() through the game's own board()
// accessor, so a case asserts what a click does and not what the RNG dealt.
//
// ⚠ NEVER hold a Screen& across a step(). App::test_run_frames does
// `m_screen = std::make_unique<Screen>(cols, rows)` on EVERY call, so a
// reference taken from screen() before a frame dangles after it — as a
// segfault, not a wrong value. Every helper below therefore takes the Probe and
// fetches the Screen itself. A Layout& from the game is fine: that one lives in
// the Game object, which the Shell keeps alive for the whole entry.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

#include <termforge/core/types.hpp>

#include <termgame/arcade/registry.hpp>
#include <termgame/arcade/shell.hpp>
#include <termgame/games/minesweeper/glyphs.hpp>
#include <termgame/games/minesweeper/minesweeper.hpp>

namespace {

using termgame::Minesweeper;
using termgame::Shell;
using namespace termgame::minesweeper;

class Probe final : public Shell {
 public:
  using Shell::screen;

  Probe() { set_frame_ms(0); }

  auto step(int frames = 1, int cols = 80, int rows = 24) -> void {
    test_run_frames(frames, cols, rows, &m_sink);
  }

 private:
  std::string m_sink;
};

[[nodiscard]] auto key(termforge::Key k) -> termforge::Event {
  return termforge::Event{termforge::KeyEvent{.key = k}};
}
[[nodiscard]] auto ch(char32_t c) -> termforge::Event {
  return termforge::Event{
      termforge::KeyEvent{.key = termforge::Key::Char, .ch = c}};
}
[[nodiscard]] auto click(int x, int y, int button = 0, bool pressed = true)
    -> termforge::Event {
  return termforge::Event{termforge::MouseEvent{
      .x = x, .y = y, .button = button, .pressed = pressed}};
}

[[nodiscard]] auto minesweeper_index() -> int {
  const auto games = termgame::all_games();
  for (std::size_t i = 0; i < games.size(); ++i) {
    if (games[i].meta.slug == "minesweeper") return static_cast<int>(i);
  }
  return -1;
}

[[nodiscard]] auto game_of(Shell& shell) -> Minesweeper* {
  return dynamic_cast<Minesweeper*>(const_cast<termgame::Game*>(
      static_cast<const termgame::Game*>(shell.current_game())));
}

auto enter_game(Probe& app, int cols = 80, int rows = 24) -> Minesweeper* {
  app.step(1, cols, rows);  // give the list its geometry
  const int index = minesweeper_index();
  REQUIRE(index >= 0);
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);
  app.step(1, cols, rows);  // one frame so the layout exists for hit-testing
  Minesweeper* g = game_of(app);
  REQUIRE(g != nullptr);
  return g;
}

[[nodiscard]] auto row_text(Probe& app, int y) -> std::string {
  auto& s = app.screen();
  std::string out;
  for (int x = 0; x < s.cols(); ++x) out += s.at(x, y).text;
  return out;
}

[[nodiscard]] auto screen_contains(Probe& app, std::string_view needle) -> bool {
  for (int y = 0; y < app.screen().rows(); ++y) {
    if (row_text(app, y).find(needle) != std::string::npos) return true;
  }
  return false;
}

[[nodiscard]] auto glyph_at(Probe& app, const Layout& l, Coord p) -> std::string {
  return app.screen().at(l.glyph_x(p.col), l.row_y(p.row)).text;
}

[[nodiscard]] auto cell_text(Probe& app, int x, int y) -> std::string {
  return app.screen().at(x, y).text;
}

// Every byte of every cell on the screen must be 7-bit.
[[nodiscard]] auto all_seven_bit(Probe& app) -> bool {
  auto& s = app.screen();
  for (int y = 0; y < s.rows(); ++y) {
    for (int x = 0; x < s.cols(); ++x) {
      for (const char c : s.at(x, y).text) {
        if (static_cast<unsigned char>(c) >= 0x80) return false;
      }
    }
  }
  return true;
}

}  // namespace

// ─── The floor is literally 7-bit ──────────────────────────────────────────

TEST_CASE("nothing the game paints leaves 7-bit ASCII", "[minesweeper]") {
  // ⚠ The highest-value case in this file. glyphs.hpp static_asserts that the
  // ASCII TABLE is 7-bit; this asserts that the ASCII table is what actually
  // reaches the Screen — including the frame, the HUD, the hints and the
  // cursor. The two are not the same check: picking the wrong table, or
  // hardcoding a glyph at a call site, passes the first and fails this.
  Probe app;
  Minesweeper* g = enter_game(app);

  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);

  app.step();
  REQUIRE(all_seven_bit(app));

  // Marks.
  g->board().cycle_mark({.row = 2, .col = 2});
  app.step();
  REQUIRE(all_seven_bit(app));
  g->board().cycle_mark({.row = 2, .col = 2});  // -> Question
  app.step();
  REQUIRE(all_seven_bit(app));

  // Numbers and an opened region.
  g->board().reveal({.row = 1, .col = 1});
  app.step();
  REQUIRE(all_seven_bit(app));

  // A lost board: mines, the detonated one, and a wrong flag.
  g->board().cycle_mark({.row = 6, .col = 6});
  g->board().reveal({.row = 4, .col = 4});
  REQUIRE(g->board().state() == State::Lost);
  app.step();
  REQUIRE(all_seven_bit(app));

  // Every difficulty, including the one that does not fit.
  for (const char32_t c : {U'1', U'2', U'3'}) {
    app.dispatch_event(ch(c));
    app.step();
    REQUIRE(all_seven_bit(app));
  }
}

TEST_CASE("every cell state paints a distinct glyph", "[minesweeper]") {
  // The runtime half of glyphs.hpp's distinctness static_assert: at a tier with
  // no colour, two states that look the same are unplayable.
  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {8, 8}};
  g->board().load_mines(mines);

  // Hidden, Flag and Question are asserted BEFORE anything is revealed. A zero
  // region on a two-mine board floods almost the whole grid, so a cell chosen
  // as "still hidden" afterwards would not be.
  g->board().cycle_mark({.row = 3, .col = 0});  // Flag
  g->board().cycle_mark({.row = 3, .col = 1});
  g->board().cycle_mark({.row = 3, .col = 1});  // Question
  app.step();

  const Layout& l = g->layout();
  REQUIRE(glyph_at(app, l, {.row = 6, .col = 0}) == "#");
  REQUIRE(glyph_at(app, l, {.row = 3, .col = 0}) == "F");
  REQUIRE(glyph_at(app, l, {.row = 3, .col = 1}) == "?");

  g->board().reveal({.row = 1, .col = 1});  // a number: touches (0,0)
  g->board().reveal({.row = 5, .col = 5});  // a zero: opens a region
  app.step();
  REQUIRE(glyph_at(app, l, {.row = 1, .col = 1}) == "1");
  REQUIRE(glyph_at(app, l, {.row = 5, .col = 5}) == " ");
  // The flag survived the flood — it is what stopped it — so the board is not
  // won and the marks still mean what they meant.
  REQUIRE(g->board().state() == State::Playing);
  REQUIRE(glyph_at(app, l, {.row = 3, .col = 0}) == "F");

  // A lost board adds the mine, the detonated mine and the wrong flag. The flag
  // at (3,0) is the wrong flag: there is no mine under it, which is exactly why
  // it was able to stop the flood and leave a hole in an opened region.
  g->board().reveal({.row = 0, .col = 0});
  REQUIRE(g->board().state() == State::Lost);
  app.step();
  REQUIRE(glyph_at(app, l, {.row = 0, .col = 0}) == "@");  // exploded
  REQUIRE(glyph_at(app, l, {.row = 8, .col = 8}) == "*");  // the other mine
  REQUIRE(glyph_at(app, l, {.row = 3, .col = 0}) == "X");  // wrong flag
}

// ─── Mouse ─────────────────────────────────────────────────────────────────

TEST_CASE("a left click reveals the cell that was clicked", "[minesweeper]") {
  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);
  app.step();

  const Layout& l = g->layout();
  const Coord target{.row = 1, .col = 1};
  app.dispatch_event(click(l.glyph_x(target.col), l.row_y(target.row)));
  REQUIRE(g->board().at(target).revealed);
  // And the keyboard cursor followed the pointer.
  REQUIRE(g->cursor() == target);

  // The gutter column belongs to the same cell — half the grid would be dead to
  // the mouse otherwise.
  const Coord second{.row = 2, .col = 3};
  app.dispatch_event(click(l.gutter_x(second.col), l.row_y(second.row)));
  REQUIRE(g->cursor() == second);
}

TEST_CASE("a right click cycles the mark", "[minesweeper]") {
  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}};
  g->board().load_mines(mines);
  app.step();

  const Layout& l = g->layout();
  const Coord p{.row = 5, .col = 5};
  const int x = l.glyph_x(p.col);
  const int y = l.row_y(p.row);

  app.dispatch_event(click(x, y, 2));
  REQUIRE(g->board().at(p).mark == Mark::Flag);
  app.dispatch_event(click(x, y, 2));
  REQUIRE(g->board().at(p).mark == Mark::Question);
  app.dispatch_event(click(x, y, 2));
  REQUIRE(g->board().at(p).mark == Mark::None);
}

TEST_CASE("releases and drag-motion change nothing at all", "[minesweeper]") {
  // ⚠ MUTATION TARGET. The terminal is in ?1002h button-event tracking: a
  // release arrives as the same button with pressed == false, and so does
  // motion while a button is held. Delete the pressed guard in handle_mouse and
  // a drag across the board opens a swath of cells while every click fires
  // twice. Both arrive here as the same event shape, which is why one case
  // covers both.
  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);
  app.step();

  const Layout& l = g->layout();
  const int before_revealed = g->board().revealed_count();
  const int before_flags = g->board().flag_count();

  for (int c = 0; c < 9; ++c) {
    app.dispatch_event(click(l.glyph_x(c), l.row_y(6), 0, /*pressed=*/false));
    app.dispatch_event(click(l.glyph_x(c), l.row_y(6), 2, /*pressed=*/false));
  }
  REQUIRE(g->board().revealed_count() == before_revealed);
  REQUIRE(g->board().flag_count() == before_flags);

  // The wheel is ignored too.
  app.dispatch_event(termforge::Event{termforge::MouseEvent{
      .x = l.glyph_x(3), .y = l.row_y(3), .button = -1, .scroll_up = true}});
  REQUIRE(g->board().revealed_count() == before_revealed);
}

TEST_CASE("clicks off the grid are safe and do nothing", "[minesweeper]") {
  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}};
  g->board().load_mines(mines);
  app.step();

  const Layout& l = g->layout();
  const int before = g->board().revealed_count();
  const Coord cursor_before = g->cursor();

  app.dispatch_event(click(0, l.status_y));            // the HUD
  app.dispatch_event(click(0, l.hint_y));              // the hint row
  app.dispatch_event(click(l.frame_x, l.row_y(0)));    // the border
  app.dispatch_event(click(l.gutter_x(9), l.row_y(0)));  // trailing column
  app.dispatch_event(click(0, 0));
  app.dispatch_event(click(79, 23));

  REQUIRE(g->board().revealed_count() == before);
  REQUIRE(g->cursor() == cursor_before);
  REQUIRE(app.state() == Shell::State::InGame);
}

TEST_CASE("a left click on a revealed number chords", "[minesweeper]") {
  // The documented divergence from the reference, which binds chording to
  // auxclick alone.
  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}};
  g->board().load_mines(mines);
  const Coord one{.row = 1, .col = 1};
  g->board().reveal(one);
  g->board().cycle_mark({.row = 0, .col = 0});  // the flag is correct
  app.step();

  const Layout& l = g->layout();
  app.dispatch_event(click(l.glyph_x(one.col), l.row_y(one.row)));
  REQUIRE(g->board().at({.row = 0, .col = 1}).revealed);
  REQUIRE(g->board().at({.row = 2, .col = 2}).revealed);
}

// ─── Keyboard, and the two modes agreeing ──────────────────────────────────

TEST_CASE("mouse and keyboard reach the same state", "[minesweeper]") {
  // Equal footing, asserted rather than asserted-about: the same cell opened by
  // each route must leave identical boards.
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  const Coord target{.row = 2, .col = 3};

  Probe by_mouse;
  Minesweeper* m = enter_game(by_mouse);
  m->board().load_mines(mines);
  by_mouse.step();
  by_mouse.dispatch_event(
      click(m->layout().glyph_x(target.col), m->layout().row_y(target.row)));

  Probe by_keys;
  Minesweeper* k = enter_game(by_keys);
  k->board().load_mines(mines);
  by_keys.step();
  // The cursor starts at the centre of the board; walk it to the target.
  while (k->cursor().row > target.row) by_keys.dispatch_event(key(termforge::Key::Up));
  while (k->cursor().row < target.row) by_keys.dispatch_event(key(termforge::Key::Down));
  while (k->cursor().col > target.col) by_keys.dispatch_event(key(termforge::Key::Left));
  while (k->cursor().col < target.col) by_keys.dispatch_event(key(termforge::Key::Right));
  REQUIRE(k->cursor() == target);
  by_keys.dispatch_event(ch(U' '));

  REQUIRE(m->board().revealed_count() == k->board().revealed_count());
  for (int r = 0; r < 9; ++r) {
    for (int c = 0; c < 9; ++c) {
      const Coord p{.row = r, .col = c};
      REQUIRE(m->board().at(p).revealed == k->board().at(p).revealed);
    }
  }
}

TEST_CASE("the cursor is visible without colour and moves two columns",
          "[minesweeper]") {
  // ⚠ MUTATION TARGET. Replace the brackets with a background highlight and
  // this goes red — which is the point, because FallbackDriver discards colour
  // and a highlight-only cursor is invisible at exactly the tier this repo
  // promises always works (the same failure as termforge #72).
  Probe app;
  Minesweeper* g = enter_game(app);
  app.step();

  const Layout& l = g->layout();
  const Coord before = g->cursor();
  REQUIRE(cell_text(app, l.gutter_x(before.col), l.row_y(before.row)) == "[");
  REQUIRE(cell_text(app, l.gutter_x(before.col + 1), l.row_y(before.row)) == "]");

  app.dispatch_event(key(termforge::Key::Right));
  app.step();
  const Coord after = g->cursor();
  REQUIRE(after.col == before.col + 1);
  REQUIRE(cell_text(app, l.gutter_x(after.col), l.row_y(after.row)) == "[");
  REQUIRE(cell_text(app, l.gutter_x(after.col + 1), l.row_y(after.row)) == "]");
  // Two columns per cell, so the bracket moved exactly two.
  REQUIRE(l.gutter_x(after.col) - l.gutter_x(before.col) == 2);
}

TEST_CASE("a cursor on the last column keeps both brackets inside the frame",
          "[minesweeper]") {
  Probe app;
  Minesweeper* g = enter_game(app);
  app.dispatch_event(key(termforge::Key::End));
  app.step();

  const Layout& l = g->layout();
  REQUIRE(g->cursor().col == 8);
  const int closing = l.gutter_x(g->cursor().col + 1);
  REQUIRE(closing < l.frame_x + l.frame_w - 1);  // not on the right border
  REQUIRE(cell_text(app, closing, l.row_y(g->cursor().row)) == "]");
}

TEST_CASE("Escape and P are declined so the Shell keeps them", "[minesweeper]") {
  Probe app;
  enter_game(app);
  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);
  app.step();
  app.dispatch_event(key(termforge::Key::Escape));  // dismiss the dialog
  app.step();

  Probe other;
  enter_game(other);
  other.dispatch_event(key(termforge::Key::Escape));
  REQUIRE(other.state() == Shell::State::Selector);
}

TEST_CASE("Q returns to the menu from inside the game's own handler",
          "[minesweeper]") {
  Probe app;
  enter_game(app);
  app.dispatch_event(ch(U'q'));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
}

TEST_CASE("Enter on a finished board ends the game via done()", "[minesweeper]") {
  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{3, 3}};
  g->board().load_mines(mines);
  g->board().reveal({.row = 3, .col = 3});
  REQUIRE(g->board().state() == State::Lost);
  REQUIRE_FALSE(g->done());

  // ⚠ `g` DANGLES from here on. Shell::handle_in_game_key calls
  // apply_transitions() as soon as the game consumes a key, so done() is
  // polled and the Game is destroyed before dispatch_event even returns —
  // asserting g->done() afterwards is a use-after-free, which is how ASan
  // found this line. Assert the Shell's reaction instead; that the game ended
  // itself is exactly what the Shell leaving InGame means.
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::Selector);
  REQUIRE(app.current_game() == nullptr);
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
}

TEST_CASE("N and the level keys restart the board", "[minesweeper]") {
  Probe app;
  Minesweeper* g = enter_game(app);
  g->board().reveal({.row = 4, .col = 4});
  REQUIRE(g->board().revealed_count() > 0);

  app.dispatch_event(ch(U'n'));
  REQUIRE(g->board().revealed_count() == 0);
  REQUIRE(g->board().state() == State::Ready);

  app.dispatch_event(ch(U'2'));
  REQUIRE(g->level() == Level::Medium);
  REQUIRE(g->board().rows() == 16);
  REQUIRE(g->board().cols() == 16);
  app.dispatch_event(ch(U'3'));
  REQUIRE(g->board().cols() == 30);
  app.dispatch_event(ch(U'1'));
  REQUIRE(g->board().cols() == 9);
}

// ─── HUD and the clock ─────────────────────────────────────────────────────

TEST_CASE("the HUD zero-pads and only counts time after a reveal",
          "[minesweeper]") {
  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);
  app.step();

  REQUIRE(screen_contains(app, "MINES 003"));
  REQUIRE(screen_contains(app, "TIME 000"));
  REQUIRE(screen_contains(app, "EASY"));
  REQUIRE(screen_contains(app, "PLAYING"));

  // Ticks with nothing opened must not move the clock.
  for (int i = 0; i < 200; ++i) app.step();
  REQUIRE(screen_contains(app, "TIME 000"));
  // ...but they DO reach the game. The two accumulators are not the same thing.
  REQUIRE(g->ticks() > 0);

  // A flag moves the mine counter and still not the clock.
  g->board().cycle_mark({.row = 6, .col = 6});
  app.step();
  REQUIRE(screen_contains(app, "MINES 002"));
  REQUIRE(screen_contains(app, "TIME 000"));

  // ...and after a reveal it DOES advance, and the new value reaches the HUD.
  // Asserted here rather than in a pty capture: the renderer diffs, so a clock
  // ticking from 000 to 001 rewrites one digit in place and the string
  // "TIME 001" never appears in the byte stream at all.
  //
  // 3.5 seconds of ticks, not exactly 3: 180 additions of 1.0/60.0 accumulate
  // to 2.9999999999999996 in double, and seconds() truncates. The clock flips a
  // fraction of a microsecond late at each whole second, which no player can
  // see — but a test sitting exactly on that boundary is pinning floating-point
  // rounding rather than the behaviour it is named after.
  g->board().reveal({.row = 1, .col = 1});
  REQUIRE(g->board().timer_running());
  for (int i = 0; i < 7 * Shell::kTickHz / 2; ++i) {
    g->tick(std::chrono::duration<double>{1.0 / Shell::kTickHz});
  }
  app.step();
  REQUIRE(g->board().seconds() == 3);
  REQUIRE(screen_contains(app, "TIME 003"));
  REQUIRE_FALSE(screen_contains(app, "TIME 000"));
}

TEST_CASE("the state word says how the game ended, without colour",
          "[minesweeper]") {
  {
    Probe app;
    Minesweeper* g = enter_game(app);
    const Coord mines[]{{3, 3}};
    g->board().load_mines(mines);
    g->board().reveal({.row = 3, .col = 3});
    app.step();
    REQUIRE(screen_contains(app, "BOOM"));
  }
  {
    Probe app;
    Minesweeper* g = enter_game(app);
    const Coord mines[]{{0, 0}};
    g->board().load_mines(mines);
    for (int r = 0; r < 9; ++r) {
      for (int c = 0; c < 9; ++c) {
        if (r != 0 || c != 0) g->board().reveal({.row = r, .col = c});
      }
    }
    REQUIRE(g->board().state() == State::Won);
    app.step();
    REQUIRE(screen_contains(app, "YOU WIN"));
    REQUIRE(screen_contains(app, "MINES 000"));
  }
}

// ─── Sizes ─────────────────────────────────────────────────────────────────

TEST_CASE("a board too big for the terminal says so and stays playable",
          "[minesweeper]") {
  Probe app;
  Minesweeper* g = enter_game(app, 60, 20);
  app.dispatch_event(ch(U'3'));  // Hard needs 63 columns
  app.step(1, 60, 20);

  REQUIRE_FALSE(g->layout().fits);
  REQUIRE(screen_contains(app, "63x20"));
  REQUIRE(screen_contains(app, "60x20"));
  // The level keys stay live, so the player can pick one that fits rather than
  // having their difficulty silently downgraded for them.
  app.dispatch_event(ch(U'1'));
  app.step(1, 60, 20);
  REQUIRE(g->layout().fits);
  REQUIRE(g->level() == Level::Easy);
}

TEST_CASE("the game renders at every size without crashing", "[minesweeper]") {
  struct Size {
    int cols;
    int rows;
  };
  const Size sizes[]{{80, 24}, {63, 20}, {60, 20}, {40, 12}, {21, 13}, {20, 8}};
  for (const Size s : sizes) {
    Probe app;
    Minesweeper* g = enter_game(app, s.cols, s.rows);
    for (const char32_t level : {U'1', U'2', U'3'}) {
      app.dispatch_event(ch(level));
      app.step(1, s.cols, s.rows);
      REQUIRE(all_seven_bit(app));
      REQUIRE(g != nullptr);
    }
    // Below the Shell's floor the Shell draws its own too-small screen instead,
    // and must still not crash.
    app.step(1, 10, 4);
    REQUIRE(app.state() == Shell::State::InGame);
  }
}
