// Tick ROUTING: the Shell hands the framework's fixed timestep to the game,
// only in the right state, and unmodified.
//
// ⚠ Read this before adding a case. The accumulator arithmetic itself belongs
// to termforge and is already tested there (its test/24tick): N ticks for an
// N-period frame, the carried remainder, dt exactness, the first frame's dt==0,
// the clamp's numeric value, quit() from inside a tick. Do not duplicate it.
//
// What glyphcade owns, and what this file is for, is the routing: that the
// Shell actually configures the accumulator, that ticks reach Game::tick only
// while a game is running and not paused, that dt is passed through untouched,
// and that the clamp's glyphcade-visible consequence — a bounded tick count
// after a stall — holds. AGENTS.md makes both the fixed timestep and the clamp
// hard project rules, so a regression has to be red HERE and not only upstream.
//
// Driven over a fake clock, the same technique termforge's own tick and pacing
// tests use: time never really passes, so every assertion is an exact equality
// and the suite never sleeps.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include <termforge/core/types.hpp>

#include <glyphcade/arcade/registry.hpp>
#include <glyphcade/arcade/shell.hpp>
#include <glyphcade/games/minesweeper/minesweeper.hpp>

namespace {

using glyphcade::Shell;
using Seconds = std::chrono::duration<double>;
using namespace std::chrono_literals;

class TickProbe : public Shell {
 public:
  std::vector<std::string> pending;  // bytes the fake fd hands out
  int renders{0};

  auto on_render(termforge::Screen& screen) -> void override {
    ++renders;
    Shell::on_render(screen);
  }

  auto step(int frames = 1) -> void {
    for (int i = 0; i < frames; ++i) test_run_frames(1, 60, 20, &m_sink);
  }

  // One uninterrupted run — needed when the point is that the loop STOPS
  // partway, which a per-frame step() would paper over by re-arming m_running.
  auto run(int frames) -> void { test_run_frames(frames, 60, 20, &m_sink); }

  // The process was frozen BETWEEN frames: SIGSTOP, a debugger breakpoint, a
  // suspended laptop. Nothing in the loop ran; the clock simply moved.
  auto stall(std::chrono::milliseconds d) -> void { m_now += d; }

 protected:
  auto now_steady() const -> std::chrono::steady_clock::time_point override {
    return m_now;
  }
  auto wait_readable(int timeout_ms) -> bool override {
    if (!pending.empty()) return true;
    m_now += std::chrono::milliseconds(timeout_ms);  // budget spent, no input
    return false;
  }
  auto read_available(char* out, int max) -> int override {
    if (pending.empty()) return 0;
    const std::string chunk = pending.front();
    pending.erase(pending.begin());
    const auto n = static_cast<int>(
        std::min(chunk.size(), static_cast<std::size_t>(max)));
    for (int i = 0; i < n; ++i) out[i] = chunk[static_cast<std::size_t>(i)];
    return n;
  }

 private:
  std::chrono::steady_clock::time_point m_now{};
  std::string m_sink;
};

[[nodiscard]] auto key(termforge::Key k) -> termforge::Event {
  return termforge::Event{termforge::KeyEvent{.key = k}};
}
[[nodiscard]] auto ch(char32_t c) -> termforge::Event {
  return termforge::Event{
      termforge::KeyEvent{.key = termforge::Key::Char, .ch = c}};
}

[[nodiscard]] auto game_of(const Shell& shell) -> const glyphcade::Minesweeper* {
  return dynamic_cast<const glyphcade::Minesweeper*>(shell.current_game());
}

auto enter_game(TickProbe& app) -> void {
  app.step();
  int index = -1;
  const auto games = glyphcade::all_games();
  for (std::size_t i = 0; i < games.size(); ++i) {
    if (games[i].meta.slug == "minesweeper") index = static_cast<int>(i);
  }
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

// One frame of 100ms at 60Hz is exactly six tick periods.
constexpr int kFrameMs = 100;
constexpr int kTicksPerFrame = kFrameMs * Shell::kTickHz / 1000;

}  // namespace

TEST_CASE("the Shell configures a fixed timestep", "[tick]") {
  // Trivial, and it is precisely the assertion that goes red when someone
  // deletes set_tick_hz while "simplifying the constructor" — at which point
  // every game silently switches to a variable dt and nothing else complains.
  TickProbe app;
  REQUIRE(app.tick_hz() == Shell::kTickHz);
  REQUIRE(Shell::kTickHz > 0);
}

TEST_CASE("the delta clamp is in force", "[tick]") {
  // AGENTS.md: "a breakpoint or a laptop suspend cannot deliver one enormous
  // dt". A non-positive max_tick_dt would disable both the clamp and the
  // per-frame tick bound.
  TickProbe app;
  REQUIRE(app.max_tick_dt() > Seconds::zero());
  REQUIRE(app.max_tick_dt() <= Seconds{0.25});
}

TEST_CASE("an N-period frame delivers N game ticks", "[tick]") {
  TickProbe app;
  app.set_frame_ms(kFrameMs);
  enter_game(app);

  const auto* game = game_of(app);
  REQUIRE(game != nullptr);
  REQUIRE(game->ticks() == 0);

  app.step(3);
  // Asserted against the STUB's counter, i.e. through the routing, rather than
  // against App::on_tick — which would pass even if the Shell forwarded nothing.
  REQUIRE(game->ticks() == 3 * kTicksPerFrame);
}

TEST_CASE("no game ever sees a zero delta", "[tick]") {
  // A consequence of the fixed timestep, not of anything the Shell does: the
  // first frame's dt == 0 is banked by the accumulator and delivers no ticks at
  // all. It evaporates the moment anyone calls set_tick_hz(0), which is why it
  // is pinned here rather than assumed.
  TickProbe app;
  app.set_frame_ms(kFrameMs);
  enter_game(app);
  app.step(5);

  const auto* game = game_of(app);
  REQUIRE(game != nullptr);
  REQUIRE(game->ticks() > 0);
  REQUIRE(game->min_dt() > Seconds::zero());
  // And it is the constant, not a measured frame delta.
  REQUIRE(game->min_dt().count() ==
          Seconds{1.0 / Shell::kTickHz}.count());
}

TEST_CASE("a pathological delta cannot deliver an unbounded burst", "[tick]") {
  TickProbe app;
  app.set_frame_ms(kFrameMs);
  enter_game(app);

  const auto* game = game_of(app);
  REQUIRE(game != nullptr);

  app.stall(30s);  // frozen between frames
  app.step();

  // Without the clamp this frame would deliver 30 * 60 = 1800 ticks, and every
  // moving object in the game would teleport through whatever it was meant to
  // collide with. With it, the bound is ceil(max_tick_dt * tick_hz).
  const int bound = static_cast<int>(
      std::ceil(app.max_tick_dt().count() * Shell::kTickHz));
  INFO("delivered " << game->ticks() << ", bound " << bound);
  REQUIRE(game->ticks() <= bound);
  REQUIRE(game->ticks() > 0);  // the frame is throttled, not dropped
}

TEST_CASE("the selector does not tick a game", "[tick]") {
  TickProbe app;
  app.set_frame_ms(kFrameMs);
  app.step(10);
  REQUIRE(app.state() == Shell::State::Selector);
  REQUIRE(app.current_game() == nullptr);
  REQUIRE(app.renders == 10);
}

TEST_CASE("pause stops the simulation and resume restarts it", "[tick]") {
  TickProbe app;
  app.set_frame_ms(kFrameMs);
  enter_game(app);

  const auto* game = game_of(app);
  REQUIRE(game != nullptr);

  app.step(2);
  const int before = game->ticks();
  REQUIRE(before == 2 * kTicksPerFrame);

  app.dispatch_event(ch(U'p'));
  REQUIRE(app.modal());
  app.step(10);
  // The framework keeps calling App::on_tick while an overlay is up, by design.
  // Not forwarding it is the Shell's job, and this is the only thing that
  // checks the Shell did it.
  REQUIRE(game->ticks() == before);

  app.dispatch_event(key(termforge::Key::Escape));  // resume
  REQUIRE(app.state() == Shell::State::InGame);
  app.step(2);
  REQUIRE(game->ticks() > before);
}

TEST_CASE("Escape in the selector stops the loop", "[tick][escape]") {
  // End-to-end corroboration of App::running() through the real loop, with real
  // bytes rather than a synthesised Event.
  //
  // Safe to assert after the run here, unlike the two cases in test/11selector
  // that have to assert before their step(): run() is ONE uninterrupted
  // test_run_frames, so nothing re-arms m_running between quit() and the check.
  // That is the reason run() exists separately from step() — see the note above.
  TickProbe app;
  app.set_frame_ms(kFrameMs);
  app.pending.push_back("\x1b");

  app.run(5);

  // Two frames, not one, and the arithmetic is worth stating: frame 1 reads the
  // lone ESC and HOLDS it — pump_input refuses to flush while an escape
  // sequence may still be in flight, or every arrow key would arrive as a quit.
  // wait_frame grants the grace window, frame 2 flushes and dispatches, and
  // quit() ends the loop before frame 3.
  REQUIRE(app.renders == 2);
  REQUIRE_FALSE(app.running());
}
