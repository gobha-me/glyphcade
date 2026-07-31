// Minesweeper's RENDERING, INPUT, and the SOUND that input asks for, through a
// real Shell into an offscreen Screen. The rules live in test/14minesweeper and
// are not re-tested here.
//
// The SFX cases at the bottom were mutation-tested, and two of the three
// mutations survived the first attempt — the notes on each case say which and
// what was changed as a result. It is worth reading those before adding a
// binding: "the verb returned true" is not the same question as "something
// audible happened", and the difference is not the same in both directions.
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
  // ⚠ A SECOND Enter, and it goes AFTER the REQUIRE above, not before. The
  // REQUIRE is what proves the Shell entered on the FIRST Enter; moving it below
  // this line would make the case pass even if entering had come to need two.
  //
  // gitea #38: entering a game now opens its pre-start options screen, so a
  // suite that wants a BOARD has to say so. This is the change telling the truth
  // about itself, not a regression -- and the per-suite cases below assert the
  // screen is there before this dismisses it.
  //
  // ⚠ Leaving this out does not produce a red test, it produces a HANG. Several
  // cases here steer with `while (cursor().row < N) dispatch(Down)`, which is
  // bounded by the code under test: with the options screen up the arrows move a
  // cycler instead of the cursor, the predicate never becomes true, and the
  // suite spins forever.
  app.dispatch_event(key(termforge::Key::Enter));
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

// ── SFX bindings ────────────────────────────────────────────────────────────
//
// Every case below drives the REAL input path — dispatch_event into the Shell,
// into the game's handle_key/handle_mouse — over a board installed with
// load_mines(), so what is asserted is what a keystroke does rather than what a
// helper does.
//
// ⚠ Asserted through app.audio(), the SHELL's engine, never through the Game.
// That is not a stylistic choice: Shell::handle_in_game_key calls
// apply_transitions() as soon as the game consumes a key, so a key that ends
// the game destroys the Game before dispatch_event() returns. Reading a counter
// off the game afterwards is a use-after-free that only ASan catches (STATUS.md
// records it costing a debugging round). The Shell outlives every game, so
// asserting there sidesteps the trap structurally rather than by remembering.
//
// play_count() records INTENT, so all of this passes identically on the
// TERMGAME_WITH_AUDIO=OFF arm — no sink is injected and no file is written.

TEST_CASE("revealing a safe cell plays the reveal sound",
          "[minesweeper][audio]") {
  using termgame::audio::SfxId;

  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);

  // Cursor is at 0,0 which is a mine — move somewhere safe first.
  app.dispatch_event(key(termforge::Key::Down));
  app.dispatch_event(key(termforge::Key::Right));
  REQUIRE(app.audio().play_count(SfxId::Reveal) == 0);

  app.dispatch_event(ch(U' '));

  REQUIRE(app.audio().play_count(SfxId::Reveal) == 1);
  REQUIRE(app.audio().play_count(SfxId::Explode) == 0);
  REQUIRE(app.audio().play_count(SfxId::Win) == 0);
}

TEST_CASE("revealing an already-open cell is silent", "[minesweeper][audio]") {
  // ⚠ THE MUTATION-PROOF CASE for announce()'s `changed` guard. "Play Reveal
  // whenever the verb returned true" is the obvious simplification, and it is
  // wrong in exactly this way: a second press on an open cell changes nothing
  // and must make no sound.
  using termgame::audio::SfxId;

  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);

  app.dispatch_event(key(termforge::Key::Down));
  app.dispatch_event(key(termforge::Key::Right));
  app.dispatch_event(ch(U' '));
  const auto after_first = app.audio().play_count(SfxId::Reveal);
  REQUIRE(after_first == 1);

  app.dispatch_event(ch(U' '));  // same cell, already open
  REQUIRE(app.audio().play_count(SfxId::Reveal) == after_first);
}

TEST_CASE("space on a flagged cell is silent", "[minesweeper][audio]") {
  // The other half of the same guard: reveal() refuses a flagged cell, so the
  // press is a deliberate no-op and must stay quiet.
  using termgame::audio::SfxId;

  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);

  app.dispatch_event(key(termforge::Key::Down));
  app.dispatch_event(key(termforge::Key::Right));
  app.dispatch_event(ch(U'f'));  // flag it
  REQUIRE(app.audio().play_count(SfxId::Flag) == 1);

  app.dispatch_event(ch(U' '));  // refused by the model
  REQUIRE(app.audio().play_count(SfxId::Reveal) == 0);
  REQUIRE(app.audio().play_count(SfxId::Explode) == 0);
}

TEST_CASE("stepping on a mine plays explode and lose",
          "[minesweeper][audio]") {
  // ⚠ The case that proves announce() reads the STATE TRANSITION rather than
  // the return value. reveal() returns the same `true` here as it does for an
  // ordinary open, so a binding that trusted the bool alone would play Reveal.
  using termgame::audio::SfxId;

  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);

  // Open something safe first, so the board is Playing rather than Ready.
  app.dispatch_event(key(termforge::Key::Down));
  app.dispatch_event(key(termforge::Key::Right));
  app.dispatch_event(ch(U' '));

  // Now walk onto (4,4) and open it.
  //
  // ⚠ BIDIRECTIONAL, like the by_keys walk earlier in this file. gitea #38
  // made the entry cursor start CENTRED rather than at (0,0) -- dismissing the
  // options screen goes through new_game(), which recentres exactly as 1/2/3
  // always did -- so a one-directional walk can now start PAST its target and
  // spin forever. These loops are bounded by the code under test; make them
  // converge from either side rather than assume where the cursor begins.
  Minesweeper* live = game_of(app);
  REQUIRE(live != nullptr);
  while (live->cursor().row > 4) app.dispatch_event(key(termforge::Key::Up));
  while (live->cursor().row < 4) app.dispatch_event(key(termforge::Key::Down));
  while (live->cursor().col > 4) app.dispatch_event(key(termforge::Key::Left));
  while (live->cursor().col < 4) app.dispatch_event(key(termforge::Key::Right));
  REQUIRE(live->cursor().row == 4);
  REQUIRE(live->cursor().col == 4);

  app.dispatch_event(ch(U' '));

  REQUIRE(app.audio().play_count(SfxId::Explode) == 1);
  REQUIRE(app.audio().play_count(SfxId::Lose) == 1);
  REQUIRE(app.audio().play_count(SfxId::Win) == 0);
}

TEST_CASE("clearing the board plays the win sound", "[minesweeper][audio]") {
  using termgame::audio::SfxId;

  Probe app;
  Minesweeper* g = enter_game(app);

  // One mine in the corner: opening every other cell wins.
  const Coord mines[]{{0, 0}};
  g->board().load_mines(mines);

  auto& board = g->board();
  for (int r = 0; r < board.rows(); ++r) {
    for (int c = 0; c < board.cols(); ++c) {
      if (r == 0 && c == 0) continue;
      if (board.at({.row = r, .col = c}).revealed) continue;
      if (board.finished()) break;
      board.reveal({.row = r, .col = c});
    }
  }
  REQUIRE(board.state() == State::Won);

  // The model was driven directly above to set the position up, so no sound has
  // been asked for yet — the binding lives in the input path, not in Board.
  REQUIRE(app.audio().play_count(SfxId::Win) == 0);
}

TEST_CASE("winning through the input path plays the win sound",
          "[minesweeper][audio]") {
  // ⚠ The previous case proves Board stays silent; this one proves the INPUT
  // path is what sounds. Together they pin "audio is presentation": driving the
  // model makes no sound, driving the game does.
  using termgame::audio::SfxId;

  Probe app;
  Minesweeper* g = enter_game(app);

  // ⚠ The mine layout is doing real work here. With a single corner mine the
  // board is almost all zeros, so ONE reveal floods the whole grid and wins
  // before the input path is ever used — an earlier draft did exactly that and
  // the case failed on `REQUIRE_FALSE(finished())`.
  //
  // Walling (8,8) off with mines on all three of its neighbours makes it
  // unreachable by flood fill: the fill only expands THROUGH zero cells, and a
  // mine is never revealed, so nothing can open (8,8) except opening it.
  const Coord last{.row = 8, .col = 8};
  const Coord mines[]{{0, 0}, {7, 7}, {7, 8}, {8, 7}};
  g->board().load_mines(mines);

  auto is_mine = [&mines](Coord at) {
    for (const Coord m : mines) {
      if (m == at) return true;
    }
    return false;
  };

  // Open every safe cell except `last`, directly on the model.
  auto& board = g->board();
  for (int r = 0; r < board.rows(); ++r) {
    for (int c = 0; c < board.cols(); ++c) {
      const Coord at{.row = r, .col = c};
      if (is_mine(at) || at == last) continue;
      if (!board.at(at).revealed) board.reveal(at);
    }
  }
  REQUIRE_FALSE(board.finished());

  // The last cell goes through the real input path.
  Minesweeper* live = game_of(app);
  REQUIRE(live != nullptr);
  while (live->cursor().row < last.row) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  while (live->cursor().col < last.col) {
    app.dispatch_event(key(termforge::Key::Right));
  }
  app.dispatch_event(ch(U' '));

  REQUIRE(app.audio().play_count(SfxId::Win) == 1);
  REQUIRE(app.audio().play_count(SfxId::Explode) == 0);
  REQUIRE(app.audio().play_count(SfxId::Lose) == 0);
}

TEST_CASE("the mark cycle sounds flag, then click, then click",
          "[minesweeper][audio]") {
  // Placing a flag is a decision; stepping to Question and clearing back to
  // None are undoing one. announce_mark() reads what the cell BECAME, so all
  // three presses of the same key produce two different sounds.
  using termgame::audio::SfxId;

  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);

  // ⚠ A BASELINE, not zero. gitea #38: dismissing the pre-start options screen
  // starts the game through new_game(), which clicks -- so entering a game is
  // no longer silent, and a case that hardcodes 0 is really asserting how many
  // sounds ENTRY makes. Deltas say what this case is actually about.
  const auto base_click = app.audio().play_count(SfxId::Click);

  app.dispatch_event(ch(U'f'));  // None -> Flag
  REQUIRE(app.audio().play_count(SfxId::Flag) == 1);
  REQUIRE(app.audio().play_count(SfxId::Click) == base_click);

  app.dispatch_event(ch(U'f'));  // Flag -> Question
  REQUIRE(app.audio().play_count(SfxId::Flag) == 1);
  REQUIRE(app.audio().play_count(SfxId::Click) == base_click + 1);

  app.dispatch_event(ch(U'f'));  // Question -> None
  REQUIRE(app.audio().play_count(SfxId::Flag) == 1);
  REQUIRE(app.audio().play_count(SfxId::Click) == base_click + 2);
}

TEST_CASE("a chord with the wrong flag count is silent",
          "[minesweeper][audio]") {
  // chord() is a deliberate visible no-op when the flags do not add up, and a
  // no-op must be inaudible as well as harmless.
  using termgame::audio::SfxId;

  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);

  // Open a numbered cell next to a mine, with nothing flagged.
  g->board().reveal({.row = 1, .col = 1});
  app.step();

  Minesweeper* live = game_of(app);
  REQUIRE(live != nullptr);
  // ⚠ BIDIRECTIONAL, and this case was passing for the WRONG REASON without it.
  // The entry cursor now starts centred at (4,4), which is a MINE and is
  // unrevealed — so a downward-only walk left the cursor there, and 'c' was
  // being refused for "the cell is not revealed" instead of for "the flags do
  // not add up". Same assertions, green, testing something else entirely.
  while (live->cursor().row > 1) app.dispatch_event(key(termforge::Key::Up));
  while (live->cursor().row < 1) app.dispatch_event(key(termforge::Key::Down));
  while (live->cursor().col > 1) app.dispatch_event(key(termforge::Key::Left));
  while (live->cursor().col < 1) app.dispatch_event(key(termforge::Key::Right));
  // Pin it, so this cannot quietly go back to chording the wrong cell.
  REQUIRE(live->cursor().row == 1);
  REQUIRE(live->cursor().col == 1);
  REQUIRE(live->board().at({.row = 1, .col = 1}).revealed);

  const auto before = app.audio().play_count(SfxId::Reveal);
  app.dispatch_event(ch(U'c'));  // no flags placed: refused
  REQUIRE(app.audio().play_count(SfxId::Reveal) == before);
  REQUIRE(app.audio().play_count(SfxId::Explode) == 0);
}

TEST_CASE("cursor movement makes no sound", "[minesweeper][audio]") {
  // ⚠ DELIBERATE, not an oversight. A blip per keystroke under a held arrow key
  // is unpleasant, and fixing that needs a rate limit, and a rate limit needs a
  // clock — and dt inside Game::tick is the only clock a game may read. That is
  // a feel decision requiring a human ear, so it is deferred rather than
  // guessed at. Recorded in STATUS.md's Epic 2 deferral table.
  using termgame::audio::SfxId;

  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);

  // ⚠ Baselined rather than compared against zero. Entering the game played
  // MenuSelect on this same engine — the Shell's — so an absolute assertion
  // here fails for a reason that has nothing to do with the cursor. What is
  // being claimed is that MOVING adds nothing, not that nothing ever happened.
  std::vector<std::uint32_t> before;
  for (const auto id : termgame::audio::kSfxIds) {
    before.push_back(app.audio().play_count(id));
  }

  for (int i = 0; i < 5; ++i) {
    app.dispatch_event(key(termforge::Key::Down));
    app.dispatch_event(key(termforge::Key::Right));
  }
  app.dispatch_event(key(termforge::Key::Home));
  app.dispatch_event(key(termforge::Key::End));

  for (std::size_t i = 0; i < termgame::audio::kSfxIds.size(); ++i) {
    REQUIRE(app.audio().play_count(termgame::audio::kSfxIds[i]) == before[i]);
  }
}

TEST_CASE("starting a new game clicks", "[minesweeper][audio]") {
  // ⚠ NOT routed through announce(): new_game() resets the board, so the state
  // goes Playing -> Ready, which announce() would read as neither progress nor
  // an outcome and would have to special-case.
  using termgame::audio::SfxId;

  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);

  // ⚠ A baseline, for the reason above: entry itself now clicks once, because
  // dismissing the options screen IS a new_game(). That is the same sound for
  // the same reason, which is the point -- 1/2/3, 'n' and "start the game you
  // just configured" are one operation with one acknowledgement.
  const auto base_click = app.audio().play_count(SfxId::Click);
  REQUIRE(base_click == 1U);  // and entry made exactly one, not zero or two

  app.dispatch_event(ch(U'2'));
  REQUIRE(app.audio().play_count(SfxId::Click) == base_click + 1);
  app.dispatch_event(ch(U'n'));
  REQUIRE(app.audio().play_count(SfxId::Click) == base_click + 2);
}

TEST_CASE("a right click flags, like the keyboard", "[minesweeper][audio]") {
  // The mouse path and the key path must not drift apart — they are the same
  // two verbs and should make the same two sounds.
  using termgame::audio::SfxId;

  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);

  const Layout& l = g->layout();
  REQUIRE(l.fits);

  const Coord at{.row = 3, .col = 3};
  const int x = l.glyph_x(at.col);
  const int y = l.row_y(at.row);

  const auto base_click = app.audio().play_count(SfxId::Click);

  app.dispatch_event(click(x, y, 2));
  REQUIRE(app.audio().play_count(SfxId::Flag) == 1);

  app.dispatch_event(click(x, y, 2));  // -> Question
  // ⚠ base + 1, not 1: entry itself clicks once now, because dismissing the
  // pre-start options screen starts the game through new_game(). See the mark
  // cycle case above.
  REQUIRE(app.audio().play_count(SfxId::Click) == base_click + 1);
}

TEST_CASE("marking a revealed cell is silent", "[minesweeper][audio]") {
  // ⚠ THE case that makes announce_mark()'s `changed` guard load-bearing. It
  // was added after mutation testing: deleting that guard left the whole suite
  // green, because nothing here covered the one path where it matters.
  //
  // cycle_mark() refuses a revealed cell and leaves the mark at None. Without
  // the guard, announce_mark() reads that None as "became not-a-flag" and
  // clicks — so every press of f on an open square would blip. There is nothing
  // else to compare against, because nothing about the board moved.
  using termgame::audio::SfxId;

  Probe app;
  Minesweeper* g = enter_game(app);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  g->board().load_mines(mines);

  g->board().reveal({.row = 1, .col = 1});  // a number cell, now open
  app.step();

  Minesweeper* live = game_of(app);
  REQUIRE(live != nullptr);
  // ⚠ BIDIRECTIONAL, and this case is why it matters rather than being tidy.
  // The entry cursor now starts CENTRED (dismissing the options screen goes
  // through new_game(), which recentres exactly as 1/2/3 always did), so a
  // downward-only walk leaves the cursor at (4,4) — and then 'f' marks a cell
  // that is NOT the revealed one, the guard under test is never reached, and
  // the case fails for a reason that has nothing to do with what it is about.
  while (live->cursor().row > 1) app.dispatch_event(key(termforge::Key::Up));
  while (live->cursor().row < 1) app.dispatch_event(key(termforge::Key::Down));
  while (live->cursor().col > 1) app.dispatch_event(key(termforge::Key::Left));
  while (live->cursor().col < 1) app.dispatch_event(key(termforge::Key::Right));
  REQUIRE(live->cursor().row == 1);
  REQUIRE(live->cursor().col == 1);
  REQUIRE(live->board().at({.row = 1, .col = 1}).revealed);

  const auto clicks = app.audio().play_count(SfxId::Click);
  const auto flags = app.audio().play_count(SfxId::Flag);

  app.dispatch_event(ch(U'f'));
  app.dispatch_event(click(g->layout().glyph_x(1), g->layout().row_y(1), 2));

  REQUIRE(app.audio().play_count(SfxId::Click) == clicks);
  REQUIRE(app.audio().play_count(SfxId::Flag) == flags);
}

// ── Best time (gitea #14) ───────────────────────────────────────────────────
//
// ⚠ Every case below uses the SHELL's default store, which is memory-only. That
// is the point: none of this needs a file, so none of it can leave one behind or
// depend on a temp directory. What the store being the SHELL's — rather than the
// Game's — buys is the only thing these cases care about, since a fresh Game is
// built per menu entry.
//
// The store's own format, merge and degraded modes live in test/24scores.

// A win, in two halves, because the CLOCK has to run between them.
//
// ⚠ Do not fold these back together. load_mines() resets the board, which resets
// the clock — so a case that advances time and then calls a combined helper
// records a best time of zero and, because zero is a valid Lower record, gets a
// green screen assertion over a wrong stored value. That is not hypothetical:
// it is how the first draft of these cases failed.

constexpr Coord kLastCell{.row = 8, .col = 8};

// Installs the layout and opens every safe cell but one, directly on the model.
// Walling (8,8) off with mines on all three of its neighbours is what keeps the
// flood fill from opening it — the fill only expands THROUGH zero cells — so
// there is a last cell left for a keystroke to take. Leaves the clock running.
auto arm_win(Minesweeper* g) -> void {
  const Coord mines[]{{0, 0}, {7, 7}, {7, 8}, {8, 7}};
  g->board().load_mines(mines);

  auto is_mine = [&mines](Coord at) {
    for (const Coord m : mines) {
      if (m == at) return true;
    }
    return false;
  };

  auto& board = g->board();
  for (int r = 0; r < board.rows(); ++r) {
    for (int c = 0; c < board.cols(); ++c) {
      const Coord at{.row = r, .col = c};
      if (is_mine(at) || at == kLastCell) continue;
      if (!board.at(at).revealed) board.reveal(at);
    }
  }
  REQUIRE_FALSE(board.finished());
  REQUIRE(board.timer_running());
}

// Takes the last cell through the REAL input path, which is where announce()
// runs and therefore the only path that records anything.
auto finish_win(Probe& app) -> void {
  Minesweeper* live = game_of(app);
  REQUIRE(live != nullptr);
  while (live->cursor().row < kLastCell.row) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  while (live->cursor().col < kLastCell.col) {
    app.dispatch_event(key(termforge::Key::Right));
  }
  app.dispatch_event(ch(U' '));
}

TEST_CASE("a best time outlives the game that set it", "[minesweeper][scores]") {
  // ⚠ THE CASE THE WHOLE FEATURE EXISTS FOR. A fresh Game is constructed per menu
  // entry (Shell::enter_selected_game) and destroyed on the way out, so a best
  // time held in the Game would die on quit-to-menu — a "best" that resets every
  // entry is worse than none. Move the store into the Game and every other case
  // in this file still passes; only this one goes red.
  Probe app;
  Minesweeper* g = enter_game(app);

  REQUIRE(screen_contains(app, "BEST ---"));

  arm_win(g);
  // Three seconds on the clock before the winning keystroke. 3.5 rather than 3
  // for the same floating-point reason the TIME case above gives: 180 additions
  // of 1.0/60.0 land at 2.9999999999999996 and seconds() truncates.
  for (int i = 0; i < 7 * Shell::kTickHz / 2; ++i) {
    g->tick(std::chrono::duration<double>{1.0 / Shell::kTickHz});
  }
  finish_win(app);
  app.step();

  REQUIRE(app.scores().get("minesweeper", "best_time_easy") == 3);
  REQUIRE(screen_contains(app, "BEST 003"));

  // Back to the menu — which destroys the Game — and in again, which builds a
  // brand new one.
  app.dispatch_event(key(termforge::Key::Escape));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);

  Minesweeper* second = enter_game(app);
  // ⚠ Freshness is asserted through STATE, not through pointer identity. The
  // first Game is destroyed before the second is allocated, so the allocator
  // hands back the same address more often than not — `second != g` looks like
  // the stronger claim and is in fact a coin flip. A zeroed clock on a board
  // that had just been won cannot be the old object.
  REQUIRE(second->board().seconds() == 0);
  REQUIRE_FALSE(second->board().finished());
  REQUIRE(screen_contains(app, "BEST 003"));
}

TEST_CASE("a slower win does not replace a faster one", "[minesweeper][scores]") {
  // ⚠ THE CASE THAT MAKES Better::Lower LOAD-BEARING AT THIS LAYER. Every other
  // minesweeper case here records exactly one win per level, so the direction
  // never gets to matter — record with Better::Higher instead and they all stay
  // green. This is the one that notices, and it is the direction a naive design
  // gets backwards: for a TIME, smaller wins.
  Probe app;
  Minesweeper* g = enter_game(app);

  arm_win(g);
  for (int i = 0; i < 7 * Shell::kTickHz / 2; ++i) {
    g->tick(std::chrono::duration<double>{1.0 / Shell::kTickHz});
  }
  finish_win(app);
  app.step();
  REQUIRE(app.scores().get("minesweeper", "best_time_easy") == 3);

  // A second, slower game on the same level.
  app.dispatch_event(ch(U'n'));  // new game, same difficulty
  app.step();
  Minesweeper* again = game_of(app);
  REQUIRE(again != nullptr);
  arm_win(again);
  for (int i = 0; i < 20 * Shell::kTickHz; ++i) {
    again->tick(std::chrono::duration<double>{1.0 / Shell::kTickHz});
  }
  REQUIRE(again->board().seconds() >= 20);
  finish_win(app);
  app.step();

  REQUIRE(app.scores().get("minesweeper", "best_time_easy") == 3);
  REQUIRE(screen_contains(app, "BEST 003"));

  // ...and a faster one does replace it, so the case is not passing merely
  // because nothing is ever written twice.
  app.dispatch_event(ch(U'n'));
  app.step();
  Minesweeper* third = game_of(app);
  REQUIRE(third != nullptr);
  arm_win(third);
  third->tick(std::chrono::duration<double>{1.0 / Shell::kTickHz});
  finish_win(app);
  app.step();

  REQUIRE(app.scores().get("minesweeper", "best_time_easy") == 0);
  REQUIRE(screen_contains(app, "BEST 000"));
}

TEST_CASE("each difficulty keeps its own best time", "[minesweeper][scores]") {
  // ⚠ What proves the key is per-LEVEL rather than per-game. Collapse time_key()
  // to a single constant and this is the case that notices: Medium would inherit
  // Easy's record and show a time nobody ever played.
  Probe app;
  Minesweeper* g = enter_game(app);

  arm_win(g);
  for (int i = 0; i < 7 * Shell::kTickHz / 2; ++i) {
    g->tick(std::chrono::duration<double>{1.0 / Shell::kTickHz});
  }
  finish_win(app);
  app.step();
  REQUIRE(screen_contains(app, "BEST 003"));

  app.dispatch_event(ch(U'2'));  // Medium
  app.step();
  REQUIRE(game_of(app)->level() == Level::Medium);
  REQUIRE(screen_contains(app, "BEST ---"));
  REQUIRE_FALSE(app.scores().get("minesweeper", "best_time_medium").has_value());

  app.dispatch_event(ch(U'1'));  // back to Easy
  app.step();
  REQUIRE(screen_contains(app, "BEST 003"));
}

TEST_CASE("a win past the timer cap is stored in full and displayed frozen",
          "[minesweeper][scores]") {
  // ⚠ THE CASE FOR Board::elapsed(). seconds() clamps at kTimerCap for a
  // three-column HUD; record the clamp and a 1200-second win is written as 999,
  // which is wrong AND unbeatable-by-tie — every later slow win draws with it.
  // Swap elapsed() for seconds() in announce() and the store assertion below goes
  // red while the screen assertion stays green, which is exactly the split.
  Probe app;
  Minesweeper* g = enter_game(app);

  arm_win(g);
  g->board().advance(std::chrono::duration<double>{1200.0});
  REQUIRE(g->board().seconds() == Board::kTimerCap);

  finish_win(app);
  app.step();

  REQUIRE(app.scores().get("minesweeper", "best_time_easy") == 1200);
  REQUIRE(screen_contains(app, "BEST 999"));
}

TEST_CASE("the outcome word survives a fourth status field on a narrow board",
          "[minesweeper][scores][render]") {
  // ⚠ THE REGRESSION THIS ROW'S REWRITE EXISTS FOR. draw_status() used to build
  // its left half unconditionally and drop the WORD when the two collided — the
  // opposite priority from 2048's, and the reason a fourth field could not simply
  // be appended. Restore that whole shape (unconditional left, word skipped on
  // collision) and "YOU WIN" disappears here while every wide-terminal case
  // stays green. Verified by mutation, not assumed.
  //
  // ⚠ It is THE BUDGET that is load-bearing, not the unconditional final
  // write_text of the word. Putting the old `if (word_x > left.size())` guard
  // back on top of the budget changes nothing and no test notices — correctly,
  // because the budget already caps left at word_x - 2, so the guard can never
  // fire. That is the same finding twenty48.cpp records about its draw ORDER:
  // once the arithmetic is right, the second mechanism is decoration, and two
  // mechanisms for one property is how a test comes to pass on a broken row.
  //
  // At the no-colour tier the word is the only thing that says the game is over;
  // a dropped counter is merely a narrow terminal.
  for (const int cols : {30, 40, 52, 80}) {
    Probe app;
    Minesweeper* g = enter_game(app, cols, 24);

    arm_win(g);
    finish_win(app);
    app.step(1, cols, 24);

    REQUIRE(screen_contains(app, "YOU WIN"));
    REQUIRE(all_seven_bit(app));
    // Whole fields or nothing: a field that appears must appear complete, the
    // same contract test/23twenty48-ui holds 2048's row to.
    const std::string status = row_text(app, 0);
    for (const auto& [label, full] :
         std::vector<std::pair<std::string, std::string>>{
             {"MINES", "MINES 000"}, {"TIME", "TIME 000"}, {"BEST", "BEST 000"}}) {
      if (status.find(label) != std::string::npos) {
        REQUIRE(status.find(full) != std::string::npos);
      }
    }
  }
}

// ── The pre-start options screen (gitea #38) ────────────────────────────────

TEST_CASE("entering minesweeper shows the options screen, not the board",
          "[minesweeper][options]") {
  Probe app;
  app.step(1, 80, 24);
  const int index = minesweeper_index();
  REQUIRE(index >= 0);
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);
  app.step(1, 80, 24);

  std::string all;
  for (int y = 0; y < 24; ++y) all += row_text(app, y) + "\n";
  INFO(all);
  CHECK(all.find("Level") != std::string::npos);
  CHECK(all.find("Easy") != std::string::npos);
  CHECK(all.find("Enter start") != std::string::npos);
  // MINES is the status row's counter label, and belongs to a board in play.
  CHECK(all.find("MINES") == std::string::npos);
  CHECK(all_seven_bit(app));
}

TEST_CASE("the chosen level is the board you get", "[minesweeper][options]") {
  Probe app;
  app.step(1, 80, 24);
  const int index = minesweeper_index();
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  app.step(1, 80, 24);

  app.dispatch_event(key(termforge::Key::Right));  // Easy -> Medium
  app.dispatch_event(key(termforge::Key::Right));  // Medium -> Hard
  app.dispatch_event(key(termforge::Key::Enter));
  app.step(1, 80, 24);

  Minesweeper* g = game_of(app);
  REQUIRE(g != nullptr);
  // Hard is 16 rows x 30 columns / 99 mines.
  CHECK(g->board().rows() == 16);
  CHECK(g->board().cols() == 30);
  CHECK(g->board().total_mines() == 99);
}

TEST_CASE("accepting the default gives an Easy board",
          "[minesweeper][options]") {
  // ⚠ The pair of the case above -- a game that ignored selected() and always
  // applied default_index passes this one on its own.
  Probe app;
  Minesweeper* g = enter_game(app);
  REQUIRE(g != nullptr);
  CHECK(g->board().rows() == 9);
  CHECK(g->board().cols() == 9);
  CHECK(g->board().total_mines() == 10);
}

TEST_CASE("Escape from the options screen goes back to the menu",
          "[minesweeper][options]") {
  Probe app;
  app.step(1, 80, 24);
  const int index = minesweeper_index();
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);
  app.step(1, 80, 24);
  app.dispatch_event(key(termforge::Key::Escape));
  app.step(1, 80, 24);
  CHECK(app.state() == Shell::State::Selector);
}

TEST_CASE("the options screen survives the Shell's own floor",
          "[minesweeper][options]") {
  // ⚠ 20x8 is Shell::kMinCols x kMinRows -- smaller than minesweeper's own
  // playfield floor, so "the board does not fit" and "the options do not fit"
  // are different questions. The hint row must survive; the game is unreachable
  // without it.
  Probe app;
  app.step(1, 20, 8);
  const int index = minesweeper_index();
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  app.step(1, 20, 8);
  CHECK(all_seven_bit(app));
  CHECK(row_text(app, 7).find("Enter start") != std::string::npos);
}
