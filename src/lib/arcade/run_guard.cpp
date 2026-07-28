#include <termgame/arcade/run_guard.hpp>

#include <cstdio>
#include <exception>

namespace termgame {

// Why this exists, in full, because the failure is subtle and the upstream
// docstring says the opposite.
//
// termforge's App::run() is:
//
//     if (auto r = setup(); !r) { ...; return 1; }
//     m_running = true;
//     while (m_running) frame_step();
//     teardown();                       // <- no try/catch, no scope guard
//     return 0;
//
// An exception thrown from on_render propagates out of frame_step() and skips
// teardown() entirely. What rescues the terminal is ~App(), which calls
// teardown() itself, followed by ~Terminal() restoring termios — but a
// destructor only runs if the object is destroyed, and an exception that
// escapes main() with no handler anywhere calls std::terminate WITHOUT
// unwinding the stack. GCC and Clang both do this; it is permitted and it is
// what they choose. The App is never destroyed, teardown() never runs, and the
// only thing left standing between the user and a wedged terminal is
// termforge's fatal-signal handler catching the subsequent SIGABRT — which does
// restore the terminal, at the cost of exiting 134.
//
// A handler existing at the bottom of the stack is what makes the
// implementation unwind rather than terminate outright. That is the whole job
// of this function: it does not "handle" much, it just needs to be there.
//
// Order matters and is worth stating: unwinding destroys the App on the way
// out, so leave_screen() and the termios restore both happen BEFORE the catch
// block runs. The diagnostic below therefore lands on a restored terminal
// rather than being painted into an alternate screen that is about to vanish.
//
// ⚠ Deletion date: filed upstream against termforge. When App::run() guards its
// own exception path, delete this file, its header, and test/21exception's
// unwinding assertion.
auto guarded_run(const std::function<int()>& body) noexcept -> int {
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
