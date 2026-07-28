// The exception path: a throwing frame must exit 1 AND unwind the App.
//
// The second half is the real assertion. termforge's App::run() has no
// try/catch, so what restores the terminal after a thrown frame is ~App() —
// which only runs if the App is destroyed by stack unwinding, which only
// happens because guarded_run provides a handler. See src/lib/arcade/
// run_guard.cpp for the full explanation.
//
// ⚠ Known limit, stated rather than papered over: test_run_frames never enters
// the alternate screen (it skips the tty half of setup()), so teardown()'s
// leave_screen() is a no-op here. This proves the *unwinding contract* — App
// destroyed on the way out — not the escape bytes on a real terminal.
//
// The escape bytes ARE checkable without a human, using script(1) to allocate a
// pty (see AGENTS.md); that is how the normal ESC-quit path was verified. It is
// not automated for the exception path only because no shipped binary throws on
// purpose, so there would be nothing to point it at.

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include <termforge/core/app.hpp>
#include <termgame/arcade/run_guard.hpp>

namespace {

bool g_app_destroyed = false;

struct Sentinel {
  ~Sentinel() { g_app_destroyed = true; }
};

class ThrowingApp final : public termforge::App {
 public:
  auto on_render(termforge::Screen&) -> void override {
    throw std::runtime_error("boom");
  }

 private:
  // Fires only if this App is destroyed. A std::terminate without unwinding
  // leaves it untouched.
  Sentinel m_sentinel;
};

// The probe for upstream's own guarantee, as distinct from ours. It reads
// termforge's teardown state from *inside* the frame that throws, so the
// post-condition below has a before to be measured against.
class TeardownWitness final : public termforge::App {
 public:
  auto on_render(termforge::Screen&) -> void override {
    // teardown() has NOT run yet at this point — this is the "before".
    m_hooked_at_throw = test_winch_hooked();

    // A hard frame cap, per test_run_guarded's own docstring: if upstream's
    // guard ever regresses into swallowing the exception, this test must fail
    // the suite rather than spin forever.
    if (++m_frames > kFrameCap) {
      quit();
      return;
    }
    throw std::runtime_error("boom");
  }

  [[nodiscard]] auto hooked_at_throw() const -> bool { return m_hooked_at_throw; }

 private:
  static constexpr int kFrameCap = 8;

  bool m_hooked_at_throw{false};
  int  m_frames{0};
};

}  // namespace

TEST_CASE("upstream tears the terminal down before the throw reaches us",
          "[exception]") {
  // This asserts termforge's guarantee, not ours, which is why it goes through
  // no termgame code at all. It is the reason the pin is at v0.1.10: before
  // that, App::run() had no try/catch and what restored the terminal was
  // ~App() — reached only by unwinding, which a throw escaping main() does not
  // do. Now run_loop() tears down and then rethrows.
  //
  // test_run_guarded is the level the guarantee can be pinned at: run() itself
  // is untestable because setup() needs a tty, so upstream exposes the loop
  // wrapper — byte for byte what run() calls, teardown and all.
  TeardownWitness app;
  app.set_frame_ms(0);

  // REQUIRE_THROWS_AS is half the assertion: upstream deliberately does NOT
  // convert the exception to an exit code. If a future termforge started
  // swallowing it, our boundary would silently stop being the thing that
  // produces exit 1, and nothing else in this suite would notice.
  REQUIRE_THROWS_AS(app.test_run_guarded(20, 3, nullptr), std::runtime_error);

  REQUIRE(app.hooked_at_throw());          // armed when the frame threw...
  REQUIRE_FALSE(app.test_winch_hooked());  // ...and disarmed on the way out
}

TEST_CASE("a throwing frame exits 1 and unwinds the App", "[exception]") {
  g_app_destroyed = false;

  const int rc = termgame::guarded_run([] {
    // Constructed inside the guard, exactly as src/bin/main.cpp does it.
    ThrowingApp app;
    app.set_frame_ms(0);
    std::string sink;
    app.test_run_frames(1, 20, 3, &sink);
    return 0;  // unreachable — on_render throws on the first frame
  });

  REQUIRE(rc == 1);

  // THE regression assertion. Hoist the App above the guarded_run call, delete
  // guarded_run's try/catch, or make it rethrow, and this goes red — which is
  // the class of change someone makes while "simplifying main.cpp".
  REQUIRE(g_app_destroyed);
}

TEST_CASE("a clean return passes through untouched", "[exception]") {
  REQUIRE(termgame::guarded_run([] { return 7; }) == 7);
}

TEST_CASE("a non-std exception is still caught", "[exception]") {
  // The catch(...) arm. Without it this would escape guarded_run and terminate
  // the test binary rather than failing an assertion.
  REQUIRE(termgame::guarded_run([]() -> int { throw 42; }) == 1);
}
