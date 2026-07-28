// term-game — the single binary.
//
// Everything is in the Shell: the terminal, the loop, the selector, the game
// registry. This file should stay roughly this size forever.

#include <termgame/arcade/run_guard.hpp>
#include <termgame/arcade/shell.hpp>

auto main() -> int {
  return termgame::guarded_run([] {
    // ⚠ Constructed INSIDE the guard, and that placement is the whole
    // mechanism: unwinding into guarded_run's catch is what destroys `app`, and
    // it is ~App() — not run() — that calls teardown() and leaves the alternate
    // screen. Hoist this above the guarded_run call and a thrown frame leaves
    // the terminal wedged. run_guard.cpp explains why in full;
    // test/21exception fails if this moves.
    termgame::Shell app;
    return app.run();
  });
}
