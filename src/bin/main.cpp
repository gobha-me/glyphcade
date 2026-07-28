// term-game — the single binary.
//
// Epic 0 opens a placeholder. Epic 1 replaces BootApp with the Shell, which
// owns the game registry and the selector UI; this file should stay roughly
// this size when it does.

#include <termgame/arcade/boot_app.hpp>
#include <termgame/arcade/run_guard.hpp>

auto main() -> int {
  return termgame::guarded_run([] {
    // ⚠ Constructed INSIDE the guard, and that placement is the whole
    // mechanism: unwinding into guarded_run's catch is what destroys `app`, and
    // it is ~App() — not run() — that calls teardown() and leaves the alternate
    // screen. Hoist this above the guarded_run call and a thrown frame leaves
    // the terminal wedged. run_guard.cpp explains why in full;
    // test/21exception fails if this moves.
    termgame::BootApp app;
    return app.run();
  });
}
