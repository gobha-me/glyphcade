#pragma once

// term-game — the process's outermost exception boundary.
//
// This is where an exception nothing else caught becomes a diagnostic and an
// exit code, rather than std::terminate and a core dump. It wraps the whole of
// main(), and it is the only catch (...) in the tree.
//
// ⚠ What this is NOT — since it used to be exactly that: it does not restore
// the terminal. termforge::App::run() does that itself as of v0.1.10
// (termforge#71); it tears down and *then* rethrows, so by the time an
// exception arrives here the terminal is already out of raw mode and off the
// alternate screen. Until v0.1.10 restoring it was this file's whole job, and
// the mechanism was unwinding: main() constructed the App inside `body` so that
// entering the catch would destroy it, because ~App() was the only thing that
// ran teardown(). That is history. Do not reintroduce a rule about where the
// App is constructed — there is no longer one.
//
// What survives is the half upstream deliberately declines to do. From
// App::run()'s own docstring, on the exception it rethrows: it "is deliberately
// not converted to a return code: an int has no room for it, and the library
// will not decide that your exception was meaningless. Catch it around run() if
// you want a diagnostic of your own — the terminal is already sane by then."
// This is that catch. Without it a thrown frame ends the process at 134 via
// SIGABRT with nothing printed; with it the user gets a line they can read.

#include <functional>

namespace termgame {

// Runs `body` and returns its value. If `body` throws, reports the exception on
// stderr as "term-game: fatal: <what>" and returns 1.
[[nodiscard]] auto run_or_report(const std::function<int()>& body) noexcept
    -> int;

}  // namespace termgame
