// Tetris through a real Shell: geometry, rendering, input routing, sound
// intent, and the degraded keyboard arm.
//
// ⚠ Three traps, the same three test/26snake-ui carries, and they still bite:
//
//   1. test_run_frames installs a FallbackDriver, whose capabilities() report
//      all-false — so the Shell syncs to BorderStyle::Ascii and EVERY case in
//      this file exercises the bottom tier. For this game that has a second
//      consequence: kitty_keyboard is false too, so every case here is also on
//      the DEGRADED input arm. The held arm is not reachable from this
//      container at all, and test/27tetris covers it directly on the model.
//   2. NEVER hold a Screen& across a step(). App::test_run_frames reassigns the
//      Screen on every call, so a reference taken before a frame dangles after
//      it — as a segfault mid-suite, not a wrong value.
//   3. NEVER read the Game* after dispatching a key that ends the game.
//      Shell::handle_in_game_key calls apply_transitions() as soon as the game
//      consumes a key, so done() is polled and the game may be destroyed before
//      dispatch_event returns.
//
// ⚠ And one that is new here. Tetris needs 35x24, four rows taller than any
// other game in the suite, and the probe's default is exactly 80x24 — zero rows
// of slack. A case that renders at 80x20 out of habit gets the "terminal too
// small" screen and asserts nothing about the board.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <utility>

#include <termforge/core/types.hpp>

#include <glyphcade/arcade/registry.hpp>
#include <glyphcade/arcade/shell.hpp>
#include <glyphcade/games/tetris/tetris.hpp>

namespace {

using glyphcade::Shell;
using glyphcade::Tetris;
using glyphcade::audio::SfxId;
using namespace glyphcade::tetris;

class Probe final : public Shell {
 public:
  using Shell::screen;

  Probe() { set_frame_ms(0); }

  auto step(int frames = 1, int cols = 80, int rows = 24) -> void {
    test_run_frames(frames, cols, rows, &m_sink);
  }

 private:
  std::string m_sink;
};

[[nodiscard]] auto key(termforge::Key k) -> termforge::Event {
  return termforge::Event{termforge::KeyEvent{.key = k}};
}
[[nodiscard]] auto ch(char32_t c) -> termforge::Event {
  return termforge::Event{
      termforge::KeyEvent{.key = termforge::Key::Char, .ch = c}};
}
[[nodiscard]] auto ch_released(char32_t c) -> termforge::Event {
  return termforge::Event{
      termforge::KeyEvent{.key = termforge::Key::Char,
                          .ch = c,
                          .action = termforge::KeyAction::Release}};
}

[[nodiscard]] auto tetris_index() -> int {
  const auto games = glyphcade::all_games();
  for (std::size_t i = 0; i < games.size(); ++i) {
    if (games[i].meta.slug == "tetris") return static_cast<int>(i);
  }
  return -1;
}

auto enter_tetris(Probe& app, int cols = 80, int rows = 24) -> void {
  app.step(1, cols, rows);
  const int index = tetris_index();
  REQUIRE(index >= 0);
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);
  // ⚠ A SECOND Enter, and it goes AFTER the REQUIRE above, not before. The
  // REQUIRE is what proves the Shell entered on the FIRST Enter; moving it below
  // this line would make the case pass even if entering had come to need two.
  //
  // gitea #38: entering a game now opens its pre-start options screen, so a
  // suite that wants a BOARD has to say so. This is the change telling the truth
  // about itself, not a regression -- and the per-suite cases below assert the
  // screen is there before this dismisses it.
  //
  // ⚠ Leaving this out does not produce a red test, it produces a HANG. Several
  // cases here steer with `while (cursor().row < N) dispatch(Down)`, which is
  // bounded by the code under test: with the options screen up the arrows move a
  // cycler instead of the cursor, the predicate never becomes true, and the
  // suite spins forever.
  app.dispatch_event(key(termforge::Key::Enter));
  app.step(1, cols, rows);
}

[[nodiscard]] auto game_of(Shell& shell) -> Tetris* {
  return dynamic_cast<Tetris*>(const_cast<glyphcade::Game*>(
      static_cast<const glyphcade::Game*>(shell.current_game())));
}

[[nodiscard]] auto row_text(Probe& app, int y) -> std::string {
  std::string out;
  const auto& s = app.screen();
  for (int x = 0; x < s.cols(); ++x) {
    const auto& t = s.at(x, y).text;
    out += t.empty() ? " " : t;
  }
  return out;
}

[[nodiscard]] auto screen_text(Probe& app) -> std::string {
  std::string out;
  for (int y = 0; y < app.screen().rows(); ++y) out += row_text(app, y) + "\n";
  return out;
}

[[nodiscard]] auto all_seven_bit(Probe& app) -> bool {
  auto& s = app.screen();
  for (int y = 0; y < s.rows(); ++y) {
    for (int x = 0; x < s.cols(); ++x) {
      for (const char c : s.at(x, y).text) {
        if (static_cast<unsigned char>(c) >= 0x80) return false;
      }
    }
  }
  return true;
}

// The two columns of one board cell, as the screen holds them.
[[nodiscard]] auto cell_text(Probe& app, const Layout& lay, int col, int row)
    -> std::string {
  std::string out;
  for (int i = 0; i < kCellCols; ++i) {
    const auto& t = app.screen().at(lay.cell_x(col) + i, lay.cell_y(row)).text;
    out += t.empty() ? " " : t;
  }
  return out;
}

}  // namespace

// ── Geometry ────────────────────────────────────────────────────────────────

TEST_CASE("the well needs 35x24 and says so below it", "[tetris][layout]") {
  // ⚠ The first game in the suite that does not fit in twenty rows. Every other
  // one does, so "it fits at 80x24" has never before been a claim worth making.
  REQUIRE(kNeedCols == 35);
  REQUIRE(kNeedRows == 24);

  REQUIRE(compute_layout(kNeedCols, kNeedRows).fits);
  REQUIRE_FALSE(compute_layout(kNeedCols - 1, kNeedRows).fits);
  REQUIRE_FALSE(compute_layout(kNeedCols, kNeedRows - 1).fits);
  // The Shell's own floor, which the selector will happily launch us on.
  REQUIRE_FALSE(compute_layout(20, 8).fits);
}

TEST_CASE("the whole board is inside the frame at every size that fits",
          "[tetris][layout]") {
  for (const int cols : {35, 36, 40, 58, 80, 120}) {
    for (const int rows : {24, 25, 30, 40}) {
      const Layout lay = compute_layout(cols, rows);
      REQUIRE(lay.fits);
      // Left and right edges of the playfield, inside the well's frame.
      REQUIRE(lay.cell_x(0) > lay.well_x);
      REQUIRE(lay.cell_x(kCols - 1) + kCellCols <= lay.well_x + kWellCols - 1);
      // Top and bottom visible rows, inside it too.
      REQUIRE(lay.cell_y(kHiddenRows) > lay.well_y);
      REQUIRE(lay.cell_y(kRows - 1) < lay.well_y + kWellRows - 1);
      // The panel never overlaps the well, and both stay on screen.
      REQUIRE(lay.panel_x >= lay.well_x + kWellCols);
      REQUIRE(lay.panel_x + kPanelCols <= cols);
      // The status and hint rows never collide with the well.
      REQUIRE(lay.status_y < lay.well_y);
      REQUIRE(lay.hint_y >= lay.well_y + kWellRows);
    }
  }
}

TEST_CASE("the hidden rows are never drawn", "[tetris][layout]") {
  // ⚠ cell_y maps BOARD rows, buffer included, so row 0 lands two rows above
  // the first visible one — which is on top of the frame. row_visible is the
  // guard, and a clamp inside cell_y instead would silently paint a spawning
  // piece over the well's top border.
  const Layout lay = compute_layout(80, 24);
  REQUIRE_FALSE(lay.row_visible(0));
  REQUIRE_FALSE(lay.row_visible(kHiddenRows - 1));
  REQUIRE(lay.row_visible(kHiddenRows));
  REQUIRE(lay.row_visible(kRows - 1));
  REQUIRE_FALSE(lay.row_visible(kRows));
}

// ── Rendering ───────────────────────────────────────────────────────────────

TEST_CASE("the bottom tier draws the stack, the piece and its ghost with "
          "distinct glyphs",
          "[tetris][render]") {
  Probe app;
  enter_tetris(app);
  Tetris* game = game_of(app);
  REQUIRE(game != nullptr);

  // A stack with a gap, and a piece resting well above it.
  const std::vector<std::string_view> rows = [] {
    std::vector<std::string_view> r(kVisibleRows, "..........");
    r[19] = "#########.";
    return r;
  }();
  REQUIRE(game->board().load(rows, Piece::O, 0, 0, kHiddenRows));
  app.step();

  const Layout& lay = game->layout();
  REQUIRE(lay.fits);

  const std::string stack = cell_text(app, lay, 0, kRows - 1);
  const std::string active = cell_text(app, lay, 0, kHiddenRows);
  const std::string empty = cell_text(app, lay, 5, kHiddenRows + 5);

  REQUIRE(stack == std::string(kAsciiCells.stack));
  REQUIRE(active == std::string(kAsciiCells.active));
  REQUIRE(empty == std::string(kAsciiCells.empty));

  // ⚠ Distinct ON SCREEN, not merely in the table. FallbackDriver discards
  // colour, and the reference tells its seven pieces apart by colour alone.
  REQUIRE(stack != active);
  REQUIRE(stack != empty);
  REQUIRE(active != empty);

  // The ghost is drawn somewhere below the piece and above the stack.
  const std::string all = screen_text(app);
  REQUIRE(all.find(std::string(kAsciiCells.ghost)) != std::string::npos);
}

TEST_CASE("the whole screen is 7-bit at the bottom tier", "[tetris][render]") {
  Probe app;
  enter_tetris(app);
  app.step(3);
  REQUIRE(all_seven_bit(app));
}

TEST_CASE("a terminal that is too small gets a screen saying so",
          "[tetris][render]") {
  Probe app;
  enter_tetris(app, 80, 24);
  // ⚠ Re-render smaller rather than entering smaller: the selector itself needs
  // room, and entering at 30x12 would fail before the game was ever reached.
  app.step(1, 30, 12);

  const std::string all = screen_text(app);
  REQUIRE(all.find("terminal too small") != std::string::npos);
  REQUIRE(all.find("needs 35x24") != std::string::npos);
}

TEST_CASE("the panel shows the hold and next boxes", "[tetris][render]") {
  Probe app;
  enter_tetris(app);
  app.step();
  const std::string all = screen_text(app);
  REQUIRE(all.find("HOLD") != std::string::npos);
  REQUIRE(all.find("NEXT") != std::string::npos);
  REQUIRE(all.find("score") != std::string::npos);
}

TEST_CASE("the NEXT panel advances when a piece locks", "[tetris][render]") {
  // ⚠ gitea #55 was reported VISUALLY — "the preview does not update" — and the
  // model cases in test/27tetris pin preview() without ever reaching the loop
  // that DRAWS it. Three failure modes live only here: painting preview()[0]
  // into all three boxes, dropping the y advance so they overwrite each other,
  // and feeding held() into the NEXT slot.
  //
  // ⚠ Relational, and it has to be: the board is seeded from entropy(), so no
  // case in this file may name a piece. It does not need to — box k after a
  // lock must be box k+1 from before it.
  //
  // At the bottom tier colour is discarded by the fallback driver, so what is
  // being compared is the rotation-0 FOOTPRINT. All seven are distinct in the
  // top two box rows (I is four across, O is a 2x2, T/J/L put one cell over a
  // bar at three different offsets, S and Z are mirrors), which is what makes a
  // text comparison exact here rather than approximate.
  Probe app;
  enter_tetris(app);
  app.step();

  // Located by the label rather than by hard-coded coordinates, so the panel is
  // free to move. Found once: it cannot move between the two reads below, and a
  // second scan would be a second chance to find a different "NEXT".
  int label_y = -1;
  int label_x = -1;
  for (int y = 0; y < app.screen().rows() && label_y < 0; ++y) {
    const std::size_t at = row_text(app, y).find("NEXT");
    if (at != std::string::npos) {
      label_y = y;
      label_x = static_cast<int>(at);
    }
  }
  REQUIRE(label_y >= 0);

  // ⚠ SLICED TO THE PANEL, not the whole row. Box k and box k+1 are on
  // different rows, so a full-width capture would also be comparing whatever
  // the well holds at those rows — passing today only because a board one drop
  // old is blank up there, and going red for a reason with nothing to do with
  // the preview as soon as anything is drawn behind it.
  const auto boxes = [&app, label_y, label_x] {
    std::array<std::string, kPreview> out{};
    for (int k = 0; k < kPreview; ++k) {
      for (int r = 0; r < 2; ++r) {
        out[static_cast<std::size_t>(k)] +=
            row_text(app, label_y + 1 + (k * 3) + r)
                .substr(static_cast<std::size_t>(label_x), kPanelCols);
      }
    }
    return out;
  };

  const auto before = boxes();
  // ⚠ Three DIFFERENT boxes, asserted without knowing the seed: an opening
  // preview is always three pieces from within ONE bag permutation, so they
  // cannot repeat. A panel drawing preview()[0] three times passes every other
  // assertion in this file.
  REQUIRE(before[0] != before[1]);
  REQUIRE(before[1] != before[2]);
  REQUIRE(before[0] != before[2]);

  app.dispatch_event(ch(U' '));  // hard drop: locks and spawns
  app.step();

  // One drop onto an empty field cannot clear a line or top out, so there is no
  // path here that skips the spawn.
  const auto after = boxes();
  REQUIRE(after[0] == before[1]);
  REQUIRE(after[1] == before[2]);
}

TEST_CASE("the status row drops whole fields rather than truncating one",
          "[tetris][render]") {
  // ⚠ THE MUTATION THAT HAS GONE GREEN IN TWO CONSECUTIVE EPICS. The status row
  // is drawn whether or not the well fits, and at any width the GAME fits at,
  // the priority loop stops appending long before the left-hand text can reach
  // the right-aligned word — so deleting the budget changes nothing observable.
  // The widths that matter are the ones NARROWER than kNeedCols.
  Probe app;
  enter_tetris(app);

  for (const int cols : {24, 28, 30, 34, 35, 48, 80, 120}) {
    app.step(1, cols, 24);
    const std::string row = row_text(app, 0);

    // Whatever survived, no field is half-written. Each label is present with a
    // number after it, or absent entirely.
    for (const std::string label : {"score ", "lines ", "level ", "start ",
                                    "record ", "longest "}) {
      const auto at = row.find(label);
      if (at == std::string::npos) continue;
      const auto digit = at + label.size();
      REQUIRE(digit < row.size());
      REQUIRE(std::isdigit(static_cast<unsigned char>(row[digit])) != 0);
    }

    // And the two halves never collide: the outcome word is intact at the right.
    REQUIRE(row.find("PLAYING") != std::string::npos);
  }
}

// ── Input ───────────────────────────────────────────────────────────────────

TEST_CASE("Escape and p are declined, so the Shell still owns them",
          "[tetris][input]") {
  // ⚠ A line that must stay ABSENT from tetris.cpp. Binding either here would
  // strand the player in the game.
  Probe app;
  enter_tetris(app);
  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);

  Probe other;
  enter_tetris(other);
  other.dispatch_event(key(termforge::Key::Escape));
  other.step(1);
  REQUIRE(other.state() == Shell::State::Selector);
}

TEST_CASE("the movement keys reach the board", "[tetris][input]") {
  Probe app;
  enter_tetris(app);
  Tetris* game = game_of(app);
  REQUIRE(game != nullptr);

  const int x0 = game->board().active().x;
  app.dispatch_event(key(termforge::Key::Left));
  REQUIRE(game_of(app)->board().active().x == x0 - 1);

  app.dispatch_event(ch(U'l'));
  REQUIRE(game_of(app)->board().active().x == x0);

  const int rot0 = game_of(app)->board().active().rot;
  app.dispatch_event(key(termforge::Key::Up));
  REQUIRE(game_of(app)->board().active().rot != rot0);

  // Hard drop locks, which is observable as the piece being back at the top.
  const int y_before = game_of(app)->board().active().y;
  app.dispatch_event(ch(U' '));
  REQUIRE(game_of(app)->board().active().y <= y_before);
}

TEST_CASE("a release is consumed rather than handed back to the Shell",
          "[tetris][input]") {
  // ⚠ The half that matters for gitea #32's gate. A release the game DECLINED
  // would fall through to Shell::handle_in_game_key — which is exactly where a
  // released Escape used to quit to the menu twice. Tetris consumes the ones it
  // binds, so nothing downstream ever sees them.
  Probe app;
  enter_tetris(app);
  Tetris* game = game_of(app);
  REQUIRE(game != nullptr);

  REQUIRE(game->on_event(ch_released(U'h')));
  REQUIRE(game->on_event(ch_released(U'j')));
  // A key it does not bind is still declined, release or not.
  REQUIRE_FALSE(game->on_event(ch_released(U'v')));
}

TEST_CASE("changing the start level restarts the game", "[tetris][input]") {
  // ⚠ Applying it mid-run would let a player bank an easy opening and finish on
  // a record whose key does not describe how it was earned.
  Probe app;
  enter_tetris(app);
  app.dispatch_event(ch(U' '));  // a hard drop, so there is progress to lose
  REQUIRE(game_of(app)->board().score() >= 0);

  app.dispatch_event(ch(U'3'));
  const auto& b = game_of(app)->board();
  REQUIRE(b.start_level() == StartLevel::Ten);
  REQUIRE(b.level() == 10);
  REQUIRE(b.score() == 0);
  REQUIRE(b.lines() == 0);
}

TEST_CASE("q returns to the menu", "[tetris][input]") {
  Probe app;
  enter_tetris(app);
  app.dispatch_event(ch(U'q'));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
}

// ── The degraded keyboard arm ───────────────────────────────────────────────

TEST_CASE("without the kitty protocol the game says so, in the hint row",
          "[tetris][keyboard][degraded]") {
  // ⚠ THE DEGRADATION CONTRACT, made visible rather than merely logged. The
  // Shell raises an ErrorEvent, but that lands on the SELECTOR's footer, which
  // the player is not looking at while playing — "the controls feel wrong and
  // nothing said why" is precisely the outcome the contract exists to prevent.
  //
  // ⚠ This is the only arm reachable here: FallbackDriver reports no
  // capabilities, so kitty_keyboard is false for every case in this file.
  Probe app;
  enter_tetris(app, 100, 30);
  app.step(1, 100, 30);

  REQUIRE(game_of(app)->hold_support() == HoldSupport::Discrete);
  const std::string hint = row_text(app, game_of(app)->layout().hint_y);
  REQUIRE(hint.find("no key-release") != std::string::npos);
}

TEST_CASE("a held shift does not auto-repeat on the degraded arm",
          "[tetris][keyboard][degraded]") {
  // The rule behind the message above: with no release, "held" and "pressed
  // again" are the same event, so auto-repeating would slide the piece on a key
  // the player let go of.
  Probe app;
  enter_tetris(app);
  Tetris* game = game_of(app);
  REQUIRE(game != nullptr);

  const int x0 = game->board().active().x;
  app.dispatch_event(key(termforge::Key::Left));
  REQUIRE(game_of(app)->board().active().x == x0 - 1);

  // Several seconds of frames, and nothing more moves sideways.
  app.step(60);
  REQUIRE(game_of(app)->board().active().x == x0 - 1);
}

// ── Sound intent ────────────────────────────────────────────────────────────

TEST_CASE("a hard drop sounds, and gravity does not", "[tetris][audio]") {
  // ⚠ Asserted through Shell::audio().play_count(), which counts INTENT rather
  // than samples, so these pass identically on a GLYPHCADE_WITH_AUDIO=OFF build.
  // ⚠ Through the SHELL's engine, never the Game's: the Game may already be
  // gone by the time we look.
  Probe app;
  enter_tetris(app);

  const auto before = app.audio().play_count(SfxId::Slide);
  app.dispatch_event(ch(U' '));
  REQUIRE(app.audio().play_count(SfxId::Slide) == before + 1);
}

TEST_CASE("gravity and auto-shift are silent", "[tetris][audio]") {
  // ⚠ A piece falls several times a second with no input at all. A sound on
  // that is a metronome rather than feedback — the argument that kept Spawn out
  // of 2048 and Step out of Snake, applying twice over in this game.
  Probe app;
  enter_tetris(app);

  const auto click = app.audio().play_count(SfxId::Click);
  const auto lock = app.audio().play_count(SfxId::Lock);
  const auto slide = app.audio().play_count(SfxId::Slide);

  app.step(30);  // half a second of frames, no input at all

  REQUIRE(app.audio().play_count(SfxId::Click) == click);
  REQUIRE(app.audio().play_count(SfxId::Slide) == slide);
  // At level 1 gravity is 1000 ms, so nothing has locked either.
  REQUIRE(app.audio().play_count(SfxId::Lock) == lock);
}

TEST_CASE("a rotation sounds only when it is accepted", "[tetris][audio]") {
  // ⚠ A refused rotation must be SILENT: there is no deny blip in the bank and
  // inventing one is a feel decision nobody who cannot hear it should make.
  Probe app;
  enter_tetris(app);
  Tetris* game = game_of(app);
  REQUIRE(game != nullptr);

  // Box the piece in so no kick can fit.
  const std::vector<std::string_view> rows = [] {
    std::vector<std::string_view> r(kVisibleRows, "###..#####");
    return r;
  }();
  REQUIRE(game->board().load(rows, Piece::T, 1, 2, 10 + kHiddenRows));

  const auto before = app.audio().play_count(SfxId::Click);
  app.dispatch_event(key(termforge::Key::Up));
  REQUIRE(app.audio().play_count(SfxId::Click) == before);
}

// ── Scores ──────────────────────────────────────────────────────────────────

TEST_CASE("the record key carries the start level", "[tetris][scores]") {
  // ⚠ Recorded under at least TWO keys with DIFFERENT values. A case that only
  // ever records under one leaves half the key untested — which is exactly how
  // Snake's score_key() mutation went green, and it is the third epic in a row
  // where this shape matters.
  Probe app;
  enter_tetris(app);

  // Start level 1, and bank something.
  app.dispatch_event(ch(U'1'));
  Tetris* game = game_of(app);
  REQUIRE(game != nullptr);
  const std::vector<std::string_view> ready = [] {
    std::vector<std::string_view> r(kVisibleRows, "..........");
    r[19] = ".#########";
    return r;
  }();
  REQUIRE(game->board().load(ready, Piece::I, 1, -2, kHiddenRows));
  app.dispatch_event(ch(U' '));
  app.step(30);  // let the line-clear freeze run out

  const auto one = app.scores().get("tetris", "best_score_start1");
  REQUIRE(one.has_value());
  REQUIRE(*one > 0);

  // ⚠ ALL THREE keys, not two. Recording under 1 and 10 leaves the middle
  // branch of score_key() unevaluated, and a mutation that returned start1
  // from it stayed green — the same shape as Snake's, where the wrap branch of
  // its key was never reached because every case recorded in one mode.
  app.dispatch_event(ch(U'2'));
  REQUIRE(game_of(app)->board().load(ready, Piece::I, 1, -2, kHiddenRows));
  app.dispatch_event(ch(U' '));
  app.step(30);

  const auto five = app.scores().get("tetris", "best_score_start5");
  REQUIRE(five.has_value());
  REQUIRE(*five > *one);

  // Start level 10 scores the same clear higher again, so no two records can be
  // confused for each other.
  app.dispatch_event(ch(U'3'));
  REQUIRE(game_of(app)->board().load(ready, Piece::I, 1, -2, kHiddenRows));
  app.dispatch_event(ch(U' '));
  app.step(30);

  const auto ten = app.scores().get("tetris", "best_score_start10");
  REQUIRE(ten.has_value());
  REQUIRE(*ten > *five);

  // ⚠ And the earlier records did NOT follow them up, which is the whole point
  // of keying: one shared key would have let the level-10 run walk them both up.
  REQUIRE(app.scores().get("tetris", "best_score_start1") == one);
  REQUIRE(app.scores().get("tetris", "best_score_start5") == five);
}

TEST_CASE("lines are recorded separately from score", "[tetris][scores]") {
  // ⚠ TWO keys, where Snake has one. Snake refused best_length because length
  // and score were affine restatements of each other. Here a tetris scores four
  // times a single for the same four rows and both drops pay points, so a long
  // endurance run and a short high-scoring one genuinely disagree.
  Probe app;
  enter_tetris(app);
  Tetris* game = game_of(app);
  REQUIRE(game != nullptr);

  const std::vector<std::string_view> ready = [] {
    std::vector<std::string_view> r(kVisibleRows, "..........");
    r[19] = ".#########";
    return r;
  }();
  REQUIRE(game->board().load(ready, Piece::I, 1, -2, kHiddenRows));
  app.dispatch_event(ch(U' '));
  app.step(30);

  REQUIRE(app.scores().get("tetris", "best_lines_start1") == 1);
  REQUIRE(app.scores().get("tetris", "best_score_start1").value_or(0) > 1);
}

// ── The pre-start options screen (gitea #38) ────────────────────────────────

namespace {
// Enter tetris and STOP on the options screen, which enter_tetris() dismisses.
auto enter_to_options(Probe& app) -> Tetris* {
  app.step(1, 80, 24);
  const int index = tetris_index();
  REQUIRE(index >= 0);
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);
  app.step(1, 80, 24);
  Tetris* g = game_of(app);
  REQUIRE(g != nullptr);
  return g;
}
}  // namespace

TEST_CASE("entering tetris shows the options screen, not the well",
          "[tetris][options]") {
  Probe app;
  enter_to_options(app);
  std::string all;
  for (int y = 0; y < 24; ++y) all += row_text(app, y) + "\n";
  INFO(all);
  CHECK(all.find("Start level") != std::string::npos);
  CHECK(all.find("Enter start") != std::string::npos);
  CHECK(all.find("PLAYING") == std::string::npos);
  CHECK(all_seven_bit(app));
}

TEST_CASE("gravity does not run while the options screen is up",
          "[tetris][options]") {
  // ⚠ The gate in Tetris::tick. Without it the piece falls behind the screen
  // and can lock -- or top out -- before a start level has been chosen. Every
  // enter_tetris() dismisses before its first step(), so deleting the gate
  // leaves this whole suite green.
  //
  // ⚠ tick() is called directly: Probe sets frame_ms(0), so app.step(N) passes
  // almost no real time and never crosses the gravity interval.
  Probe app;
  Tetris* g = enter_to_options(app);
  const int y_before = g->board().active().y;
  const int lines_before = g->board().lines();

  for (int i = 0; i < 240; ++i) {
    g->tick(std::chrono::duration<double>{1.0 / 60.0});
  }
  CHECK(g->board().active().y == y_before);
  CHECK(g->board().lines() == lines_before);
  CHECK(g->ticks() == 240);  // the ticks arrived; the gate is why nothing fell

  // ⚠ THE CONTROL: without it this passes against a Tetris whose gravity is
  // broken outright. Dismiss and the same dt must move the piece.
  app.dispatch_event(key(termforge::Key::Enter));
  Tetris* live = game_of(app);
  REQUIRE(live != nullptr);
  const auto started = live->board().active();
  for (int i = 0; i < 240; ++i) {
    live->tick(std::chrono::duration<double>{1.0 / 60.0});
  }
  // Either the piece has fallen, or it locked and a new one spawned -- both are
  // "gravity ran", and pinning only one of them makes the control flaky.
  const auto after = live->board().active();
  CHECK((after.y != started.y || after.piece != started.piece ||
         after.x != started.x));
}

TEST_CASE("the chosen start level is what the game begins on",
          "[tetris][options]") {
  Probe app;
  enter_to_options(app);
  app.dispatch_event(key(termforge::Key::Right));   // 1 -> 5
  app.dispatch_event(key(termforge::Key::Right));   // 5 -> 10
  app.dispatch_event(key(termforge::Key::Enter));
  app.step(1, 80, 24);

  Tetris* g = game_of(app);
  REQUIRE(g != nullptr);
  CHECK(g->board().start_level() == StartLevel::Ten);
}

TEST_CASE("accepting the default starts tetris on level one",
          "[tetris][options]") {
  // ⚠ The pair of the case above. A game that ignored selected() and always
  // used default_index would pass this one alone.
  Probe app;
  enter_tetris(app);
  Tetris* g = game_of(app);
  REQUIRE(g != nullptr);
  CHECK(g->board().start_level() == StartLevel::One);
}

TEST_CASE("HoldSupport survives the options screen, on BOTH arms",
          "[tetris][options]") {
  // What this pins: dismissing the pre-start screen calls new_game(), which
  // rebuilds the Board as `tetris::Board(level, m_board.hold_support(), seed)`.
  // The degradation arm start() chose must survive that rebuild -- otherwise
  // picking a start level would silently cost you DAS.
  //
  // ⚠ Driven through a hand-built GameContext, NOT through the Shell, and that
  // is the whole reason this case can exist. test_run_frames installs a
  // FallbackDriver whose capabilities are all false, so under the Shell
  // kitty_keyboard is never true and the Held arm below is unreachable -- the
  // same blind spot the SFX bank has had since Epic 2.
  // GameContext::set_capabilities is public precisely so a degradation arm can
  // be chosen by a test instead of by a terminal.
  //
  // ⚠ AND A CORRECTION, recorded because the wrong version was nearly shipped
  // as a load-bearing comment. This case was written believing the ORDER of
  // start()'s `m_board.reset(...)` and `m_options.open(...)` was a trap -- open
  // first and the constructor's Discrete would leak through. Mutation testing
  // says otherwise: swapping them is green even on the Held arm, because open()
  // never touches the board and both calls are in start(), so the reset has
  // always happened before any dismissal can. The reasoning was plausible and
  // wrong. The case is still worth having -- it covers the Held arm, which
  // nothing else does -- but not for the reason it was written.
  using glyphcade::GameContext;

  const auto run = [](bool kitty) {
    GameContext ctx;
    termforge::Capabilities caps{};
    caps.kitty_keyboard = kitty;
    ctx.set_capabilities(caps);

    Tetris game;
    game.start(ctx);
    const auto at_start = game.hold_support();

    // Dismiss the options screen without changing anything.
    REQUIRE(game.on_event(key(termforge::Key::Enter)));
    return std::pair{at_start, game.hold_support()};
  };

  SECTION("no kitty protocol: Discrete, and it stays Discrete") {
    const auto [at_start, after] = run(false);
    CHECK(at_start == HoldSupport::Discrete);
    CHECK(after == HoldSupport::Discrete);
  }

  SECTION("kitty protocol granted: Held, and dismissal must not lose it") {
    // ⚠ This is the arm nothing in this container could reach before. It is
    // also the only assertion that fails when start() opens the screen too
    // early -- the Discrete arm above passes either way.
    const auto [at_start, after] = run(true);
    CHECK(at_start == HoldSupport::Held);
    CHECK(after == HoldSupport::Held);
  }
}

TEST_CASE("Escape from tetris' options screen goes back to the menu",
          "[tetris][options]") {
  Probe app;
  enter_to_options(app);
  app.dispatch_event(key(termforge::Key::Escape));
  app.step(1, 80, 24);
  CHECK(app.state() == Shell::State::Selector);
}
