#pragma once

// term-game — run_guard: the process's outermost exception boundary.
//
// This exists because termforge::App::run() does not have one. See the long
// comment in src/lib/arcade/run_guard.cpp for what actually goes wrong without
// it; the short version is that a throw escaping main() with no handler calls
// std::terminate *without unwinding the stack*, so the App is never destroyed,
// so the terminal is never taken out of the alternate screen by the ordinary
// path.
//
// ⚠ Workaround with a deletion date, in the sense DESIGN.md uses the phrase.
// Filed upstream against termforge; delete this file when App::run() restores
// the terminal on the exception path itself.

#include <functional>

namespace termgame {

// Runs `body` and returns its value. If `body` throws, reports the exception on
// stderr and returns 1.
//
// ⚠ Construct the App INSIDE `body`, not outside the call. That placement is
// the entire mechanism — it is unwinding into this function's catch that
// destroys the App, and ~App() is what leaves the alternate screen. An App
// living in the caller's frame is not destroyed by this catch at all, and the
// terminal stays wedged. test/21exception asserts exactly this.
[[nodiscard]] auto guarded_run(const std::function<int()>& body) noexcept -> int;

}  // namespace termgame
