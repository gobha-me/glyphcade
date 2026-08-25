// Snake's GEOMETRY, RENDERING, INPUT and the SOUND that input asks for, through
// a real Shell into an offscreen Screen. The rules and the step clock live in
// test/25snake and are not re-tested here.
//
// ⚠ test_run_frames installs a FallbackDriver, whose capabilities() reports
// all-false — so the Shell syncs to BorderStyle::Ascii and EVERY case in this
// file exercises the bottom tier. For Snake that matters more than it does for
// either other game: the reference distinguishes head, body and food by COLOUR
// alone (renderer.js paints the same rounded rectangle for all three), so a
// faithful port would be a board of identical marks here. What these cases prove
// is that the board is legible WITHOUT colour.
//
// ⚠ NEVER hold a Screen& across a step(). App::test_run_frames reassigns the
// Screen on every call, so a reference taken before a frame dangles after it —
// as a segfault mid-suite, not a wrong value. Every helper takes the Probe and
// fetches the Screen itself.
//
// ⚠ NEVER read the Game* after dispatching a key that ends the game.
// Shell::handle_in_game_key calls apply_transitions() as soon as the game
// consumes a key, so done() is polled and the game may be destroyed before
// dispatch_event returns. ASan found that one in Epic 3.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <termforge/core/types.hpp>

#include <glyphcade/arcade/registry.hpp>
#include <glyphcade/arcade/shell.hpp>
#include <glyphcade/games/snake/glyphs.hpp>
#include <glyphcade/games/snake/snake.hpp>

namespace {

using glyphcade::Shell;
using glyphcade::Snake;
using glyphcade::audio::SfxId;
using namespace glyphcade::snake;

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

[[nodiscard]] auto snake_index() -> int {
  const auto games = glyphcade::all_games();
  for (std::size_t i = 0; i < games.size(); ++i) {
    if (games[i].meta.slug == "snake") {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Enter Snake from the selector, whatever position it holds in the roster.
auto enter_snake(Probe& app, int cols = 80, int rows = 24) -> void {
  app.step(1, cols, rows);
  const int index = snake_index();
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
  // term-game#38: entering a game now opens its pre-start options screen, so a
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

[[nodiscard]] auto game_of(Shell& shell) -> Snake* {
  return dynamic_cast<Snake*>(const_cast<glyphcade::Game*>(
      static_cast<const glyphcade::Game*>(shell.current_game())));
}

[[nodiscard]] auto row_text(Probe& app, int y) -> std::string {
  std::string out;
  const auto& s = app.screen();
  for (int x = 0; x < s.cols(); ++x) {
    const auto t = s.text_at(x, y);
    out += t.empty() ? " " : t;
  }
  return out;
}

[[nodiscard]] auto all_seven_bit(Probe& app) -> bool {
  const auto& s = app.screen();
  for (int y = 0; y < s.rows(); ++y) {
    for (int x = 0; x < s.cols(); ++x) {
      for (const char c : s.text_at(x, y)) {
        if (static_cast<unsigned char>(c) >= 0x80) {
          return false;
        }
      }
    }
  }
  return true;
}

// The two screen columns a board cell occupies, read back as one string.
[[nodiscard]] auto cell_text(Probe& app, const Layout& lay, Coord p)
    -> std::string {
  std::string out;
  const auto& s = app.screen();
  for (int i = 0; i < kCellCols; ++i) {
    const auto t = s.text_at(lay.cell_x(p.x) + i, lay.cell_y(p.y));
    out += t.empty() ? " " : t;
  }
  return out;
}

// A horizontal snake heading right, head at (x, y).
[[nodiscard]] auto snake_at(int x, int y, int len = kStartLen)
    -> std::vector<Coord> {
  std::vector<Coord> body;
  for (int i = 0; i < len; ++i) {
    body.push_back(Coord{x - i, y});
  }
  return body;
}

}  // namespace

// ── Geometry ─────────────────────────────────────────────────────────────────

TEST_CASE("the board needs 58x20 and says so in one place", "[snake][layout]") {
  // The constants layout.hpp asserts, restated as a runtime case so the number
  // in STATUS.md and the number the game reports come from the same arithmetic.
  REQUIRE(kNeedCols == (kCellCols * kCols) + kChromeCols);
  REQUIRE(kNeedRows == kRows + kChromeRows);
  REQUIRE(kNeedCols == 58);
  REQUIRE(kNeedRows == 20);
}

TEST_CASE("every cell lands inside the frame at every size that fits",
          "[snake][layout]") {
  // ⚠ The failure this exists to prevent is a write on the frame's border, which
  // looks like a rendering glitch and is actually an off-by-one in the geometry.
  // Both columns of the widest cell must be strictly inside.
  for (const int cols : {58, 59, 60, 80, 120}) {
    for (const int rows : {20, 21, 24, 40}) {
      const auto lay = compute_layout(cols, rows);
      REQUIRE(lay.fits);

      REQUIRE(lay.cell_x(0) == lay.frame_x + 1);
      REQUIRE(lay.cell_y(0) == lay.frame_y + 1);
      // The last column of the last cell is the last interior column.
      REQUIRE(lay.cell_x(kCols - 1) + kCellCols - 1 ==
              lay.frame_x + lay.frame_w - 2);
      REQUIRE(lay.cell_y(kRows - 1) == lay.frame_y + lay.frame_h - 2);
      // And the frame itself is on screen, below the status row and above the
      // hint row.
      REQUIRE(lay.frame_x >= 0);
      REQUIRE(lay.frame_y > lay.status_y);
      REQUIRE(lay.frame_y + lay.frame_h <= lay.hint_y);
    }
  }
}

TEST_CASE("a screen one column or one row short does not fit",
          "[snake][layout]") {
  REQUIRE_FALSE(compute_layout(kNeedCols - 1, kNeedRows).fits);
  REQUIRE_FALSE(compute_layout(kNeedCols, kNeedRows - 1).fits);
  REQUIRE(compute_layout(kNeedCols, kNeedRows).fits);
}

// ── Rendering ────────────────────────────────────────────────────────────────

TEST_CASE("the whole Snake screen is 7-bit at the ASCII tier",
          "[snake][render]") {
  // The promise AGENTS.md makes for every game, swept cell by cell rather than
  // argued. Note what this canNOT see: a non-ASCII byte in a field the viewport
  // does not include. That is why GameMeta's prose is checked at COMPILE time in
  // all_games.cpp instead — see the em dash that reached a bare pty in Epic 4.
  Probe app;
  enter_snake(app);
  app.step(3);
  REQUIRE(all_seven_bit(app));
}

TEST_CASE("head, body and food are told apart without any colour",
          "[snake][render]") {
  // ⚠ THE CASE THIS FILE EXISTS FOR. The reference separates all three by colour
  // and nothing else; FallbackDriver discards colour outright. glyphs.hpp makes
  // that a compile error, and this makes it an observation.
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  const std::vector<Coord> body = snake_at(10, 5);
  g->board().load(body, Coord{20, 9});
  app.step(1);

  const auto& lay = g->layout();
  REQUIRE(lay.fits);

  const std::string head = cell_text(app, lay, Coord{10, 5});
  const std::string trunk = cell_text(app, lay, Coord{9, 5});
  const std::string food = cell_text(app, lay, Coord{20, 9});
  const std::string empty = cell_text(app, lay, Coord{2, 2});

  REQUIRE(head == std::string(kAsciiCells.head));
  REQUIRE(trunk == std::string(kAsciiCells.body));
  REQUIRE(food == std::string(kAsciiCells.food));
  REQUIRE(empty == std::string(kAsciiCells.empty));

  // Pairwise distinct on screen, not merely in the table.
  REQUIRE(head != trunk);
  REQUIRE(head != food);
  REQUIRE(trunk != food);
}

TEST_CASE("the head is drawn over the food, not under it", "[snake][render]") {
  // Reachable in play exactly once: on a full board there is no free cell to
  // respawn into, so the last food stays where the head just ate it. Drawing
  // order is the only thing that decides which of the two the player sees, and
  // "you are here" outranks "there was food here".
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  g->board().load(snake_at(10, 5), Coord{10, 5});  // food UNDER the head
  app.step(1);

  const auto& lay = g->layout();
  REQUIRE(cell_text(app, lay, Coord{10, 5}) == std::string(kAsciiCells.head));
}

TEST_CASE("a dead head is drawn as a corpse, not as a head", "[snake][render]") {
  // Derived presentation over resolved state — dying is a rule, the corpse is
  // not. It is also the only thing on the board that says "this run is over"
  // besides the status word.
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  // One step from the right wall, heading right, in Solid mode.
  g->board().load(snake_at(kCols - 1, 5), Coord{2, 2});
  g->board().tick(std::chrono::duration<double>{0.2});
  REQUIRE(g->board().state() == State::Lost);
  app.step(1);

  const auto& lay = g->layout();
  REQUIRE(cell_text(app, lay, Coord{kCols - 1, 5}) ==
          std::string(kAsciiCells.dead));
}

TEST_CASE("the status row shows every field it shows in full",
          "[snake][render]") {
  // ⚠ EVERY field that appears must appear in full, not just the first one.
  // Epic 4 learned this the hard way: with the reserved gap removed, the
  // counters ran into the right-aligned word and a field was left rendered as
  // its label with the digits eaten, while the case still passed because it only
  // checked the first one. A missing field reads as a narrow terminal; a
  // half-written one reads as a wrong score.
  //
  // ⚠ And no label here may be a substring of another, or find(label) would
  // match the wrong field — which is why the length field is spelled "length"
  // and not "len", next to "level".
  //
  // ⚠ 40 AND 50 ARE THE WIDTHS THAT MAKE THIS CASE MEAN ANYTHING, and leaving
  // them out is how the first draft passed with the budget deleted. The five
  // fields total 63 columns; at 58 and above the priority loop stops early
  // anyway, so the left-hand text never reaches the right-aligned word and
  // removing the budget changes nothing observable. It is only on a screen too
  // narrow for the board — where the status row is still drawn — that an
  // unbudgeted row runs into PLAYING and leaves a field truncated. Epic 4's
  // status-row mutation went green for exactly this reason.
  for (const int cols : {40, 50, 58, 64, 80, 120}) {
    Probe app;
    enter_snake(app, cols, 24);
    auto* g = game_of(app);
    REQUIRE(g != nullptr);

    // A long snake with a big score, so every counter is at its widest.
    g->board().load(snake_at(20, 8, 20), Coord{2, 2}, /*eaten=*/17);
    app.step(1, cols, 24);

    const std::string status = row_text(app, 0);
    REQUIRE(status.find("PLAYING") != std::string::npos);

    for (const auto& [label, full] :
         std::vector<std::pair<std::string, std::string>>{
             {"score", "score 170"},
             {"length", "length 20"},
             {"level", "level normal"},
             {"walls", "walls solid"},
             {"record", "record "}}) {
      if (status.find(label) != std::string::npos) {
        REQUIRE(status.find(full) != std::string::npos);
      }
    }
  }
}

TEST_CASE("the outcome is a word, not a colour", "[snake][render]") {
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  REQUIRE(row_text(app, 0).find("PLAYING") != std::string::npos);

  g->board().load(snake_at(kCols - 1, 5), Coord{2, 2});
  g->board().tick(std::chrono::duration<double>{0.2});
  app.step(1);
  REQUIRE(row_text(app, 0).find("GAME OVER") != std::string::npos);
}

TEST_CASE("a screen too small for the board says so", "[snake][render]") {
  // The same answer both other games give, and the same open issue behind it:
  // GameMeta carries no minimum size (term-game#15), so the selector will launch a
  // board this terminal cannot draw.
  Probe app;
  enter_snake(app, 40, 12);
  app.step(1, 40, 12);

  bool found = false;
  for (int y = 0; y < 12; ++y) {
    if (row_text(app, y).find("Snake needs 58x20") != std::string::npos) {
      found = true;
    }
  }
  REQUIRE(found);
}

// ── Input ────────────────────────────────────────────────────────────────────

TEST_CASE("arrows, hjkl and wasd all steer", "[snake][input]") {
  struct Case {
    termforge::Event ev;
    Dir want;
  };
  // Up from the default heading (Right) is legal for all three spellings; the
  // point is that each spelling arrives, not that each direction is legal.
  const Case cases[]{
      {key(termforge::Key::Up), Dir::Up},
      {ch(U'k'), Dir::Up},
      {ch(U'w'), Dir::Up},
      {key(termforge::Key::Down), Dir::Down},
      {ch(U'j'), Dir::Down},
      {ch(U's'), Dir::Down},
  };

  for (const auto& c : cases) {
    Probe app;
    enter_snake(app);
    auto* g = game_of(app);
    REQUIRE(g != nullptr);
    g->board().load(snake_at(10, 5), Coord{2, 2});

    app.dispatch_event(c.ev);
    REQUIRE(g->board().queued().size() == 1);
    REQUIRE(g->board().queued()[0] == c.want);
  }
}

TEST_CASE("1/2/3 change the difficulty and restart", "[snake][input]") {
  // Minesweeper's binding, and for its reason: applying a difficulty change
  // mid-run would let a player bank an easy opening onto a record whose key
  // says otherwise.
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  g->board().load(snake_at(10, 5), Coord{2, 2}, /*eaten=*/5);
  REQUIRE(g->board().eaten() == 5);

  app.dispatch_event(ch(U'3'));
  REQUIRE(g->board().level() == Level::Hard);
  REQUIRE(g->board().eaten() == 0);
  REQUIRE(g->board().length() == kStartLen);
  REQUIRE(g->board().interval_ms() == 80);

  app.dispatch_event(ch(U'1'));
  REQUIRE(g->board().level() == Level::Easy);
  REQUIRE(g->board().interval_ms() == 120);
}

TEST_CASE("m toggles the wall mode and restarts, keeping the difficulty",
          "[snake][input]") {
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  app.dispatch_event(ch(U'3'));
  REQUIRE(g->board().walls() == Walls::Solid);
  REQUIRE(g->board().level() == Level::Hard);

  app.dispatch_event(ch(U'm'));
  REQUIRE(g->board().walls() == Walls::Wrap);
  REQUIRE(g->board().level() == Level::Hard);  // preserved across the toggle

  app.dispatch_event(ch(U'M'));
  REQUIRE(g->board().walls() == Walls::Solid);
}

TEST_CASE("Escape and p are declined, so the Shell still owns them",
          "[snake][input]") {
  // ⚠ A line that must stay ABSENT from snake.cpp. A game that consumed either
  // would strand the player inside it — Escape is quit-to-menu and 'p' is pause,
  // and term-game#6's "pause" scope item is satisfied by NOT writing one.
  Probe app;
  enter_snake(app);

  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);

  // Close the dialog, then check Escape still leaves.
  app.dispatch_event(key(termforge::Key::Escape));
  app.step(1);

  Probe other;
  enter_snake(other);
  other.dispatch_event(key(termforge::Key::Escape));
  other.step(1);
  REQUIRE(other.state() == Shell::State::Selector);
}

TEST_CASE("the wall mode the player chose is the one that runs",
          "[snake][input]") {
  // The mode is a player-facing toggle rather than a compile-time option, so it
  // has to survive the trip from the keystroke into the rules.
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  app.dispatch_event(ch(U'm'));
  REQUIRE(g->board().walls() == Walls::Wrap);

  g->board().load(snake_at(kCols - 1, 5), Coord{2, 2});
  // Exactly one step: 150 ms buys one 100 ms interval and banks 50. Not 200,
  // which would buy two and land the head at x == 1 — and not exactly 100,
  // which would make the case depend on a floating-point tie.
  g->board().tick(std::chrono::duration<double>{0.15});

  REQUIRE(g->board().state() == State::Running);
  REQUIRE(g->board().head() == Coord{0, 5});
}

// ── Sound ────────────────────────────────────────────────────────────────────
//
// Asserted through Shell::audio().play_count(), which counts INTENT rather than
// samples — so these cases pass identically on a GLYPHCADE_WITH_AUDIO=OFF build.
// ⚠ Through the SHELL's engine, never the Game's: the Game may already be gone.

TEST_CASE("an accepted turn clicks", "[snake][audio]") {
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);
  g->board().load(snake_at(10, 5), Coord{2, 2});

  const auto before = app.audio().play_count(SfxId::Click);
  app.dispatch_event(key(termforge::Key::Up));
  REQUIRE(app.audio().play_count(SfxId::Click) == before + 1);
}

TEST_CASE("a REFUSED turn is silent", "[snake][audio]") {
  // ⚠ There is no deny blip in the bank and inventing one is a feel decision
  // nobody who cannot hear it should make — the same call 2048 and minesweeper
  // both make for a no-op gesture. This is the case that keeps the guard in
  // steer() load-bearing.
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);
  g->board().load(snake_at(10, 5), Coord{2, 2});
  REQUIRE(g->board().direction() == Dir::Right);

  const auto click = app.audio().play_count(SfxId::Click);
  const auto eat = app.audio().play_count(SfxId::Eat);
  const auto lose = app.audio().play_count(SfxId::Lose);

  app.dispatch_event(key(termforge::Key::Left));   // a reversal into the neck
  app.dispatch_event(key(termforge::Key::Right));  // already going that way

  REQUIRE(app.audio().play_count(SfxId::Click) == click);
  REQUIRE(app.audio().play_count(SfxId::Eat) == eat);
  REQUIRE(app.audio().play_count(SfxId::Lose) == lose);
}

TEST_CASE("eating sounds once, and plain movement does not sound at all",
          "[snake][audio]") {
  // ⚠ The one place Snake's soundscape differs in KIND from the other two games.
  // It steps several times a second with no input at all, so a per-step sound is
  // not feedback, it is a metronome. term-game#6 asks for "eat, turn, die" and that
  // list is exactly right.
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  // Food one step ahead, then clear road for several more steps.
  g->board().load(snake_at(10, 5), Coord{11, 5});

  const auto eat_before = app.audio().play_count(SfxId::Eat);
  g->tick(std::chrono::duration<double>{0.15});  // one step: the meal
  REQUIRE(app.audio().play_count(SfxId::Eat) == eat_before + 1);

  // Move the food far away and run on. Nothing should sound.
  g->board().load(snake_at(10, 5), Coord{2, 2});
  const auto eat_mid = app.audio().play_count(SfxId::Eat);
  const auto click_mid = app.audio().play_count(SfxId::Click);
  g->tick(std::chrono::duration<double>{0.55});  // five steps, no food, no keys
  REQUIRE(app.audio().play_count(SfxId::Eat) == eat_mid);
  REQUIRE(app.audio().play_count(SfxId::Click) == click_mid);
}

TEST_CASE("dying sounds Lose", "[snake][audio]") {
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);
  g->board().load(snake_at(kCols - 1, 5), Coord{2, 2});

  const auto before = app.audio().play_count(SfxId::Lose);
  g->tick(std::chrono::duration<double>{0.2});
  REQUIRE(g->board().state() == State::Lost);
  REQUIRE(app.audio().play_count(SfxId::Lose) == before + 1);
}

// ── Persistence ──────────────────────────────────────────────────────────────

TEST_CASE("the record is keyed by BOTH the difficulty and the wall mode",
          "[snake][scores]") {
  // ⚠ Wrap removes four of the five ways to die, so a single per-difficulty
  // record would let a wrap run permanently outrank every solid one. The keys
  // are switched on the enums, never derived from the UI labels — renaming a
  // label must not orphan a player's records.
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  // Eat once in the default Normal/Solid.
  g->board().load(snake_at(10, 5), Coord{11, 5});
  g->tick(std::chrono::duration<double>{0.15});
  REQUIRE(g->board().score() == 10);

  REQUIRE(app.scores().get("snake", "best_score_normal_solid") == 10);
  REQUIRE_FALSE(app.scores().get("snake", "best_score_normal_wrap").has_value());
  REQUIRE_FALSE(app.scores().get("snake", "best_score_hard_solid").has_value());

  // ⚠ NOW RECORD IN THE OTHER MODE, and with a HIGHER score. Without this half
  // the case is blind: a score_key() that ignored the wall mode entirely would
  // still satisfy every assertion above, because nothing above ever records in
  // wrap. Mutation testing found that — the first draft passed with the wrap
  // branch returning the solid key.
  //
  // 50 is deliberately larger than 10, so a shared key would be VISIBLE: with
  // record() monotone, the solid record would be walked up to 50 rather than
  // merely being written twice.
  app.dispatch_event(ch(U'm'));
  REQUIRE(g->board().walls() == Walls::Wrap);
  g->board().load(snake_at(10, 5), Coord{11, 5}, /*eaten=*/4);
  g->tick(std::chrono::duration<double>{0.15});
  REQUIRE(g->board().score() == 50);

  REQUIRE(app.scores().get("snake", "best_score_normal_wrap") == 50);
  REQUIRE(app.scores().get("snake", "best_score_normal_solid") == 10);
}

TEST_CASE("the record survives a restart and does not follow the score down",
          "[snake][scores]") {
  // Store::record() is monotone, which is why recording after every food needs
  // no end-of-run hook: a fresh game's score of 0 cannot displace what is held.
  Probe app;
  enter_snake(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  g->board().load(snake_at(10, 5), Coord{11, 5}, /*eaten=*/9);
  g->tick(std::chrono::duration<double>{0.15});
  REQUIRE(app.scores().get("snake", "best_score_normal_solid") == 100);

  app.dispatch_event(ch(U'n'));
  REQUIRE(g->board().score() == 0);
  REQUIRE(app.scores().get("snake", "best_score_normal_solid") == 100);
}

// ── The pre-start options screen (term-game#38) ──────────────────────────────

TEST_CASE("entering snake shows the options screen, not the field",
          "[snake][options]") {
  // ⚠ enter_snake() is NOT used here: it dismisses the screen, which is exactly
  // what this case needs to observe. Everything below the helper's second Enter
  // is what the other twenty cases test; this is what the helper skips past.
  Probe app;
  app.step(1, 80, 24);
  const int index = snake_index();
  REQUIRE(index >= 0);
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);
  app.step(1, 80, 24);

  // The settings, and how to leave. Not a board.
  std::string all;
  for (int y = 0; y < 24; ++y) all += row_text(app, y) + "\n";
  INFO(all);
  CHECK(all.find("Level") != std::string::npos);
  CHECK(all.find("Walls") != std::string::npos);
  CHECK(all.find("Enter start") != std::string::npos);
  // The status row's outcome word belongs to a run in progress.
  CHECK(all.find("PLAYING") == std::string::npos);
  CHECK(all_seven_bit(app));
}

TEST_CASE("the board does not move while the options screen is up",
          "[snake][options]") {
  // ⚠ THE GATE IN Snake::tick, and it needs its own case because every
  // enter_snake() dismisses before the first step(). Delete the gate and the
  // rest of this suite stays green.
  //
  // ⚠ Snake::tick is called DIRECTLY rather than driven through app.step().
  // Probe sets frame_ms(0), so 240 frames pass almost no real time and the
  // Shell's accumulator yields a handful of ticks -- nowhere near Snake's step
  // interval, so a step()-driven version of this case would pass against the
  // mutant for the wrong reason. It measured 4 ticks when it wanted 100.
  Probe app;
  app.step(1, 80, 24);
  const int index = snake_index();
  REQUIRE(index >= 0);
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  app.step(1, 80, 24);

  Snake* g = game_of(app);
  REQUIRE(g != nullptr);
  const auto head_before = g->board().head();

  // ⚠ A BASELINE, NOT ZERO, and this used to be an absolute `ticks() == 120`.
  // ticks() counts EVERY tick the game receives, and the two app.step() calls
  // above deliver some: test_run_frames deliberately does not reset the tick
  // clock, so real wall time spent drawing selector frames becomes ticks the
  // Shell's accumulator hands over. On a quiet machine that is zero and the
  // absolute form passed for six releases. It is not a property of the code
  // under test — term-game#42 added a few string builds to draw_selector and this
  // case went red five runs in six on the loaded TSan build, pointing at Snake
  // for something Snake had no part in. What the case means is that the 120
  // ticks below ARRIVED, so that is what it now measures.
  const int ticks_before = g->ticks();

  // ⚠ Far ENOUGH. Snake's step interval is hundreds of milliseconds, so a small
  // dt passes against the mutant and proves nothing. Two seconds is several
  // steps at every difficulty.
  for (int i = 0; i < 120; ++i) {
    g->tick(std::chrono::duration<double>{1.0 / 60.0});
  }
  CHECK(g->board().head() == head_before);
  CHECK(g->board().state() == State::Running);
  // The ticks ARRIVED; the gate above is why they did nothing.
  CHECK(g->ticks() - ticks_before == 120);

  // ⚠ THE CONTROL. Without it this case passes against a Snake whose board
  // cannot move at all -- a broken tick(), a frozen board, a wrong fixture.
  // Dismiss, tick the same amount, and the head must move.
  app.dispatch_event(key(termforge::Key::Enter));
  Snake* live = game_of(app);
  REQUIRE(live != nullptr);
  const auto head_after_start = live->board().head();
  for (int i = 0; i < 120; ++i) {
    live->tick(std::chrono::duration<double>{1.0 / 60.0});
  }
  CHECK(live->board().head() != head_after_start);
}

TEST_CASE("the chosen level and walls are what the run starts on",
          "[snake][options]") {
  Probe app;
  app.step(1, 80, 24);
  const int index = snake_index();
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  app.step(1, 80, 24);

  // Level -> Hard (index 2, from the default Normal at 1), then down a row and
  // Walls -> Wrap.
  app.dispatch_event(key(termforge::Key::Right));
  app.dispatch_event(key(termforge::Key::Down));
  app.dispatch_event(key(termforge::Key::Right));
  app.dispatch_event(key(termforge::Key::Enter));
  app.step(1, 80, 24);

  Snake* g = game_of(app);
  REQUIRE(g != nullptr);
  CHECK(g->board().level() == Level::Hard);
  CHECK(g->board().walls() == Walls::Wrap);
}

TEST_CASE("accepting the defaults starts on Normal and Solid",
          "[snake][options]") {
  // ⚠ The PAIR of the case above, and neither is sufficient alone. A game that
  // ignored selected() and always applied the schema default would pass this
  // one; a game that applied selected() correctly passes both.
  Probe app;
  enter_snake(app);
  Snake* g = game_of(app);
  REQUIRE(g != nullptr);
  CHECK(g->board().level() == Level::Normal);
  CHECK(g->board().walls() == Walls::Solid);
}

TEST_CASE("Escape from the options screen goes back to the menu",
          "[snake][options]") {
  // The screen must not be a trap. OptionsScreen returns Ignored for Escape so
  // the game returns false and the Shell's quit-to-menu still fires.
  Probe app;
  app.step(1, 80, 24);
  const int index = snake_index();
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);
  app.step(1, 80, 24);

  app.dispatch_event(key(termforge::Key::Escape));
  app.step(1, 80, 24);
  CHECK(app.state() == Shell::State::Selector);
}
