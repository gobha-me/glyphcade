// The Shell's state machine: Selector <-> InGame <-> Paused.
//
// Everything here is driven through App::dispatch_event, which is public and is
// the real routing path — including the overlay-capture policy, so the pause
// cases genuinely prove that the dialog swallows the game's input rather than
// asserting that we remembered to ignore it. One case goes a layer lower and
// feeds raw bytes through test_pump, so the escape-sequence decoder is covered
// at least once.
//
// ⚠ Five traps for whoever extends this file:
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

#include <catch2/catch_test_macros.hpp>

#include <string>

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

  auto step(int frames = 1) -> void {
    test_run_frames(frames, 60, 20, &m_sink);
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
[[nodiscard]] auto minesweeper_index() -> int {
  const auto games = termgame::all_games();
  for (std::size_t i = 0; i < games.size(); ++i) {
    if (games[i].meta.slug == "minesweeper") return static_cast<int>(i);
  }
  return -1;
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

TEST_CASE("a repeated arrow still moves the selection",
          "[selector][keyboard]") {
  // ⚠ THE HALF THAT IS EASY TO GET WRONG IN THE OTHER DIRECTION. The gate is
  // "drop Release", not "keep only Press". Under Enhanced the terminal sends
  // Repeat INSTEAD OF a second Press while a key is held, so a gate written as
  // `action == Press` would silently kill hold-to-scroll in the menu the day
  // any game turns the tier on — with no error and nothing in the suite to say
  // so, since a plain press would keep working.
  if (termgame::all_games().size() < 2) return;

  Probe app;
  app.step();
  REQUIRE(app.selector_index() == 0);

  app.dispatch_event(key_repeated(termforge::Key::Down));
  REQUIRE(app.selector_index() == 1);
}

TEST_CASE("the keyboard tier is the game's, and is given back on the way out",
          "[selector][keyboard]") {
  // Every entry in the roster today declares Legacy, so this asserts the
  // identity case: entering and leaving a game must not move the tier. It is
  // the regression guard for the OTHER direction of gitea #32 — a Shell that
  // set Enhanced globally, or that forgot to restore, would fail here the
  // moment the first game asks for a tier, which is exactly when nobody would
  // be looking at this file.
  for (const auto& entry : termgame::all_games()) {
    REQUIRE(entry.meta.keyboard == termforge::KeyboardMode::Legacy);
  }

  Probe app;
  REQUIRE(app.keyboard_mode() == termforge::KeyboardMode::Legacy);

  enter_game(app);
  REQUIRE(app.keyboard_mode() == termforge::KeyboardMode::Legacy);

  app.dispatch_event(key(termforge::Key::Escape));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
  REQUIRE(app.keyboard_mode() == termforge::KeyboardMode::Legacy);
}
