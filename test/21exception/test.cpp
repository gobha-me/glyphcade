// The exception path, in two halves that belong to two different projects.
//
// termforge's half: App::run() restores the terminal before an exception
// leaves it (termforge#71, v0.1.10). Asserted below via test_run_guarded.
//
// Ours: whatever it rethrows becomes a diagnostic and exit 1 rather than
// std::terminate. Asserted below via guarded_run. See src/lib/arcade/
// run_guard.cpp for why that is all our half is now.
//
// ⚠ Known limit, stated rather than papered over: nothing here enters the
// alternate screen, so no test in this file can see the escape bytes. What
// pins those is the pty-restore test — cmake/pty_restore.sh drives a
// deliberately-throwing probe under script(1) and asserts the alt-screen
// leave appears *before* the fatal message in the byte stream. That is the
// half of this contract a headless test cannot reach; run both.

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include <termforge/core/app.hpp>
#include <termgame/arcade/run_guard.hpp>

namespace {

// Reads termforge's teardown state from *inside* the frame that throws, so the
// post-condition has a before to be measured against.
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

TEST_CASE("an escaping exception becomes exit 1", "[exception]") {
  // The shape of src/bin/main.cpp, over the same loop main() actually runs.
  // test_run_guarded, not test_run_frames: the latter drives one frame body and
  // never enters run_loop(), so it would exercise a path main() never takes.
  //
  // What used to be here as well was a sentinel member on the App, asserting it
  // had been destroyed by unwinding. That assertion pinned a *mechanism* — and
  // the mechanism was retired with termforge#71, since teardown no longer
  // depends on the App being destroyed at all. The guarantee it was standing in
  // for is asserted directly by the case above, at the level it actually lives.
  const int rc = termgame::guarded_run([] {
    TeardownWitness app;
    app.set_frame_ms(0);
    return app.test_run_guarded(20, 3, nullptr);  // rethrows
  });

  REQUIRE(rc == 1);
}

TEST_CASE("a clean return passes through untouched", "[exception]") {
  REQUIRE(termgame::guarded_run([] { return 7; }) == 7);
}

TEST_CASE("a non-std exception is still caught", "[exception]") {
  // The catch(...) arm. Without it this would escape guarded_run and terminate
  // the test binary rather than failing an assertion.
  REQUIRE(termgame::guarded_run([]() -> int { throw 42; }) == 1);
}
