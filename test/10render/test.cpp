// The placeholder renders, with no terminal involved.
//
// This is the pattern every future game's rendering test should copy: drive the
// real frame loop through termforge's headless harness, then assert against the
// offscreen Screen. No tty, no pty, no sleeping.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include <termgame/arcade/boot_app.hpp>

namespace {

class Probe final : public termgame::BootApp {
 public:
  // App::screen() is protected. Re-exposing it is the whole reason BootApp is
  // not `final`.
  using BootApp::screen;
};

// Scans a row for a non-blank cell. Cheaper to read than reconstructing the
// row's text, and it is what we actually care about: something was painted
// there.
[[nodiscard]] auto row_has_content(termforge::Screen& s, int y) -> bool {
  for (int x = 0; x < s.cols(); ++x) {
    if (!s.at(x, y).blank()) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("BootApp paints a placeholder frame with no tty", "[render]") {
  Probe app;

  // ⚠ Load-bearing, not tidiness. test_run_frames runs the REAL frame_step(),
  // which ends in wait_frame(). At the default 33ms this test would either
  // sleep a frame or block reading whatever stdin happens to be under ctest.
  // At 0 the frame deadline equals frame_start, so the wait loop exits before
  // it ever touches the fd — deterministic and instant.
  //
  // Overriding the now_steady()/wait_readable()/read_available() seams instead
  // (as termforge's test/23pacing does) is only necessary when the test is
  // about cadence. This one is about pixels.
  app.set_frame_ms(0);

  std::string sink;
  app.test_run_frames(1, 40, 12, &sink);

  // The renderer emitted bytes at all — i.e. the frame reached the driver
  // rather than being skipped.
  REQUIRE_FALSE(sink.empty());

  auto& screen = app.screen();
  REQUIRE(screen.cols() == 40);
  REQUIRE(screen.rows() == 12);

  // The title, drawn through termforge::Label at h/2. If the Label failed to
  // link or draw, this row is blank while the two write_text rows below still
  // pass — which is exactly the split worth being able to see.
  REQUIRE(row_has_content(screen, 12 / 2));

  // The audio line and the quit hint, both via Screen::write_text.
  REQUIRE(row_has_content(screen, 12 - 2));
  REQUIRE(row_has_content(screen, 12 - 1));
}

TEST_CASE("BootApp re-renders at a different size", "[render]") {
  // Catches a placeholder that hardcodes geometry from the first frame: every
  // row asserted above is positioned off rows()/cols(), so a narrower, shorter
  // screen must still come out fully populated.
  Probe app;
  app.set_frame_ms(0);

  std::string sink;
  app.test_run_frames(1, 20, 6, &sink);

  auto& screen = app.screen();
  REQUIRE(screen.cols() == 20);
  REQUIRE(screen.rows() == 6);
  REQUIRE(row_has_content(screen, 6 / 2));
  REQUIRE(row_has_content(screen, 6 - 1));
}
