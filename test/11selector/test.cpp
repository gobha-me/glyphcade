// The Shell's state machine: Selector <-> InGame <-> Paused.
//
// Everything here is driven through App::dispatch_event, which is public and is
// the real routing path — including the overlay-capture policy, so the pause
// cases genuinely prove that the dialog swallows the game's input rather than
// asserting that we remembered to ignore it. One case goes a layer lower and
// feeds raw bytes through test_pump, so the escape-sequence decoder is covered
// at least once.
//
// ⚠ Six traps for whoever extends this file:
//
//   1. Dialog::begin_result()'s latch clears only on the next draw(). A test
//      that pauses, answers, and pauses again without running a frame in
//      between will find the second answer silently ignored. Run one frame.
//   2. ListWidget::rect() is only set inside Shell::on_render, so anything
//      geometry-dependent needs a frame first.
//   3. Everything here renders at the ASCII tier. test_run_frames installs a
//      FallbackDriver, which reports no colour, so the Shell probes
//      BorderStyle::Ascii and every glyph family resolves to its 7-bit form —
//      the selection marker is '>' here and '▸' on a capable terminal. That is
//      the tier worth testing (it is the one the repo promises always works),
//      but do not write a case that only holds there and call it universal.
//      ⚠ There is no way to reach the OTHER tier from here, and it is not for
//      want of a seam: test_wire_headless is private, hardcodes the
//      FallbackDriver, and that driver's capabilities() is an all-false
//      literal, so TERM= in the ctest environment changes nothing. The pause
//      dialog's border (gitea #44) joins the ▸ marker and the ↑↓ hint row on
//      the list of things whose colour-tier arm lives only in AGENTS.md's pty
//      recipe. The consequence worth stating out loud: at this tier
//      set_border_style(m_ctx.border_style()) and a hardcoded
//      set_border_style(Ascii) are indistinguishable, so the cases below pin
//      the tier's OUTPUT and not the fact that it was derived.
//   4. App::running() is NOT STICKY. test_run_frames sets m_running = true on
//      entry, so running() answers "did a quit happen during the last run",
//      not "has one ever happened". Assert it BEFORE the step() you needed for
//      a state transition; after that step it is true again whatever the
//      dispatched key did, and REQUIRE(app.running()) passes vacuously. Two
//      cases below assert it early for exactly that reason — do not tidy them
//      down beside the other post-step assertions. (Shell::quit_requested()
//      used to hide this by latching; it was a workaround for termforge #73
//      and went with gitea #17.)
//   5. Mouse events are HIT-TESTED before they are routed. App::route_mouse
//      only offers an event to a widget whose rect() contains its x/y, so a
//      wheel or click at coordinates outside the list is not "ignored by the
//      list" — it never reaches the list at all, and a case built on one
//      passes while asserting nothing. Combined with trap 2 (rect() is unset
//      until a frame has rendered), coordinates before the first step() are
//      always outside. Use kMarkX/kRow0 and step() first, as the wheel and
//      click cases below do.
//   6. AFTER step() THE SCREEN HOLDS NO OVERLAY. App::frame_step draws the
//      overlay stack, present()s it, and then restore_backdrop()s every cell
//      back from a snapshot taken before the dialog was drawn — upstream's own
//      comment on that line is "the overlay pass leaves no trace behind". So
//      row_text() after a pause shows the game underneath, and a case that
//      searched it for dialog text would assert nothing and pass. Nothing in
//      this file could read an overlay's cells until Probe::paint_overlay_pass
//      below. Use it, and mind that it leaves the Screen dimmed and overlaid
//      until the next step().

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <termforge/core/types.hpp>

#include <termgame/arcade/registry.hpp>
#include <termgame/arcade/shell.hpp>
#include <termgame/games/minesweeper/minesweeper.hpp>

namespace {

using termgame::Shell;

class Probe final : public Shell {
 public:
  using Shell::screen;

  Probe() { set_frame_ms(0); }  // see the comment in test/10render

  // ⚠ THE SIZE IS A PARAMETER, and that is what turned two deferrals on.
  // Interior list rows are h - 5, so a roster overflows its pane only when
  // h - 5 < entries — at four games that was rows == 8, the Shell's own floor
  // and nowhere else. Hardcoded at 60x20 this probe could never reach it, so
  // "revisit at four roster entries" was necessary but not sufficient: the
  // fourth game AND this parameter are what make the wheel's positive half and
  // the scrollbar's 7-bit sweep reachable. See STATUS.md.
  //
  // ⚠ Sokoban is the FIFTH, so the overflow band is now rows <= 9 rather than
  // rows == 8 alone. The cases below still drive 8 on purpose: it is the size
  // that overflows for every roster from four entries upward, so they do not
  // silently become a test of a roster length instead of a test of a
  // scrollbar.
  auto step(int frames = 1, int cols = 60, int rows = 20) -> void {
    test_run_frames(frames, cols, rows, &m_sink);
  }

  // Draw the overlay stack into the Screen and LEAVE IT THERE — see trap 6.
  // This is the only way to read a dialog's cells, because frame_step undoes
  // the overlay pass before it returns.
  //
  // ⚠ Not a shortcut for step(): it runs no tick and no on_render, so the
  // frame underneath is whatever the last step() left. Call step() first, or
  // the dialog is composited over a blank screen.
  //
  // render_overlays is upstream's documented test seam ("protected so a test
  // can drive the draw pass without a tty") and is protected at every tag from
  // v0.2.2 to v0.6.0, which is what makes the red-verify below buildable.
  using Shell::render_overlays;
  auto paint_overlay_pass() -> void { render_overlays(screen()); }

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

[[nodiscard]] auto ctrl_c() -> termforge::Event {
  return termforge::Event{termforge::KeyEvent{
      .key = termforge::Key::Char, .ch = U'c', .ctrl = true}};
}

// The same three keys again, carrying a KeyAction (termforge #60, v0.2.2).
//
// ⚠ These construct an event NO TERMINAL CAN SEND US TODAY, and that is the
// point rather than a flaw. KeyAction::Release is only ever delivered under a
// KeyboardMode above Legacy, no game on the roster asks for one yet, and the
// first that does will be Tetris (gitea #7). The Shell's gate against them
// therefore has to be pinned by synthesis or not at all — and "not at all"
// means the gate lands together with the game that makes it reachable, which
// is the bundling gitea #32 exists to avoid.
//
// ⚠ .action is the LAST member of KeyEvent, appended deliberately because the
// upstream parser aggregate-initializes positionally. Designated initializers
// here mean a future field cannot silently shift what we are setting.
[[nodiscard]] auto key_released(termforge::Key k) -> termforge::Event {
  return termforge::Event{termforge::KeyEvent{
      .key = k, .action = termforge::KeyAction::Release}};
}

[[nodiscard]] auto key_repeated(termforge::Key k) -> termforge::Event {
  return termforge::Event{termforge::KeyEvent{
      .key = k, .action = termforge::KeyAction::Repeat}};
}

[[nodiscard]] auto ch_released(char32_t c) -> termforge::Event {
  return termforge::Event{
      termforge::KeyEvent{.key = termforge::Key::Char,
                          .ch = c,
                          .action = termforge::KeyAction::Release}};
}

[[nodiscard]] auto ctrl_c_released() -> termforge::Event {
  return termforge::Event{
      termforge::KeyEvent{.key = termforge::Key::Char,
                          .ch = U'c',
                          .ctrl = true,
                          .action = termforge::KeyAction::Release}};
}

[[nodiscard]] auto click(int x, int y, int button = 0, bool pressed = true)
    -> termforge::Event {
  return termforge::Event{termforge::MouseEvent{
      .x = x, .y = y, .button = button, .pressed = pressed}};
}

// A wheel notch. termforge encodes the wheel as a MouseEvent carrying no
// button (-1) and one of the two scroll flags — NOT as a button number, which
// is the shape an xterm-protocol reflex expects. Same encoding as the one wheel
// event elsewhere in the suite, test/15minesweeper-ui's "the wheel is ignored
// too".
//
// ⚠ pressed stays false. A wheel notch is not a press, and setting it would
// also send the Shell down the m_ring.focus_at() branch, which is a different
// gesture entirely.
[[nodiscard]] auto wheel(int x, int y, bool up) -> termforge::Event {
  return termforge::Event{termforge::MouseEvent{.x = x,
                                                .y = y,
                                                .button = -1,
                                                .scroll_up = up,
                                                .scroll_down = !up}};
}

[[nodiscard]] auto cell_text(Probe& app, int x, int y) -> std::string {
  return app.screen().at(x, y).text;
}

// The whole row, as the screen holds it. Blank cells contribute nothing —
// Screen::fill_rect leaves Cell::text empty rather than writing a space.
[[nodiscard]] auto row_text(Probe& app, int y) -> std::string {
  std::string out;
  for (int x = 0; x < app.screen().cols(); ++x) out += cell_text(app, x, y);
  return out;
}

// Where a word sits, one grapheme per cell, as write_text lays it out.
//
// ⚠ Column-exact, and that is why it does not go through row_text(): blank
// cells contribute nothing there (see its comment), so an offset into that
// string is not a column index — and a dialog row is mostly gaps.
//
// Returns EVERY match rather than the first, so a caller can require there is
// exactly one. On an overlay pass a second hit would mean the backdrop is
// showing through as well as the dialog, which is a different bug wearing the
// same assertion.
struct Hit {
  int x{}, y{};
};

[[nodiscard]] auto find_word(Probe& app, std::string_view word)
    -> std::vector<Hit> {
  std::vector<Hit> hits;
  auto& s = app.screen();
  const int n = static_cast<int>(word.size());
  for (int y = 0; y < s.rows(); ++y) {
    for (int x = 0; x + n <= s.cols(); ++x) {
      bool all = true;
      for (int i = 0; i < n && all; ++i) {
        all = s.at(x + i, y).text == std::string(1, word[static_cast<std::size_t>(i)]);
      }
      if (all) hits.push_back(Hit{x, y});
    }
  }
  return hits;
}

// The cell a button's label starts in — which carries the button's colours,
// because Button::draw fill_rect()s its whole rect in (fg, bg) before writing
// the label in the same pair. Read after paint_overlay_pass(), where the
// dialog is drawn AFTER the backdrop dim, so these are the undimmed values.
[[nodiscard]] auto button_cell(Probe& app, std::string_view label)
    -> termforge::Cell {
  const auto hits = find_word(app, label);
  REQUIRE(hits.size() == 1);
  return app.screen().at(hits[0].x, hits[0].y);
}

// Every byte of every cell on the screen must be 7-bit. Same helper, same
// reason, as test/15minesweeper-ui.
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

// Looked up rather than hardcoded — the roster will grow, and menu order is
// registry order.
[[nodiscard]] auto game_index(std::string_view slug) -> int {
  const auto games = termgame::all_games();
  for (std::size_t i = 0; i < games.size(); ++i) {
    if (games[i].meta.slug == slug) return static_cast<int>(i);
  }
  return -1;
}

[[nodiscard]] auto minesweeper_index() -> int {
  return game_index("minesweeper");
}

[[nodiscard]] auto game_of(const Shell& shell) -> const termgame::Minesweeper* {
  return dynamic_cast<const termgame::Minesweeper*>(shell.current_game());
}

// Enter Minesweeper from a fresh selector.
auto enter_game(Probe& app) -> void {
  app.step();  // give the list its geometry
  const int index = minesweeper_index();
  REQUIRE(index >= 0);
  while (app.selector_index() < index) app.dispatch_event(key(termforge::Key::Down));
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
}

}  // namespace

TEST_CASE("the selector lists every linked game", "[selector]") {
  Probe app;
  REQUIRE(app.selector_item_count() == termgame::all_games().size());
  REQUIRE(app.selector_item_count() > 0);
  REQUIRE(app.state() == Shell::State::Selector);
  REQUIRE(app.current_game() == nullptr);
}

TEST_CASE("arrow keys move the selection", "[selector]") {
  Probe app;
  app.step();
  const int start = app.selector_index();
  REQUIRE(start == 0);

  app.dispatch_event(key(termforge::Key::End));
  REQUIRE(app.selector_index() ==
          static_cast<int>(termgame::all_games().size()) - 1);

  app.dispatch_event(key(termforge::Key::Home));
  REQUIRE(app.selector_index() == 0);

  if (termgame::all_games().size() > 1) {
    app.dispatch_event(key(termforge::Key::Down));
    REQUIRE(app.selector_index() == 1);
    app.dispatch_event(key(termforge::Key::Up));
    REQUIRE(app.selector_index() == 0);
  }
}

// ── The selection marker ────────────────────────────────────────────────────
//
// Until termforge v0.1.11 the Shell drew this itself, into a two-column gutter
// it carved out of the list's rect, and NOTHING in this file asserted it. The
// workaround shipped untested for two epics. Upstream owns the marker now
// (#72, gitea #17), so these are the cases that should have existed all along —
// they pin the behaviour, not the implementation, and would have gone red for
// either one.
//
// Geometry, recomputed the way draw_selector does it for the Probe's 60x20:
// body_y = 1, body_h = h - 3 = 17; with_detail since 60 >= kDetailPaneMinCols;
// list_w = max(24, 60 * 2 / 5) = 24; so the frame is {0,1,24,17} and its
// content_rect() is {1,2,22,15}. The marker sits in the gutter's first column.
constexpr int kMarkX = 1;
constexpr int kRow0 = 2;

// No comma in this name, deliberately: Catch2 splits a command-line test spec
// on commas, so a case whose name contains one cannot be run by name.
TEST_CASE("the selected row is marked in TEXT and not colour",
          "[selector][mark]") {
  // ⚠ The point of the whole case. test_run_frames installs a FallbackDriver,
  // which discards colour outright — so a selection stated only as a theme
  // inversion is invisible here and on any bare TTY. Asserting on cell text is
  // what makes "you can see what is selected" a testable claim.
  Probe app;
  app.step();

  // Anchor the row rather than trusting the constant: if the layout moves, this
  // fails loudly instead of quietly comparing two blank cells.
  REQUIRE(row_text(app, kRow0).find(termgame::all_games().front().meta.title) !=
          std::string::npos);

  // '>' and not '▸' because the fallback tier is the ASCII tier — mark_glyphs()
  // keys off BorderStyle, and Shell::draw_selector passes the one it probed.
  REQUIRE(cell_text(app, kMarkX, kRow0) == ">");

  if (termgame::all_games().size() > 1) {
    // ⚠ .empty(), not " ". fill_rect leaves Cell::text empty; it does not write
    // a space. Asserting " " here fails against a correct implementation.
    REQUIRE(cell_text(app, kMarkX, kRow0 + 1).empty());

    app.dispatch_event(key(termforge::Key::Down));
    app.step();
    REQUIRE(cell_text(app, kMarkX, kRow0).empty());
    REQUIRE(cell_text(app, kMarkX, kRow0 + 1) == ">");
  }
}

TEST_CASE("the selector screen is 7-bit at the ASCII tier", "[selector][mark]") {
  // The only thing that catches a dropped m_list.set_style(style). ListWidget
  // defaults to BorderStyle::Single, whose marker is '▸' (U+25B8) — three bytes
  // of UTF-8 on a terminal that has told us it cannot draw a box. Observed on a
  // pty during gitea #17, not hypothesised.
  Probe app;
  app.step();
  REQUIRE(all_seven_bit(app));
}

TEST_CASE("a click in the marker gutter selects its row", "[selector][mark]") {
  // The workaround's known limitation, inverted into a guarantee. Its gutter
  // was carved out of the list's rect, so a click at kMarkX missed the widget
  // entirely and did nothing; upstream's gutter is inside rect(). This case is
  // red under the old geometry, where the list began at x = 3.
  Probe app;
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);

  app.dispatch_event(click(kMarkX, kRow0));
  REQUIRE(app.state() == Shell::State::InGame);
  REQUIRE(app.current_game() != nullptr);
  REQUIRE(app.current_game()->meta().slug ==
          termgame::all_games().front().meta.slug);
}

TEST_CASE("real escape sequences reach the list", "[selector]") {
  // The one byte-level case: everything else injects Events directly, which
  // would keep passing if the decoder broke. "\x1b[B" is Down.
  Probe app;
  app.step();
  if (termgame::all_games().size() < 2) return;  // nothing to move to yet

  REQUIRE(app.selector_index() == 0);
  app.test_pump({"\x1b[B"});
  REQUIRE(app.selector_index() == 1);
}

TEST_CASE("Enter enters the selected game", "[selector]") {
  Probe app;
  enter_game(app);
  REQUIRE(app.current_game() != nullptr);
  REQUIRE(app.current_game()->meta().slug == "minesweeper");
}

TEST_CASE("every entry gets a fresh game", "[selector][lifetime]") {
  Probe app;
  enter_game(app);

  // Advance the simulation so the first instance has visibly accumulated state.
  const auto* first = game_of(app);
  REQUIRE(first != nullptr);
  app.on_tick(std::chrono::duration<double>{1.0 / Shell::kTickHz});
  REQUIRE(first->ticks() > 0);

  app.dispatch_event(key(termforge::Key::Escape));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
  REQUIRE(app.current_game() == nullptr);

  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);

  // Freshness asserted through observable state, not a pointer comparison: a
  // recycled allocation can hand back the same address and would make a
  // pointer test pass or fail for reasons that have nothing to do with this.
  const auto* second = game_of(app);
  REQUIRE(second != nullptr);
  REQUIRE(second->ticks() == 0);
  REQUIRE(second->elapsed().count() == 0.0);
}

TEST_CASE("Escape quits in the selector but returns to the menu in a game",
          "[selector][escape]") {
  // ⚠ THE regression this epic exists to pin. It works only because
  // Shell::on_event never chains to termforge::App::on_event, whose default
  // quits on Escape. That is a *negative* — a line that must stay absent — and
  // every termforge example ends its on_event with `App::on_event(ev);`. If
  // someone "restores" it, this case is what goes red.
  {
    Probe app;
    app.step();
    REQUIRE(app.running());
    app.dispatch_event(key(termforge::Key::Escape));
    REQUIRE_FALSE(app.running());
  }
  {
    Probe app;
    enter_game(app);
    app.dispatch_event(key(termforge::Key::Escape));
    REQUIRE(app.state() == Shell::State::Selector);
    REQUIRE(app.running());
    app.step();
    REQUIRE(app.current_game() == nullptr);
  }
}

TEST_CASE("Ctrl+C quits from every state", "[selector][escape]") {
  {
    Probe app;
    app.step();
    app.dispatch_event(ctrl_c());
    REQUIRE_FALSE(app.running());
  }
  {
    Probe app;
    enter_game(app);
    app.dispatch_event(ctrl_c());
    REQUIRE_FALSE(app.running());
  }
  {
    // The interesting one: App::dispatch_event routes Ctrl+C past the overlay
    // on purpose, so a paused game must still be killable. Ctrl+C is no longer
    // inherited from App::on_event — the Shell handles it itself — so this is
    // the case that catches someone dropping that branch.
    Probe app;
    enter_game(app);
    app.dispatch_event(ch(U'p'));
    REQUIRE(app.state() == Shell::State::Paused);
    app.dispatch_event(ctrl_c());
    REQUIRE_FALSE(app.running());
  }
}

TEST_CASE("P pauses, and the overlay swallows the game's input",
          "[selector][pause]") {
  Probe app;
  enter_game(app);

  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);
  REQUIRE(app.overlay_count() == 1);
  REQUIRE(app.modal());

  // 'f' is Minesweeper's flag key. While paused it must never reach the game —
  // proof that the pause suspends input rather than merely suspending ticks.
  // Asserting on the model rather than on "we are still paused" is what makes
  // this stronger than the diagnostic it replaced: the flag either moved the
  // mine counter or it did not, and nothing else could have moved it.
  const auto* game = game_of(app);
  REQUIRE(game != nullptr);
  const int before = game->board().mines_remaining();
  app.dispatch_event(ch(U'f'));
  app.step();
  REQUIRE(app.state() == Shell::State::Paused);
  REQUIRE(app.current_game() != nullptr);
  REQUIRE(game->board().mines_remaining() == before);
}

TEST_CASE("Escape resumes from pause", "[selector][pause]") {
  Probe app;
  enter_game(app);
  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);

  // Dialog::on_escape cancels, i.e. answers "Resume".
  app.dispatch_event(key(termforge::Key::Escape));
  REQUIRE(app.state() == Shell::State::InGame);
  REQUIRE(app.overlay_count() == 0);
  REQUIRE(app.current_game() != nullptr);
}

// ── the pause dialog's border tier (gitea #44) ───────────────────────────────
//
// The dialog was the one widget in the app the tier never reached. termforge's
// Dialog owns a Frame privately and that Frame defaults to BorderStyle::Single,
// which is a family sync_capabilities() NEVER chooses — its two answers are
// Ascii and Rounded. Nothing set it, so the pause dialog painted U+250C/U+2500/
// U+2502 onto terminals that had just reported no colour, in every release since
// the dialog existed.
//
// ⚠ This could not be asserted before gitea #36. See trap 6: frame_step restores
// the backdrop before it returns, so a sweep after step() was sweeping the GAME.
// paint_overlay_pass() is what makes the dialog's cells readable at all, and it
// was deliberately left unspent there — an unrelated red inside a pin bump
// destroys the "nothing else moved" signal that is the point of doing one alone.

TEST_CASE("the pause dialog is 7-bit at the ASCII tier",
          "[selector][pause][render]") {
  Probe app;
  enter_game(app);
  app.step();  // a real game frame to composite the dialog over — trap 6
  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);
  app.paint_overlay_pass();

  // ⚠ THE CONTROLS COME FIRST, and not as tidiness. all_seven_bit() below passes
  // on a screen with no dialog on it at all — it does exactly that in four other
  // cases in this file — and the backdrop here is the Minesweeper board, which
  // test/15minesweeper-ui already pins 7-bit. So a green sweep on its own is
  // fully consistent with "the overlay was never drawn", which is what happens
  // if paint_overlay_pass() is dropped or moved above the step(). A REQUIRE
  // aborts its case, so putting these first turns that into its own message
  // instead of a vacuous pass. "Paused" is the Frame's own title, so it proves
  // the widget under test drew; "Resume" is a Button label, so it proves
  // draw_content ran too.
  const auto title = find_word(app, "Paused");
  REQUIRE(title.size() == 1);
  REQUIRE(find_word(app, "Resume").size() == 1);

  // Frame::draw writes the title as ONE string — title_left, space, the title,
  // space, title_right — starting at r.x + 1. Every border family's delimiter is
  // one column wide, so the 'P' sits at r.x + 3: two columns left of it is the
  // opening delimiter, and three is the frame's top-left corner. ⚠ Mind the
  // space between them; -1 is blank and an assertion there tests nothing.
  //
  // ⚠ Anchored to the title's position rather than searched for globally. The
  // Minesweeper board underneath is drawn at the same tier and contributes its
  // own '+' and '|', so a count or a find_word() of either proves nothing about
  // the dialog. Same trap the scrollbar-thumb case below had to avoid.
  //
  // Two glyphs, two causes. The corner is the box-drawing ring the issue was
  // filed about; the delimiter is a SECOND leak from the same table that the
  // issue did not mention, because Frame takes title_left/title_right out of
  // border_glyphs(style) as well. Single and Rounded both give ┤/├ there.
  REQUIRE(cell_text(app, title[0].x - 2, title[0].y) == "|");
  REQUIRE(cell_text(app, title[0].x - 3, title[0].y) == "+");

  // And nothing else in the dialog leaked either.
  REQUIRE(all_seven_bit(app));
}

TEST_CASE("the pause dialog stays 7-bit at every window size",
          "[selector][pause][render]") {
  // ⚠ NOT tier coverage — the glyph families do not vary with size, so this
  // cannot say anything the case above did not. What it covers is Dialog::layout
  // clamping to the screen and Frame dropping its title when the budget runs
  // out: at the Shell's own 20x8 floor the dialog is clamped to the full width,
  // and "Paused" survives on 14 columns of title budget by four. A change that
  // narrowed the chrome would take the control out with it.
  for (const auto& [cols, rows] :
       {std::pair{20, 8}, std::pair{40, 12}, std::pair{60, 20},
        std::pair{80, 24}}) {
    INFO("size: " << cols << "x" << rows);
    Probe app;
    enter_game(app);  // enters at the default 60x20; the size below is the paint
    app.step(1, cols, rows);
    app.dispatch_event(ch(U'p'));
    REQUIRE(app.state() == Shell::State::Paused);
    app.paint_overlay_pass();

    REQUIRE(find_word(app, "Paused").size() == 1);  // the control, as above
    REQUIRE(all_seven_bit(app));
  }
}

// ── the pause dialog's press flash (gitea #36) ───────────────────────────────
//
// Two cases, and they are deliberately NOT one. The pin bump v0.2.2 -> v0.6.0
// crosses two upstream tags that both touch this, and the only way to say which
// of them did what is to make the two claims fail independently — a REQUIRE
// aborts its case, so a single case can only ever report the first one.
//
// The mechanism. Activating a ConfirmDialog's button arms a press flash and
// closes the dialog in the SAME dispatch, so the flash never renders in the
// showing that armed it. What happens to it afterwards is what moved:
//
//              first paint of showing 2   second paint   what clears it
//   v0.2.2     LIT                        clear          draw(), after painting
//   v0.4.0     LIT                        LIT            nothing — see below
//   v0.6.0     clear                      clear          reset_transient()
//
// v0.4.0 (#69) rebuilt the flash as a wall-clock countdown in Widget::on_tick.
// App keeps no widget registry and Shell::on_tick forwards only to m_game, so
// nothing ticks m_pause and the countdown never runs: the flash becomes
// permanent for the life of the process. v0.5.0 (#122) added
// Widget::reset_transient(), which Dialog::draw() calls at its per-showing
// boundary BEFORE anything paints, and Button implements by zeroing the flash.
//
// ⚠ What the two red arms actually proved, and it corrects the issue. gitea #36
// assumed v0.4.0 introduces this. It does not — v0.2.2, the pin we ship TODAY,
// already paints one frame of a wrongly-lit Resume button every time the pause
// dialog re-opens. v0.4.0 would have made that permanent; v0.5.0 cures both.
// So the bump does not merely avoid a regression, it fixes a live defect, and
// that is the one thing this bump buys beyond v0.3.0's draw_image contract.
//
// We rely on the boundary rather than forwarding ticks — see the comment at
// Shell::on_tick's pause gate for why — so these cases pin upstream's BEHAVIOUR
// in our tree rather than trusting the doc comment that describes it.
//
// ⚠ Neither case can go red for ADDING tick_widgets(dt, {&m_pause}); that is
// harmless and stays green. Only the comment at the call site guards that.

namespace {

// Drive to the first paint of a second showing of the pause dialog, with the
// Resume button having been activated in the first. Returns showing 1's cells
// so a case can assert against the same button before it was ever pressed.
struct PauseSample {
  termforge::Cell resume, menu;
};

auto pause_activate_and_reopen(Probe& app) -> PauseSample {
  enter_game(app);
  app.step();  // a real game frame for the dialog to composite over — trap 6

  // ── showing 1: the control. Nothing has been activated yet.
  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);
  app.paint_overlay_pass();

  const PauseSample before{button_cell(app, "Resume"),
                           button_cell(app, "Menu")};

  // ⚠ The sanity guard, and both cases are worthless without it. It proves the
  // reader returns a real, state-dependent colour rather than one constant for
  // every cell — Shell::open_pause calls set_default(false), so Resume is
  // focused and Menu is not, and Button::draw paints those differently. Drop
  // this and everything below passes against a screen of blank Cells.
  REQUIRE(before.resume.bg != before.menu.bg);

  // ⚠ ENTER, not 'y'. ConfirmDialog::on_event answers Y/N *before* the focus
  // ring and calls finish() directly, and Escape goes through on_escape() —
  // neither ever reaches Button::on_event, so neither arms the flash these
  // cases are about. Every other pause case in this file uses one of those two
  // and is structurally blind to it. Enter goes through the ring to the focused
  // button, which set_default(false) made Resume.
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);
  REQUIRE(app.overlay_count() == 0);

  // Trap 1: the result latch clears on the next draw, so the second showing
  // needs a frame between it and the answer.
  app.step();

  // ── showing 2: same dialog, same focus, one activation ago.
  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);
  app.paint_overlay_pass();
  return before;
}

}  // namespace

// The contract itself. Red at v0.2.2 AND v0.4.0, green from v0.5.0.
TEST_CASE("a dialog button is not still lit at the next showing",
          "[selector][pause][render]") {
  Probe app;
  const PauseSample before = pause_activate_and_reopen(app);

  // ⚠ A differential, never a comparison against upstream's pressed palette.
  // m_pressed_fg/m_pressed_bg are private defaults upstream is free to retheme,
  // and a case that hardcoded them would fail on a theme change while the
  // behaviour was still correct. Two showings of the same button in the same
  // focus state must look the same; what they look like is not our business.
  REQUIRE(button_cell(app, "Resume").bg == before.resume.bg);
  REQUIRE(button_cell(app, "Resume").fg == before.resume.fg);

  // And the unpressed neighbour did not move either, which distinguishes "the
  // flash was cleared" from "the whole dialog is being drawn in one colour".
  REQUIRE(button_cell(app, "Menu").bg == before.menu.bg);
}

// The duration, which is a different claim. Green at v0.2.2 — draw() cleared
// the flag right after painting with it, so the stale flash cost exactly one
// frame — and red at v0.4.0, where nothing clears it at all. This is the case
// that says v0.4.0 changed the SEVERITY rather than introducing the bug, and
// without it the two red arms above are indistinguishable.
TEST_CASE("a stale press flash does not outlive one paint",
          "[selector][pause][render]") {
  Probe app;
  const PauseSample before = pause_activate_and_reopen(app);

  app.paint_overlay_pass();  // the second paint of the same showing
  REQUIRE(button_cell(app, "Resume").bg == before.resume.bg);
}

TEST_CASE("the pause dialog's Menu answer returns to the selector",
          "[selector][pause]") {
  Probe app;
  enter_game(app);
  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);

  // ConfirmDialog's unconditional Y hotkey = the "yes" button, labelled "Menu".
  app.dispatch_event(ch(U'y'));

  // ⚠ BEFORE the step, not after — see trap 4. "Menu" must return to the
  // selector, not quit the app, and the step below re-arms m_running and would
  // hide it if it did.
  REQUIRE(app.running());

  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
  REQUIRE(app.current_game() == nullptr);
  REQUIRE(app.overlay_count() == 0);
}

TEST_CASE("a game can end itself", "[selector][lifetime]") {
  // Minesweeper's done() means "the player accepted a finished board", so the
  // board has to actually be finished. load_mines installs an exact layout, and
  // opening the single mine loses immediately — no dependence on the RNG.
  Probe app;
  enter_game(app);
  auto* game = const_cast<termgame::Minesweeper*>(game_of(app));
  REQUIRE(game != nullptr);
  const termgame::minesweeper::Coord mines[]{{3, 3}};
  game->board().load_mines(mines);
  game->board().reveal({.row = 3, .col = 3});
  REQUIRE(game->board().finished());

  app.dispatch_event(key(termforge::Key::Enter));  // accept the result
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
  REQUIRE(app.current_game() == nullptr);
}

TEST_CASE("a game can request the menu from inside its own event handler",
          "[selector][lifetime]") {
  // ⚠ This is a use-after-free probe, and it is only worth anything under the
  // sanitizer toolchains — run it there. Minesweeper's 'q' calls
  // GameContext::quit_to_menu() from inside on_event; a Shell that honoured
  // that synchronously would destroy the game while its handler's frame was
  // still live. The deferred flag in GameContext exists for exactly this.
  Probe app;
  enter_game(app);
  app.dispatch_event(ch(U'q'));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
  REQUIRE(app.current_game() == nullptr);
}

TEST_CASE("the selector survives entering and leaving repeatedly",
          "[selector][lifetime]") {
  Probe app;
  for (int i = 0; i < 5; ++i) {
    enter_game(app);
    app.dispatch_event(key(termforge::Key::Escape));

    // ⚠ BEFORE the step — see trap 4. Escape inside a game means "back to the
    // menu", never "quit", and that is the escape rule this whole file exists
    // to pin. Asserted after the step it is vacuous on every iteration.
    REQUIRE(app.running());

    app.step();
    REQUIRE(app.state() == Shell::State::Selector);
    REQUIRE(app.current_game() == nullptr);
  }
}

// ── Menu SFX ────────────────────────────────────────────────────────────────
//
// Asserted through Shell::audio().play_count(), which counts INTENT rather than
// sound. Two consequences worth knowing before extending these:
//
//   * they pass identically on the TERMGAME_WITH_AUDIO=OFF arm CI runs, because
//     a NullSink Shell still records what was asked for; and
//   * no sink injection is needed anywhere, so nothing here touches the disk.
//
// This note used to record two things as UNTESTABLE on a one-game roster: that
// MenuMove could never be positively asserted, and that the
// `m_state == State::Selector` guard in both handlers was consequently blind.
// Epic 4 registered a second game and both went live — "moving the selection
// plays the move sound" and "a click on a NON-selected row plays select and not
// move" below are those two cases. Kept in outline because the shape recurs:
// coverage that depends on the roster's LENGTH reads as coverage that exists.
//
// ⚠ The wheel is no longer one of the gestures that can move the selection at
// all, and this is where that stops being obvious. Before termforge v0.2.0,
// ListWidget answered a wheel notch with set_selected(selected ± 3) — so with
// two entries a wheel-down DID move the selection and DID fire MenuMove. #35
// unified the wheel onto the view offset instead. Both halves of the new
// contract are asserted below rather than left to the release note, because
// nothing else in this file can see the difference: every other case passed
// identically before and after the bump.
//
// ⚠ What is still deferred, with a condition. The wheel's POSITIVE half — that
// a notch moves ListWidget::scroll_offset() — needs a roster longer than the
// pane, and there is no size at which two entries overflow: the Shell's floor
// is 20x8, which leaves the list three interior rows. all_games() is a
// file-local constexpr table with no injection seam. Revisit when the roster
// reaches four entries (Epic 6), or when a test-only substitute for the
// term-game_roster target earns its keep — src/lib/CMakeLists.txt already makes
// the roster its own archive, so the seam exists at the link level. Until then
// the negative half is the whole assertion, and it is the load-bearing one:
// what changed under us is that the selection stopped moving.

TEST_CASE("entering a game plays the select sound", "[selector][audio]") {
  using termgame::audio::SfxId;

  Probe app;
  app.step();
  REQUIRE(app.audio().play_count(SfxId::MenuSelect) == 0);

  app.dispatch_event(key(termforge::Key::Enter));

  REQUIRE(app.state() == Shell::State::InGame);
  REQUIRE(app.audio().play_count(SfxId::MenuSelect) == 1);
}

TEST_CASE("a click that enters a game plays select, not move",
          "[selector][audio]") {
  // One gesture, one sound. ⚠ This does NOT currently discriminate the State
  // guard in the mouse path — see the note above; with one game the click does
  // not move the selection, so MenuMove stays silent either way. What it does
  // pin is that entering by mouse and entering by Enter agree, which is the
  // property that would break if the two paths ever grew separate bindings.
  using termgame::audio::SfxId;

  Probe app;
  app.step();  // ListWidget::rect() is only set inside on_render — trap 2

  // ⚠ The same coordinates as "a click in the marker gutter selects its row"
  // above, and unconditionally asserted. An `if (state == InGame)` here would
  // pass silently the day the layout moves and the click stops landing — which
  // is precisely the shape of test that reports green while checking nothing.
  app.dispatch_event(click(kMarkX, kRow0));

  REQUIRE(app.state() == Shell::State::InGame);
  REQUIRE(app.audio().play_count(SfxId::MenuSelect) == 1);
  REQUIRE(app.audio().play_count(SfxId::MenuMove) == 0);
}

TEST_CASE("a click on a NON-selected row plays select and not move",
          "[selector][audio][mark]") {
  // ⚠ THE case the two `m_state == State::Selector` guards were waiting for, and
  // the reason the note above says they shipped untested for two epics.
  //
  // The existing click case targets row 0, which is ALREADY selected — so the
  // selection does not move, MenuMove stays silent with or without the guard, and
  // the case is blind to it. Established by mutation, not assumed: deleting the
  // guard from the mouse path left this whole file green.
  //
  // A click on row 1 is the gesture that discriminates. ListWidget moves the
  // selection AND fires on_select inside the same route_mouse() call, so by the
  // time control returns the Shell is already InGame. Without the state guard that
  // one gesture emits BOTH sounds; with it, only MenuSelect. One gesture, one
  // sound.
  if (termgame::all_games().size() < 2) return;

  using termgame::audio::SfxId;

  Probe app;
  app.step();  // ListWidget::rect() is only set inside on_render — trap 2
  REQUIRE(app.selector_index() == 0);

  app.dispatch_event(click(kMarkX, kRow0 + 1));

  REQUIRE(app.state() == Shell::State::InGame);
  REQUIRE(app.current_game() != nullptr);
  // It entered the SECOND game, which is also what proves the click landed on
  // row 1 rather than being clamped back to row 0.
  REQUIRE(app.current_game()->meta().slug ==
          termgame::all_games()[1].meta.slug);

  REQUIRE(app.audio().play_count(SfxId::MenuSelect) == 1);
  REQUIRE(app.audio().play_count(SfxId::MenuMove) == 0);
}

TEST_CASE("the wheel scrolls the view and does not move the selection",
          "[selector][wheel]") {
  // ⚠ RED-VERIFIED against the previous pin, which is the only reason this case
  // is worth anything: `cmake -B build-oldpin -DTERMFORGE_TAG=v0.1.15` builds
  // the suite against termforge v0.1.15, where ListWidget answers a wheel notch
  // with set_selected(selected ± 3). With two entries that clamps 0 -> 1 and
  // this REQUIRE fails. Every other case in this file passes on both pins.
  //
  // What it pins going forward is the decision, not just the framework: the
  // wheel scrolls the view, the selection stays put, and the Shell does not
  // reach back in to re-implement the old behaviour. See the mouse branch in
  // src/lib/arcade/shell.cpp.
  Probe app;
  app.step();  // ListWidget::rect() is only set inside on_render — trap 2
  REQUIRE(app.selector_index() == 0);

  // Down first: from row 0 this is the direction with somewhere to go, so it is
  // the notch the old behaviour would have moved. Up afterwards would clamp at
  // the top and prove nothing on its own.
  app.dispatch_event(wheel(kMarkX, kRow0, /*up=*/false));
  REQUIRE(app.selector_index() == 0);

  app.dispatch_event(wheel(kMarkX, kRow0, /*up=*/true));
  REQUIRE(app.selector_index() == 0);

  // And the wheel is not a way into a game either — it never fired on_select,
  // before or after #35, but a Shell that "helpfully" re-bound it could.
  REQUIRE(app.state() == Shell::State::Selector);
}

TEST_CASE("the wheel plays no sound", "[selector][wheel][audio]") {
  // The other half, and it is a claim about OUR code rather than termforge's:
  // MenuMove is edge-detected on the selection, so a wheel that moves nothing
  // must be silent. Binding the sound to the gesture instead — the mistake the
  // key path's comment warns about — would blip on every notch of a wheel that
  // did nothing at all.
  using termgame::audio::SfxId;

  Probe app;
  app.step();

  app.dispatch_event(wheel(kMarkX, kRow0, /*up=*/false));
  app.dispatch_event(wheel(kMarkX, kRow0, /*up=*/true));

  REQUIRE(app.audio().play_count(SfxId::MenuMove) == 0);
  REQUIRE(app.audio().play_count(SfxId::MenuSelect) == 0);
}

TEST_CASE("a key that moves nothing makes no sound", "[selector][audio]") {
  // ⚠ THE case, given a one-game roster — and the one that stays meaningful
  // however long the roster grows, because Up at the top and Down at the bottom
  // always clamp. Binding the arrow keys directly instead of edge-detecting the
  // selection would make every one of these blip.
  using termgame::audio::SfxId;

  Probe app;
  app.step();
  REQUIRE(app.selector_index() == 0);

  app.dispatch_event(key(termforge::Key::Up));    // already at the top
  app.dispatch_event(key(termforge::Key::Home));  // already at Home
  REQUIRE(app.selector_index() == 0);
  REQUIRE(app.audio().play_count(SfxId::MenuMove) == 0);

  app.dispatch_event(key(termforge::Key::End));
  if (termgame::all_games().size() == 1) {
    // End on a one-item list is also a no-op, so still silent.
    REQUIRE(app.audio().play_count(SfxId::MenuMove) == 0);
  }
}

TEST_CASE("moving the selection plays the move sound", "[selector][audio]") {
  // Behind the roster guard — see the note above. This is the positive half,
  // and it starts running for free the moment a second game is registered.
  if (termgame::all_games().size() < 2) return;

  using termgame::audio::SfxId;

  Probe app;
  app.step();

  app.dispatch_event(key(termforge::Key::Down));
  REQUIRE(app.selector_index() == 1);
  REQUIRE(app.audio().play_count(SfxId::MenuMove) == 1);
  REQUIRE(app.audio().play_count(SfxId::MenuSelect) == 0);

  app.dispatch_event(key(termforge::Key::Up));
  REQUIRE(app.audio().play_count(SfxId::MenuMove) == 2);
}

TEST_CASE("a silent build still records what was asked for",
          "[selector][audio]") {
  // ⚠ What makes every assertion above arm-independent. A default Shell holds a
  // NullSink, so play() posts nothing and the ring stays empty — but the intent
  // counters still move. If Engine::play() ever stopped short-circuiting, the
  // dropped() counter here would start climbing instead.
  using termgame::audio::SfxId;

  Probe app;
  app.step();
  app.dispatch_event(key(termforge::Key::Enter));

  REQUIRE(app.audio().play_count(SfxId::MenuSelect) == 1);

  const auto stats = app.audio().stats();
  REQUIRE(stats.pushed == 0);
  REQUIRE(stats.dropped == 0);
  REQUIRE(stats.silenced >= 1);
}

// ── The keyboard tier, and the Shell's gate on KeyAction (gitea #32) ─────────
//
// Everything below is about a contract that has no consumer yet: no game on
// this roster asks for a KeyboardMode above Legacy, so no terminal will ever
// hand this Shell a Release. That is precisely why the cases exist here and
// now — the gate has to be correct BEFORE the first game turns the tier on,
// because after that a bug in it is indistinguishable from a bug in the game.
//
// ⚠ What these cases CANNOT show is that Enhanced works. This container's
// terminal has no kitty keyboard protocol, and test_run_frames installs a
// FallbackDriver whose capabilities() are all false — so every arm exercised
// anywhere in this suite is the degraded one. See STATUS.md.

TEST_CASE("a released Escape does not quit to the menu a second time",
          "[selector][keyboard]") {
  // ⚠ THE BUG THIS CHANGE EXISTS TO PREVENT. Under Enhanced, one press of
  // Escape delivers two events: the press, which means "back to the menu", and
  // the release. Ungated, the release finds the Shell already in the selector
  // and quit_app()s — so a player leaving Tetris would leave the program.
  Probe app;
  enter_game(app);

  app.dispatch_event(key_released(termforge::Key::Escape));
  REQUIRE(app.state() == Shell::State::InGame);
  REQUIRE(app.current_game() != nullptr);

  // The press still does what it always did. Asserted in the same case rather
  // than a sibling: "the release is ignored" is worthless without a control
  // proving the key is otherwise live on this path.
  app.dispatch_event(key(termforge::Key::Escape));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
}

TEST_CASE("a released Escape in the selector does not quit the app",
          "[selector][keyboard]") {
  Probe app;
  app.step();

  app.dispatch_event(key_released(termforge::Key::Escape));
  // ⚠ Asserted BEFORE the next step() — trap 4. running() is re-armed on entry
  // to test_run_frames, so after a step it is true whatever the key did.
  REQUIRE(app.running());
  REQUIRE(app.state() == Shell::State::Selector);
}

TEST_CASE("a released Ctrl+C does not quit the app", "[selector][keyboard]") {
  // The break-glass is the worst one to double-fire: it is routed past the
  // overlay stack on purpose, so it is reachable from every state including a
  // modal, and it takes the whole program with it.
  Probe app;
  app.step();

  app.dispatch_event(ctrl_c_released());
  REQUIRE(app.running());

  app.dispatch_event(ctrl_c());
  REQUIRE_FALSE(app.running());
}

TEST_CASE("a released p does not open the pause dialog",
          "[selector][keyboard]") {
  Probe app;
  enter_game(app);

  app.dispatch_event(ch_released(U'p'));
  REQUIRE(app.state() == Shell::State::InGame);

  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);
}

TEST_CASE("a repeated arrow still moves the selection, a released one does not",
          "[selector][keyboard]") {
  // ⚠ THIS CASE GUARDS A PAIR OF CHANGES, NEITHER OF WHICH IS VISIBLE ALONE,
  // and that is worth stating because mutation testing is what established it.
  // The two tidies a future reader will reach for are (a) writing the Shell's
  // gate as `action == Press`, which reads cleaner, and (b) hoisting the gate
  // to the top of handle_selector_key instead of applying it at the Escape
  // branch. Each on its own leaves every case in this file green — (a) because
  // Press and Repeat mean the same thing for the only keys the Shell owns, (b)
  // because the ring drops releases without our help. Together they stop a held
  // Down from scrolling the menu, with no error anywhere.
  //
  // ⚠ The second half also pins UPSTREAM's contract, not ours: FocusRing::
  // handle_key returns false for a Release (focus_ring.cpp:50) so that a widget
  // cannot insert twice per keystroke. If a future pin bump changed that, the
  // selection would move on the way up as well as the way down, and this is
  // where that would surface.
  if (termgame::all_games().size() < 2) return;

  Probe app;
  app.step();
  REQUIRE(app.selector_index() == 0);

  app.dispatch_event(key_repeated(termforge::Key::Down));
  REQUIRE(app.selector_index() == 1);

  app.dispatch_event(key_released(termforge::Key::Down));
  REQUIRE(app.selector_index() == 1);
}

TEST_CASE("a released key still reaches the running game",
          "[selector][keyboard]") {
  // ⚠ THE OTHER DIRECTION OF THE GATE, and it went untested until a mutation
  // that hoisted shell_may_act() above the game's refusal in
  // handle_in_game_key came back green. The placement is the whole point: a
  // game that asks for KeyboardMode::Enhanced asks to see releases — that is
  // the only reason to ask — so the Shell must stop acting on them itself
  // without stopping them from arriving. A gate above m_game->on_event would
  // take away exactly the thing the tier exists to deliver, and Tetris's DAS
  // would never see a key come up.
  //
  // Minesweeper is the vehicle because it is what this file already enters and
  // its arrow keys move an observable cursor. It does not inspect KeyAction —
  // no game does yet — which is precisely why a release arriving is visible as
  // a cursor move.
  Probe app;
  enter_game(app);

  const auto* game = game_of(app);
  REQUIRE(game != nullptr);
  const auto before = game->cursor();

  app.dispatch_event(key_released(termforge::Key::Right));

  const auto after = game_of(app)->cursor();
  REQUIRE(after.col == before.col + 1);
  REQUIRE(after.row == before.row);
}

TEST_CASE("the keyboard tier is the game's, and is given back on the way out",
          "[selector][keyboard]") {
  // ⚠ THIS CASE CHANGED SHAPE WHEN TETRIS LANDED, and the way it changed is the
  // point. gitea #32 shipped the mode-switching branch with NO consumer: every
  // roster entry declared Legacy, deleting either set_keyboard_mode left the
  // whole suite green, and the branch had to be red-verified in a pty by
  // flipping a game to Enhanced in a scratch tree. This is that flip, made
  // permanent and legitimate by a game that genuinely wants the tier.
  //
  // So: a game that asks gets it, a game that does not is unaffected, and the
  // selector is back on Legacy either way. Minesweeper is the control — without
  // it, "entering a game sets Enhanced" would also pass if the Shell simply set
  // Enhanced on every entry, which is the bug the per-game field exists to
  // prevent.
  Probe app;
  REQUIRE(app.keyboard_mode() == termforge::KeyboardMode::Legacy);

  // The control: a Legacy game must not move the tier.
  enter_game(app);
  REQUIRE(app.keyboard_mode() == termforge::KeyboardMode::Legacy);
  app.dispatch_event(key(termforge::Key::Escape));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
  REQUIRE(app.keyboard_mode() == termforge::KeyboardMode::Legacy);

  // And the consumer.
  const int index = game_index("tetris");
  REQUIRE(index >= 0);
  REQUIRE(termgame::all_games()[static_cast<std::size_t>(index)].meta.keyboard ==
          termforge::KeyboardMode::Enhanced);

  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);
  REQUIRE(app.keyboard_mode() == termforge::KeyboardMode::Enhanced);

  app.dispatch_event(key(termforge::Key::Escape));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
  // ⚠ The half that is easy to omit. A Shell that set the tier and never
  // restored it would pass every assertion above and leave the selector — and
  // then every Legacy game entered after it — reading CSI-u.
  REQUIRE(app.keyboard_mode() == termforge::KeyboardMode::Legacy);
}

TEST_CASE("a terminal without the protocol is told, once, on entry",
          "[selector][keyboard]") {
  // The other half of gitea #32 that only a consumer can reach: upstream emits
  // its fallback ErrorEvent from App::setup(), which has long returned by the
  // time a game entry sets a mode, so the Shell has to raise its own.
  //
  // ⚠ This asserts the DEGRADED arm, and it is the only arm anything here can
  // reach: test_run_frames installs a FallbackDriver whose capabilities() are
  // all false, so kitty_keyboard is false and the notice must fire. A terminal
  // that HAS the protocol is not testable from this container at all.
  const int index = game_index("tetris");
  REQUIRE(index >= 0);

  Probe app;
  app.step();
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);

  // The notice reaches the selector's notice row, which is h - 2 — the LAST row
  // is the key hints. Reading rows() - 1 finds the hints and an empty search,
  // which is indistinguishable from "the notice never fired".
  app.dispatch_event(key(termforge::Key::Escape));
  app.step();
  const std::string notice = row_text(app, app.screen().rows() - 2);
  REQUIRE(notice.find("kitty keyboard protocol") != std::string::npos);

  // ⚠ The control. Without it this case would still pass if the Shell raised
  // the notice on EVERY game entry rather than only for one that asked for a
  // tier it could not have — which is the more likely mistake, since the
  // capability is false for every game here.
  const int mines = minesweeper_index();
  REQUIRE(mines >= 0);
  Probe other;
  other.step();
  while (other.selector_index() < mines) {
    other.dispatch_event(key(termforge::Key::Down));
  }
  other.dispatch_event(key(termforge::Key::Enter));
  other.dispatch_event(key(termforge::Key::Escape));
  other.step();
  const std::string quiet = row_text(other, other.screen().rows() - 2);
  REQUIRE(quiet.find("kitty keyboard protocol") == std::string::npos);
}

// ── What the fourth game turned on ──────────────────────────────────────────
//
// Both cases below were deferred with a condition, in this file and in
// STATUS.md, from the v0.2.2 pin bump onward: "revisit when the roster reaches
// four entries". Tetris is the fourth.
//
// ⚠ The condition as written was necessary but NOT sufficient, and that was
// measured rather than noticed. The selector's interior list rows are h - 5, so
// four entries overflow only when h - 5 < 4, i.e. at rows == 8 — the Shell's
// floor exactly, and a size the probe could not reach until step() took one.
// Registering a fourth game and stopping there would have left both assertions
// as unreachable as they were before.

TEST_CASE("a wheel notch scrolls the view without moving the selection",
          "[selector][wheel]") {
  // ⚠ THE POSITIVE HALF of termforge v0.2.0's wheel change (#35), which this
  // file has only ever been able to assert the negative of. Before #35 a notch
  // was set_selected(selected ± 3); now it is a view offset, and the selection
  // stays put and may scroll out of sight. The negative half — that the
  // selection does NOT move and no MenuMove sounds — has been asserted since
  // the bump. This is the half that needs a roster longer than the pane.
  if (termgame::all_games().size() < 4) return;

  using termgame::audio::SfxId;
  Probe app;
  app.step(1, 60, 8);  // three interior rows, four entries: it overflows

  REQUIRE(app.selector_index() == 0);
  const auto moves = app.audio().play_count(SfxId::MenuMove);

  // ⚠ Coordinates inside the list, and after a frame — traps 2 and 5. A wheel
  // event outside the list's rect never reaches the list at all.
  app.dispatch_event(wheel(kMarkX, kRow0, false));

  REQUIRE(app.selector_index() == 0);
  REQUIRE(app.audio().play_count(SfxId::MenuMove) == moves);
}

TEST_CASE("the scrollbar is 7-bit when the roster overflows the pane",
          "[selector][render]") {
  // ⚠ THE SECOND, PREVIOUSLY INVISIBLE REASON m_list.set_style(style) is
  // load-bearing in draw_selector. termforge v0.2.1 gave ListWidget a
  // one-column scrollbar that reads its track and thumb from
  // scrollbar_glyphs(style) — keyed off the SAME BorderStyle enum as the
  // selection marker, so it is '|' and '#' under Ascii and '│' and '█' under
  // every other family.
  //
  // Until a roster overflowed its pane no scrollbar was ever drawn at any size
  // the 7-bit sweep could reach, so "the bottom-tier case passes without that
  // line" was not evidence of anything. It is now.
  if (termgame::all_games().size() < 4) return;

  Probe app;
  app.step(1, 60, 8);
  REQUIRE(all_seven_bit(app));

  // And the strip is actually there to have been checked — otherwise this is
  // the same vacuous pass it has been for three epics. The ASCII thumb is '#',
  // which appears nowhere else on this screen: the Ascii frame family draws
  // '+', '-' and '|'.
  bool thumb = false;
  for (int y = 0; y < app.screen().rows(); ++y) {
    if (row_text(app, y).find('#') != std::string::npos) thumb = true;
  }
  REQUIRE(thumb);
}

TEST_CASE("the detail pane's scrollbar is 7-bit too", "[selector][render]") {
  // ⚠ A REAL BUG, found by the case above rather than by reading. termforge
  // v0.2.1 gave the shared scrollbar to ListWidget, Table AND TextBox;
  // draw_selector set the style on the list and not on the detail pane, so the
  // pane painted '│' and '█' onto a terminal that had just reported no colour.
  //
  // ⚠ It had been in the tree since the v0.2.2 bump and needed no fourth game
  // to be reachable — only a window short enough for a description to overflow,
  // which is a thing a real player has and a hardcoded 60x20 probe does not.
  // Two deferrals shared one condition and only one of them was actually
  // waiting on it.
  //
  // Short and WIDE: the detail pane only exists at >= 48 columns, so a case
  // that narrowed the window instead would remove the pane and assert nothing.
  for (const int rows : {8, 9, 10, 12}) {
    Probe app;
    app.step(1, 60, rows);
    INFO("rows = " << rows);
    REQUIRE(all_seven_bit(app));
  }
}

// ── The detail pane advertises a game's settings (gitea #38) ────────────────
//
// ⚠ THIS IS THE HALF A GAME-SIDE-ONLY FIX WOULD HAVE LEFT STANDING. #38 names
// two defects: the options are in the wrong place in time, AND the one screen
// you see before pressing Enter never mentions they exist. Snake's description
// says "Three difficulties" and nothing says how to choose one. The schema is
// read by the Shell for exactly this, and by the game for the screen itself --
// one source, so the menu cannot advertise an option the game does not have.

TEST_CASE("the detail pane names each game's options", "[selector][options]") {
  Probe app;
  app.step(1, 80, 24);

  const auto pane_text = [&app]() {
    std::string all;
    for (int y = 0; y < 24; ++y) all += row_text(app, y) + "\n";
    return all;
  };

  // Minesweeper is index 0 and declares a Level with three named choices.
  {
    const std::string all = pane_text();
    INFO(all);
    CHECK(all.find("options:") != std::string::npos);
    CHECK(all.find("Level") != std::string::npos);
    CHECK(all.find("Easy") != std::string::npos);
    CHECK(all.find("Hard") != std::string::npos);
  }

  // ⚠ 2048 is next, declares nothing, and the pane must say nothing extra.
  // Without this the case would pass against a Shell that printed an "options:"
  // header unconditionally -- an empty promise is worse than silence.
  app.dispatch_event(key(termforge::Key::Down));
  app.step(1, 80, 24);
  {
    const std::string all = pane_text();
    INFO(all);
    REQUIRE(all.find("2048") != std::string::npos);  // we really did move
    CHECK(all.find("options:") == std::string::npos);
  }

  // Snake declares two, and both must be named.
  app.dispatch_event(key(termforge::Key::Down));
  app.step(1, 80, 24);
  {
    const std::string all = pane_text();
    INFO(all);
    CHECK(all.find("options:") != std::string::npos);
    CHECK(all.find("Level") != std::string::npos);
    CHECK(all.find("Walls") != std::string::npos);
    CHECK(all.find("Solid") != std::string::npos);
    CHECK(all.find("Wrap") != std::string::npos);
  }
}

TEST_CASE("a long choice list is advertised by COUNT, not by joining",
          "[selector][options]") {
  // ⚠ Sokoban declares twenty level names. Joined, that is five wrapped rows of
  // a pane which is dropped entirely below 48 columns and shares what it has
  // with the description -- so past kInlineChoiceMax the pane states how many
  // there are, which is the part a player is deciding on.
  //
  // ⚠ Both arms need a case. With only Sokoban over the cap, inverting the
  // comparison still passes for the other four games.
  Probe app;
  app.step(1, 80, 24);
  const int index = game_index("sokoban");
  REQUIRE(index >= 0);
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.step(1, 80, 24);

  std::string all;
  for (int y = 0; y < 24; ++y) all += row_text(app, y) + "\n";
  INFO(all);
  CHECK(all.find("options:") != std::string::npos);
  CHECK(all.find("to choose from") != std::string::npos);
}

TEST_CASE("the options lines stay 7-bit at every pane width",
          "[selector][options][render]") {
  // The same sweep the description already gets, extended to the new lines.
  // An em dash in an option label is static_asserted away at compile time
  // (test/33options), but the punctuation the Shell itself adds -- the "/"
  // separators and the "options:" header -- is written here, not there.
  for (int rows : {8, 9, 10, 12, 20}) {
    for (int cols : {48, 60, 80, 120}) {
      INFO("size: " << cols << "x" << rows);
      Probe app;
      app.step(2, cols, rows);
      for (int i = 0; i < 5; ++i) {
        REQUIRE(all_seven_bit(app));
        app.dispatch_event(key(termforge::Key::Down));
        app.step(1, cols, rows);
      }
    }
  }
}

// ── The geometry block: a floor with a kind, and a ceiling (gitea #15 + #42) ──
//
// ⚠ WHAT IS TESTED HERE AND NOT IN test/34geometry. That suite owns the
// NUMBERS — that every declared floor is the size its own compute_layout
// actually flips at. This one owns what the SELECTOR does with them, which
// needs a Shell and a Screen: the two words, the footer, the precedence
// against a real degradation notice, and the ceiling.

TEST_CASE("the detail pane names the size a game wants", "[selector][geometry]") {
  // ⚠ TWO GAMES, ONE PER SizeFloor VALUE, and neither is optional. The kind
  // exists because Minesweeper's 21x13 is arithmetic and Sokoban's 34x12 is a
  // judgement (see GameGeometry in arcade/game_meta.hpp), and the only place a
  // player ever sees that difference is this word. With one case, collapsing
  // the two arms into whichever string that case names stays green — and the
  // enum quietly stops meaning anything.
  const auto pane_text = [](Probe& app, int rows) {
    std::string all;
    for (int y = 0; y < rows; ++y) all += row_text(app, y) + "\n";
    return all;
  };

  const auto select = [](Probe& app, std::string_view slug) {
    const int index = game_index(slug);
    REQUIRE(index >= 0);
    while (app.selector_index() < index) {
      app.dispatch_event(key(termforge::Key::Down));
    }
    app.step(1, 80, 24);
    REQUIRE(app.selector_index() == index);
  };

  SECTION("a Drawable floor is stated and left there") {
    Probe app;
    app.step(1, 80, 24);
    select(app, "snake");
    const std::string all = pane_text(app, 24);
    INFO(all);
    CHECK(all.find("58x20 needed") != std::string::npos);
    // ⚠ The negative matters more than the positive here. Snake has no camera
    // and no judgement to explain, so it must not acquire a reason it does not
    // have — and an implementation that appended the suffix unconditionally
    // would pass the positive check above on its own.
    CHECK(all.find("to play well") == std::string::npos);
  }

  SECTION("a Playable floor says why its number is where it is") {
    Probe app;
    app.step(1, 80, 24);
    select(app, "sokoban");
    const std::string all = pane_text(app, 24);
    INFO(all);
    CHECK(all.find("34x12 needed to play well") != std::string::npos);
  }

  SECTION("neither kind promises the game will run below it") {
    // ⚠ THE CASE THAT EXISTS BECAUSE THE FIRST DRAFT WAS WRONG. It printed
    // "recommended" for Sokoban against "minimum" for the rest, which reads as
    // "you may go below this one" — and Sokoban then refuses just as hard as
    // every other game (its compute_layout returns !fits below 34x12 and its
    // draw() falls through to draw_too_small). The menu was making a promise
    // the game does not keep, one keystroke apart, which is the exact defect
    // gitea #42 was filed about.
    //
    // No word in this pane may soften into advice. "recommended" and "prefers"
    // are the two that were actually written down at some point.
    for (const std::string_view slug : {"minesweeper", "2048", "snake",
                                        "tetris", "sokoban"}) {
      Probe app;
      app.step(1, 80, 24);
      select(app, slug);
      const std::string all = pane_text(app, 24);
      INFO(slug << ":\n" << all);
      CHECK(all.find("needed") != std::string::npos);
      CHECK(all.find("recommended") == std::string::npos);
      CHECK(all.find("prefers") == std::string::npos);
    }
  }
}

TEST_CASE("the footer warns when the selected game will not fit",
          "[selector][geometry]") {
  // ⚠ THE FOOTER OWNS ONE BAND AND THE DETAIL PANE OWNS THE OTHER. The pane is
  // dropped below kDetailPaneMinCols (48), which is very nearly the band in
  // which games stop fitting — so on exactly the terminals where the answer
  // matters, the pane is not there to give it. Below 48 the warning takes this
  // row; at or above it the pane is already saying so and the row stays the
  // degradation notice's. Neither message ever displaces the other.
  const auto footer_of = [](Probe& app, int rows) {
    return row_text(app, rows - 2);
  };

  const auto select = [](Probe& app, std::string_view slug, int cols, int rows) {
    const int index = game_index(slug);
    REQUIRE(index >= 0);
    while (app.selector_index() < index) {
      app.dispatch_event(key(termforge::Key::Down));
    }
    app.step(1, cols, rows);
    REQUIRE(app.selector_index() == index);
  };

  SECTION("below the detail pane's width, the footer says what is needed") {
    // 40 columns: below kDetailPaneMinCols, so there is no pane, and below
    // Tetris's 35x24 in rows.
    Probe app;
    app.step(1, 40, 20);
    select(app, "tetris", 40, 20);
    const std::string foot = footer_of(app, 20);
    INFO(foot);
    CHECK(foot.find("Tetris needs 35x24") != std::string::npos);
    // It names what the player HAS as well: "needs 35x24" alone leaves them
    // counting rows.
    CHECK(foot.find("40x20") != std::string::npos);
  }

  SECTION("and says nothing at a size that fits") {
    // ⚠ THE CONTROL, and without it "the warning is absent" is
    // indistinguishable from "nothing drew" — the same rule AGENTS.md writes
    // down for pty greps. Same width, same probe, four more rows.
    Probe app;
    app.step(1, 40, 24);
    select(app, "tetris", 40, 24);
    const std::string foot = footer_of(app, 24);
    INFO(foot);
    CHECK(foot.find("needs") == std::string::npos);
    CHECK(foot.find("Tetris") == std::string::npos);
  }

  SECTION("a Playable floor is stated with the same force as any other") {
    // ⚠ SOKOBAN REFUSES BELOW 34x12 EXACTLY AS HARD AS THE OTHER FOUR, so this
    // row must not soften for it. The first draft printed "Sokoban plays better
    // at 34x12 or larger" here — advice, deliberately omitting the terminal's
    // own size — and a player who took the advice and pressed Enter got a hard
    // refusal screen with no board on it.
    Probe app;
    app.step(1, 30, 10);
    select(app, "sokoban", 30, 10);
    const std::string foot = footer_of(app, 10);
    INFO(foot);
    CHECK(foot.find("needs 34x12") != std::string::npos);
    CHECK(foot.find("plays better") == std::string::npos);
  }

  SECTION("the message never gets cut mid-number") {
    // ⚠ THE CASE THAT CAUGHT A REAL DEFECT, and 30 columns could not see it.
    // The footer was one long string on the argument that only its tail would
    // be clipped; measured on a real 22-column pty it read
    //
    //     Minesweeper needs 21x1
    //
    // a truncated number that reads as a complete and wrong one. The Shell
    // draws the selector from kMinCols, so every width from there up is a
    // width a player can have.
    //
    // The assertion is deliberately not "the full sentence is present" — the
    // point of a cascade is that it is not. It is that whatever DOES appear
    // contains the whole size, never a prefix of it.
    // ⚠ EVERY GAME, NOT ONE, AND MINESWEEPER IS THE ONE THAT MATTERS. A first
    // draft swept Sokoban alone and mutation testing walked straight through
    // it: "Sokoban" is short enough that even the widest form clips after the
    // size ("Sokoban needs 34x12; you have " still contains 34x12), so
    // collapsing the cascade to its widest entry survived. "Minesweeper" is
    // eleven characters and its widest form clips at "Minesweeper needs 21",
    // mid-number. The title length is what makes this reachable, so the sweep
    // has to cover the longest title on the roster rather than a convenient
    // one.
    //
    // The assertion is deliberately NOT "the full sentence is present" — the
    // point of a cascade is that it is not. It is that whatever appears
    // contains the whole size, never a prefix of it.
    for (const auto& entry : termgame::all_games()) {
      const std::string wh =
          std::to_string(entry.meta.geometry.cols) + "x" +
          std::to_string(entry.meta.geometry.rows);
      for (int cols = Shell::kMinCols; cols < Shell::kDetailPaneMinCols;
           ++cols) {
        Probe app;
        app.step(1, cols, 8);
        select(app, entry.meta.slug, cols, 8);
        const std::string foot = footer_of(app, 8);
        INFO(entry.meta.slug << " at " << cols << " cols, footer: " << foot);
        REQUIRE(foot.find(wh) != std::string::npos);
      }
    }
  }
}

TEST_CASE("the size warning and a degradation notice never displace each other",
          "[selector][geometry]") {
  // ⚠ THIS CASE CHOSE THE DESIGN, after both orderings of a shared row turned
  // out to be wrong.
  //
  // Notice-wins first: m_notice is STICKY — it holds the most recent ErrorEvent
  // until the next game entry clears it — and FallbackDriver reports no colour
  // during setup, so on a bare terminal the footer is never free. The size
  // warning was unreachable for an entire session at the bottom tier, which is
  // the tier this repo promises always works and the one a too-small terminal
  // is most likely to be. This suite failed on the first run with the footer
  // reading "no colour capability: ASCII bo".
  //
  // Warning-wins next: that swallowed a FRESH degradation. Leaving Tetris on a
  // terminal with no kitty protocol raises the report one frame before the
  // selector redraws, and if Tetris also did not fit, the warning took the row.
  // The notice is one-shot in practice — entering another game clears it — so
  // that is a lost event, and "degradation is an event, never a silent
  // downgrade" is a hard rule.
  //
  // Splitting the row by width dissolves the conflict rather than picking a
  // loser: the warning only speaks below kDetailPaneMinCols, where the pane
  // that would otherwise carry the size does not exist.

  SECTION("above the pane's width, the notice keeps the row") {
    Probe app;
    app.step(1, 60, 20);
    app.dispatch_event(termforge::Event{
        termforge::ErrorEvent{.severity = termforge::Severity::Warning,
                              .source = "detect",
                              .message = "no colour capability"}});
    app.step(1, 60, 20);

    // Tetris does NOT fit at 60x20 — it wants 24 rows — so this is exactly the
    // case that used to lose the notice.
    const int index = game_index("tetris");
    REQUIRE(index >= 0);
    while (app.selector_index() < index) {
      app.dispatch_event(key(termforge::Key::Down));
    }
    app.step(1, 60, 20);

    const std::string foot = row_text(app, 20 - 2);
    INFO(foot);
    CHECK(foot.find("no colour capability") != std::string::npos);
    CHECK(foot.find("Tetris needs") == std::string::npos);

    // ⚠ And the size is still being told to the player, on the other channel.
    // Without this the case would pass against a Shell that had simply dropped
    // the warning, which is the failure it is meant to exclude.
    std::string pane;
    for (int y = 0; y < 20; ++y) pane += row_text(app, y) + "\n";
    INFO(pane);
    CHECK(pane.find("35x24 needed") != std::string::npos);
  }

  SECTION("below it, the warning takes the row because nothing else can") {
    Probe app;
    app.step(1, 40, 20);
    app.dispatch_event(termforge::Event{
        termforge::ErrorEvent{.severity = termforge::Severity::Warning,
                              .source = "detect",
                              .message = "no colour capability"}});
    app.step(1, 40, 20);
    const int index = game_index("tetris");
    REQUIRE(index >= 0);
    while (app.selector_index() < index) {
      app.dispatch_event(key(termforge::Key::Down));
    }
    app.step(1, 40, 20);

    const std::string foot = row_text(app, 20 - 2);
    INFO(foot);
    CHECK(foot.find("Tetris needs 35x24") != std::string::npos);

    // ⚠ AND BACK, which is what makes it borrowing rather than clobbering.
    // Without this a footer that simply dropped the notice on the first
    // too-small game would pass everything above.
    while (app.selector_index() > 0) {
      app.dispatch_event(key(termforge::Key::Up));
    }
    app.step(1, 40, 20);
    const std::string back = row_text(app, 20 - 2);
    INFO(back);
    CHECK(back.find("no colour capability") != std::string::npos);
  }
}

TEST_CASE("the selector body stops widening and centres", "[selector][geometry]") {
  // ⚠ THE DEFECT, in one sentence: every game is a fixed rectangle centred in
  // whatever it is given, and the selector was not — so a 300-column terminal
  // got an edge-to-edge menu and then, one keystroke later, a 58x20 box
  // floating in the middle of it. Past kSelectorMaxCols the body stops growing
  // and is centred, which is what a game does.

  // Row 1 is the frames' top border, which spans the whole body width.
  const auto painted_span = [](Probe& app, int y) {
    int first = -1;
    int last = -1;
    for (int x = 0; x < app.screen().cols(); ++x) {
      if (cell_text(app, x, y).empty()) continue;
      if (first < 0) first = x;
      last = x;
    }
    return std::pair{first, last};
  };

  SECTION("at twice the cap it is centred and capped") {
    const int cols = 2 * Shell::kSelectorMaxCols;
    Probe app;
    app.step(1, cols, 24);
    const auto [first, last] = painted_span(app, 1);
    INFO("span " << first << ".." << last << " of " << cols);
    CHECK(first == (cols - Shell::kSelectorMaxCols) / 2);
    CHECK(last - first + 1 == Shell::kSelectorMaxCols);
    // Centred, not merely capped: an implementation that clamped the width and
    // left the body at x=0 passes the width check and fails this one.
    CHECK(first == cols - 1 - last);
    CHECK(first > 0);
  }

  SECTION("at the cap exactly, nothing has moved") {
    // ⚠ The property that keeps every pre-#42 case honest. At and below
    // kSelectorMaxCols this change is a no-op, which is why the constant is 120
    // — the widest size the suite already drove.
    Probe app;
    app.step(1, Shell::kSelectorMaxCols, 24);
    const auto [first, last] = painted_span(app, 1);
    CHECK(first == 0);
    CHECK(last == Shell::kSelectorMaxCols - 1);
  }

  SECTION("the chrome rows are offset with the panes, not left at x=0") {
    // ⚠ THE SPAN CHECK ABOVE READS ROW 1 — the frames' top border — so it says
    // nothing about the title, the footer or the hint row. An earlier draft
    // left all three pinned to column 0 on the argument that they are chrome
    // rather than a measure, which put them sixty columns from the panes they
    // describe. Mutation testing found the gap: moving the title and the hint
    // row back to x=0 survived the whole suite.
    const int cols = 2 * Shell::kSelectorMaxCols;
    const int expect = (cols - Shell::kSelectorMaxCols) / 2;
    Probe app;
    app.step(1, cols, 24);

    const auto first_painted = [&app](int y) {
      for (int x = 0; x < app.screen().cols(); ++x) {
        if (!cell_text(app, x, y).empty()) return x;
      }
      return -1;
    };

    // The hint row is a fixed string starting at the body's left edge.
    CHECK(first_painted(23) == expect);
    // The title is CENTRED within the body rather than flush with it, so it
    // cannot be compared to `expect` directly — but it must sit inside the
    // body, and a title left at x=0 would centre on the full width and land
    // well left of it.
    const int title_x = first_painted(0);
    INFO("title at " << title_x << ", body " << expect << ".."
                     << expect + Shell::kSelectorMaxCols - 1);
    CHECK(title_x > expect);
    CHECK(title_x < expect + Shell::kSelectorMaxCols);
  }

  SECTION("a click still lands on the row it looks like it lands on") {
    // ⚠ THE FAILURE MODE A COLUMN CHECK CANNOT SEE. Offsetting the panes moves
    // where rows are PAINTED; if the widget's own rect were not moved with
    // them, the screen would look right and every click would land a row's
    // worth of columns away from where the player aimed — pixels and hit-test
    // drifting apart with no compile error, which is exactly the hazard
    // gitea #42 warns about for Sokoban's camera.
    //
    // Nothing here recomputes the offset by hand: it reads the marker's own
    // column off the screen, clicks a row BELOW it in that same column, and
    // asserts the selection followed.
    const int cols = 2 * Shell::kSelectorMaxCols;
    Probe app;
    app.step(1, cols, 24);
    REQUIRE(app.selector_index() == 0);

    const auto marks = find_word(app, ">");
    REQUIRE(marks.size() == 1);
    INFO("marker at " << marks[0].x << "," << marks[0].y);
    CHECK(marks[0].x > 0);  // it really is offset, or this proves nothing

    app.dispatch_event(click(marks[0].x + 6, marks[0].y + 2));
    app.step(1, cols, 24);
    CHECK(app.selector_index() == 2);
  }

  SECTION("rows are capacity and are not capped") {
    // ⚠ Deliberately asymmetric. More rows is more of the roster visible in a
    // scrolling list — real information — where more columns past a measure
    // only stretches a line nobody wanted stretched. There is no
    // kSelectorMaxRows and this case is what says so.
    Probe app;
    app.step(1, 80, 40);
    // The body band is rows 1 .. h-3, so the bottom border sits at h-3.
    const auto [first, last] = painted_span(app, 40 - 3);
    INFO("span " << first << ".." << last);
    CHECK(first == 0);
    CHECK(last == 79);
  }
}

TEST_CASE("the size lines stay 7-bit, including on a terminal that is too small",
          "[selector][geometry][render]") {
  // The existing sweeps run at sizes where every game fits, so none of them can
  // see the footer string at all. These two do — 30x10 is below four of the
  // five floors, and 60x20 is below Tetris's alone.
  for (const auto& [cols, rows] : {std::pair{Shell::kMinCols, 8},
                                   std::pair{30, 10}, std::pair{47, 12},
                                   std::pair{60, 20},
                                   std::pair{2 * Shell::kSelectorMaxCols, 24}}) {
    INFO("size: " << cols << "x" << rows);
    Probe app;
    app.step(2, cols, rows);
    for (int i = 0; i < 5; ++i) {
      REQUIRE(all_seven_bit(app));
      app.dispatch_event(key(termforge::Key::Down));
      app.step(1, cols, rows);
    }
  }
}
