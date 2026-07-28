#include <termgame/arcade/exception_boundary.hpp>

#include <cstdio>
#include <exception>

namespace termgame {

// What this function is for, and — because it used to be for something else
// entirely — what it is no longer for.
//
// Since termforge v0.1.10, App::run() guards its own exception path:
//
//     try { if (auto r = setup(); !r) { teardown(); ...; return 1; } }
//     catch (...) { teardown(); throw; }
//     ...
//     return run_loop();   // try { loop } catch (...) { teardown(); throw; }
//
// So the terminal is out of raw mode and off the alternate screen before an
// exception ever reaches this file. That was this function's original and only
// job — v0.1.9's run() had no try/catch at all, and what rescued the terminal
// was ~App(), reached only by unwinding, which a throw escaping main() with no
// handler does not do (std::terminate is called WITHOUT unwinding). Providing a
// handler at the bottom of the stack was the entire mechanism. It is not any
// more, and the "construct the App inside body" rule that went with it is gone.
//
// What is left is the piece upstream will not do for us. run() restores the
// terminal and then rethrows, on the stated grounds that an int has no room for
// an exception and the library will not decide yours was meaningless — and it
// points the consumer here: "Catch it around run() if you want a diagnostic of
// your own — the terminal is already sane by then." This is that catch, and
// that is now the whole of it: a readable line and exit 1, instead of exit 134
// via SIGABRT with nothing printed.
//
// The ordering claim in the old version of this comment survives, on a new
// footing. The diagnostic below still lands on a restored terminal rather than
// being painted into an alternate screen that is about to vanish — but because
// run_loop() calls teardown() *before* it rethrows, not because unwinding
// destroys the App on the way out.
//
// ⚠ This file no longer has a deletion condition; it has an upstream
// dependency. If a future termforge stops tearing down before it rethrows, or
// starts swallowing the exception into a return code, this diagnostic goes back
// to being written into a dying alt screen and nothing here would say so. The
// pty-restore test (cmake/pty_restore.sh) is what catches that, by asserting
// the alt-screen leave appears in the byte stream *before* this message does.
auto run_or_report(const std::function<int()>& body) noexcept -> int {
  try {
    return body();
  } catch (const std::exception& e) {
    // fprintf, not std::cerr: matches termforge, and keeps this path free of
    // anything that could itself throw or allocate.
    std::fprintf(stderr, "term-game: fatal: %s\n", e.what());
    return 1;
  } catch (...) {
    std::fprintf(stderr, "term-game: fatal: unknown exception\n");
    return 1;
  }
}

}  // namespace termgame
