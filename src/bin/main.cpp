// term-game — the single binary.
//
// Everything is in the Shell: the terminal, the loop, the selector, the game
// registry. This file should stay roughly this size forever.

#include <termgame/arcade/exception_boundary.hpp>
#include <termgame/arcade/shell.hpp>

auto main() -> int {
  return termgame::run_or_report([] {
    // Constructed inside the callable, which is no longer a *requirement* — it
    // was, until termforge v0.1.10, because unwinding into the catch below
    // was the only thing that ran teardown(). run() tears the terminal down
    // itself now, on every path out. Kept anyway, for a smaller reason that
    // still holds: `app`'s lifetime is then strictly inside the try, so
    // ~Shell runs before the diagnostic prints. teardown() is idempotent, so
    // that is free belt-and-braces rather than a second restore.
    //
    // The point of saying so: do not inherit an expired constraint from this
    // file. Hoisting `app` out would be harmless today.
    termgame::Shell app;
    return app.run();
  });
}
