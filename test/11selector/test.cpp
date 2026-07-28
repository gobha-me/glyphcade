// The Shell's state machine: Selector <-> InGame <-> Paused.
//
// Everything here is driven through App::dispatch_event, which is public and is
// the real routing path — including the overlay-capture policy, so the pause
// cases genuinely prove that the dialog swallows the game's input rather than
// asserting that we remembered to ignore it. One case goes a layer lower and
// feeds raw bytes through test_pump, so the escape-sequence decoder is covered
// at least once.
//
// ⚠ Two traps for whoever extends this file:
//
//   1. Dialog::begin_result()'s latch clears only on the next draw(). A test
//      that pauses, answers, and pauses again without running a frame in
//      between will find the second answer silently ignored. Run one frame.
//   2. ListWidget::rect() is only set inside Shell::on_render, so anything
//      geometry-dependent needs a frame first.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include <termforge/core/types.hpp>

#include <termgame/arcade/registry.hpp>
#include <termgame/arcade/shell.hpp>
#include <termgame/games/stub/stub_game.hpp>

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

// The stub's index in the menu, looked up rather than hardcoded — the stub is
// scheduled for deletion and the roster will grow before then.
[[nodiscard]] auto stub_index() -> int {
  const auto games = termgame::all_games();
  for (std::size_t i = 0; i < games.size(); ++i) {
    if (games[i].meta.slug == "stub") return static_cast<int>(i);
  }
  return -1;
}

[[nodiscard]] auto stub_of(const Shell& shell) -> const termgame::StubGame* {
  return dynamic_cast<const termgame::StubGame*>(shell.current_game());
}

// Enter the stub game from a fresh selector.
auto enter_stub(Probe& app) -> void {
  app.step();  // give the list its geometry
  const int index = stub_index();
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
  enter_stub(app);
  REQUIRE(app.current_game() != nullptr);
  REQUIRE(app.current_game()->meta().slug == "stub");
}

TEST_CASE("every entry gets a fresh game", "[selector][lifetime]") {
  Probe app;
  enter_stub(app);

  // Advance the simulation so the first instance has visibly accumulated state.
  const auto* first = stub_of(app);
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
  const auto* second = stub_of(app);
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
    REQUIRE_FALSE(app.quit_requested());
    app.dispatch_event(key(termforge::Key::Escape));
    REQUIRE(app.quit_requested());
  }
  {
    Probe app;
    enter_stub(app);
    app.dispatch_event(key(termforge::Key::Escape));
    REQUIRE(app.state() == Shell::State::Selector);
    REQUIRE_FALSE(app.quit_requested());
    app.step();
    REQUIRE(app.current_game() == nullptr);
  }
}

TEST_CASE("Ctrl+C quits from every state", "[selector][escape]") {
  {
    Probe app;
    app.step();
    app.dispatch_event(ctrl_c());
    REQUIRE(app.quit_requested());
  }
  {
    Probe app;
    enter_stub(app);
    app.dispatch_event(ctrl_c());
    REQUIRE(app.quit_requested());
  }
  {
    // The interesting one: App::dispatch_event routes Ctrl+C past the overlay
    // on purpose, so a paused game must still be killable. Ctrl+C is no longer
    // inherited from App::on_event — the Shell handles it itself — so this is
    // the case that catches someone dropping that branch.
    Probe app;
    enter_stub(app);
    app.dispatch_event(ch(U'p'));
    REQUIRE(app.state() == Shell::State::Paused);
    app.dispatch_event(ctrl_c());
    REQUIRE(app.quit_requested());
  }
}

TEST_CASE("P pauses, and the overlay swallows the game's input",
          "[selector][pause]") {
  Probe app;
  enter_stub(app);

  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);
  REQUIRE(app.overlay_count() == 1);
  REQUIRE(app.modal());

  // 'd' is the stub's finish key. While paused it must never reach the game —
  // proof that the pause suspends input rather than merely suspending ticks.
  app.dispatch_event(ch(U'd'));
  app.step();
  REQUIRE(app.state() == Shell::State::Paused);
  REQUIRE(app.current_game() != nullptr);
}

TEST_CASE("Escape resumes from pause", "[selector][pause]") {
  Probe app;
  enter_stub(app);
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
  enter_stub(app);
  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);

  // ConfirmDialog's unconditional Y hotkey = the "yes" button, labelled "Menu".
  app.dispatch_event(ch(U'y'));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
  REQUIRE(app.current_game() == nullptr);
  REQUIRE(app.overlay_count() == 0);
  REQUIRE_FALSE(app.quit_requested());
}

TEST_CASE("a game can end itself", "[selector][lifetime]") {
  Probe app;
  enter_stub(app);
  app.dispatch_event(ch(U'd'));  // stub sets done()
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
  REQUIRE(app.current_game() == nullptr);
}

TEST_CASE("a game can request the menu from inside its own event handler",
          "[selector][lifetime]") {
  // ⚠ This is a use-after-free probe, and it is only worth anything under the
  // sanitizer toolchains — run it there. The stub calls
  // GameContext::quit_to_menu() from inside on_event; a Shell that honoured
  // that synchronously would destroy the game while its handler's frame was
  // still live. The deferred flag in GameContext exists for exactly this.
  Probe app;
  enter_stub(app);
  app.dispatch_event(ch(U'm'));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
  REQUIRE(app.current_game() == nullptr);
}

TEST_CASE("the selector survives entering and leaving repeatedly",
          "[selector][lifetime]") {
  Probe app;
  for (int i = 0; i < 5; ++i) {
    enter_stub(app);
    app.dispatch_event(key(termforge::Key::Escape));
    app.step();
    REQUIRE(app.state() == Shell::State::Selector);
    REQUIRE(app.current_game() == nullptr);
    REQUIRE_FALSE(app.quit_requested());
  }
}
