// A binary that throws on purpose, so cmake/pty_restore.sh has something to
// point a pty at.
//
// This exists because the one thing gitea #16 actually turns on — the terminal
// is restored when a frame throws — is the one thing test/21exception cannot
// see. Nothing in that file enters the alternate screen (test_run_guarded
// deliberately does not set m_in_screen, and with neither stream a tty the
// Terminal's out_fd is -1 and its writes are dropped), so leave_screen() is a
// no-op there and there are no escape bytes to assert on. Under script(1) there
// is a real pty, so setup() takes the tty path, run() emits the real sequences,
// and the whole claim becomes checkable without a human.
//
// A bare termforge::App rather than a glyphcade::Shell, on purpose: what is
// under test is the framework's run/teardown lifecycle plus our boundary around
// it, and a Shell would drag the selector, the registry and a game into a test
// about escape bytes.
//
// ⚠ The App is constructed OUTSIDE the boundary here, which is deliberately
// NOT what src/bin/main.cpp does. Mirroring main.cpp was the obvious choice and
// it is wrong: with the App inside the callable, unwinding into the catch
// destroys it and ~App() runs teardown() — the pre-v0.1.10 mechanism — so the
// terminal comes back whether or not upstream's guard exists. Verified, not
// reasoned: deleting the teardown() from termforge's run_loop() catch left this
// probe passing when it was shaped like main.cpp.
//
// Hoisting it out removes that second mechanism. Now the only thing that can
// emit the alt-screen leave before the diagnostic is run_loop() tearing down
// before it rethrows. If upstream regresses, ~App still restores the terminal
// at the end of main() — but AFTER the fatal message, which is exactly what
// pty_restore.sh's ordering assertion is looking for.
//
// This also pins the claim src/bin/main.cpp now makes about itself: that
// keeping the App inside the callable is belt-and-braces rather than a
// requirement. This binary is the shape without the braces, and it must pass.

#include <exception>
#include <stdexcept>

#include <termforge/core/app.hpp>
#include <termforge/core/screen.hpp>
#include <termforge/widgets/theme.hpp>

#include <glyphcade/arcade/exception_boundary.hpp>

namespace {

class ThrowingProbe final : public termforge::App {
 public:
  auto on_render(termforge::Screen& screen) -> void override {
    if (m_frames++ == 0) {
      // Frame 1 paints, so a run that produced no ?1049l can be told apart
      // from a run that never got into the alternate screen to begin with.
      // Without this the two failures look identical in the capture.
      screen.write_text(0, 0, "pty restore probe", termforge::theme::kFg,
                        termforge::theme::kBg);
      return;
    }
    throw std::runtime_error("pty probe");
  }

 private:
  int m_frames{0};
};

}  // namespace

auto main() -> int {
  ThrowingProbe app;  // outside the boundary — see the header comment
  app.set_frame_ms(0);
  return glyphcade::run_or_report([&app] { return app.run(); });
}
