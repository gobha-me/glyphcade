// The selector renders, with no terminal involved.
//
// This is the pattern every future game's rendering test should copy: drive the
// real frame loop through termforge's headless harness, then assert against the
// offscreen Screen. No tty, no pty, no sleeping.
//
// Note which tier this exercises. test_run_frames installs a FallbackDriver,
// whose capabilities() reports all-false — so the Shell syncs to the ASCII
// border tier and every assertion below is about the BOTTOM tier, which is the
// one AGENTS.md says must always work.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include <termgame/arcade/registry.hpp>
#include <termgame/arcade/shell.hpp>

namespace {

class Probe final : public termgame::Shell {
 public:
  // App::screen() is protected. Re-exposing it is the whole reason Shell is not
  // `final`.
  using Shell::screen;
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

// Reconstructs a row as text. The renderer positions every cell individually,
// so this is the only way to look for a string on screen.
[[nodiscard]] auto row_text(termforge::Screen& s, int y) -> std::string {
  std::string out;
  for (int x = 0; x < s.cols(); ++x) out += s.at(x, y).text;
  return out;
}

[[nodiscard]] auto screen_contains(termforge::Screen& s, std::string_view needle)
    -> bool {
  for (int y = 0; y < s.rows(); ++y) {
    if (row_text(s, y).find(needle) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("the Shell paints the selector with no tty", "[render]") {
  Probe app;

  // ⚠ Load-bearing, not tidiness. test_run_frames runs the REAL frame_step(),
  // which ends in wait_frame(). At the default 33ms this test would either
  // sleep a frame or block reading whatever stdin happens to be under ctest.
  // At 0 the frame deadline equals frame_start, so the wait loop exits before
  // it ever touches the fd — deterministic and instant.
  //
  // Overriding the now_steady()/wait_readable()/read_available() seams instead
  // (as test/13tick does) is only necessary when the test is about cadence.
  // This one is about pixels.
  app.set_frame_ms(0);

  std::string sink;
  app.test_run_frames(1, 60, 20, &sink);

  // The renderer emitted bytes at all — i.e. the frame reached the driver
  // rather than being skipped.
  REQUIRE_FALSE(sink.empty());

  auto& screen = app.screen();
  REQUIRE(screen.cols() == 60);
  REQUIRE(screen.rows() == 20);

  // The title, drawn through termforge::Label at row 0. If the Label failed to
  // link or draw, this row is blank while the write_text rows still pass —
  // exactly the split worth being able to see.
  REQUIRE(row_has_content(screen, 0));

  // The list frame's top border, and the footer hint line.
  REQUIRE(row_has_content(screen, 1));
  REQUIRE(row_has_content(screen, 20 - 1));

  // Every registered game's title is on screen. This is the selector's whole
  // job, and it fails loudly if the registry came back empty — the exact
  // symptom self-registering statics produce.
  REQUIRE_FALSE(termgame::all_games().empty());
  for (const auto& entry : termgame::all_games()) {
    INFO("missing game title: " << entry.meta.title);
    REQUIRE(screen_contains(screen, entry.meta.title));
  }
}

TEST_CASE("the selector re-renders at a different size", "[render]") {
  // Catches geometry hardcoded from the first frame: every row asserted above
  // is positioned off rows()/cols(), so a narrower, shorter screen must still
  // come out populated. 40 columns is also below kDetailPaneMinCols, so this
  // covers the single-pane branch.
  Probe app;
  app.set_frame_ms(0);

  std::string sink;
  app.test_run_frames(1, 40, 12, &sink);

  auto& screen = app.screen();
  REQUIRE(screen.cols() == 40);
  REQUIRE(screen.rows() == 12);
  REQUIRE(row_has_content(screen, 0));
  REQUIRE(row_has_content(screen, 12 - 1));
  REQUIRE(screen_contains(screen, termgame::all_games().front().meta.title));
}

TEST_CASE("a too-small window degrades instead of crashing", "[render]") {
  // Under the sanitizer toolchains this is where a negative-width Rect or an
  // out-of-range write would surface. The contract is only that we survive and
  // say something.
  Probe app;
  app.set_frame_ms(0);

  std::string sink;
  app.test_run_frames(1, 10, 4, &sink);

  auto& screen = app.screen();
  REQUIRE(screen.cols() == 10);
  REQUIRE(screen.rows() == 4);
  REQUIRE(row_has_content(screen, 0));
}
