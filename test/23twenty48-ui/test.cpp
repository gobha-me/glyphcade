// 2048's GEOMETRY, RENDERING, INPUT and the SOUND that input asks for, through a
// real Shell into an offscreen Screen. The rules and the tween live in
// test/22twenty48 and are not re-tested here.
//
// ⚠ test_run_frames installs a FallbackDriver, whose capabilities() reports
// all-false — so the Shell syncs to BorderStyle::Ascii and EVERY case in this file
// exercises the bottom tier. That is the tier AGENTS.md promises always works, the
// only tier CI can reach, and the tier where colour does not exist. For 2048 that
// matters more than it sounds: the whole colour ramp is invisible here, so what
// these cases prove is that the board is playable WITHOUT it.
//
// ⚠ NEVER hold a Screen& across a step(). App::test_run_frames reassigns the
// Screen on every call, so a reference taken before a frame dangles after it — as
// a segfault mid-suite, not a wrong value. Every helper takes the Probe and
// fetches the Screen itself. A Layout& from the game is fine: that lives in the
// Game, which the Shell keeps alive for the whole entry.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <termforge/core/types.hpp>

#include <glyphcade/arcade/registry.hpp>
#include <glyphcade/arcade/shell.hpp>
#include <glyphcade/games/twenty48/glyphs.hpp>
#include <glyphcade/games/twenty48/twenty48.hpp>

namespace {

using glyphcade::Shell;
using glyphcade::Twenty48;
using namespace glyphcade::twenty48;

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

[[nodiscard]] auto twenty48_index() -> int {
  const auto games = glyphcade::all_games();
  for (std::size_t i = 0; i < games.size(); ++i) {
    if (games[i].meta.slug == "2048") {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Enter 2048 from the selector, whatever position it holds in the roster.
auto enter_2048(Probe& app, int cols = 80, int rows = 24) -> void {
  app.step(1, cols, rows);
  const int index = twenty48_index();
  REQUIRE(index >= 0);
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);
  app.step(1, cols, rows);
}

// The same shape test/15minesweeper-ui uses. current_game() is const, and a
// fixture needs Twenty48::load() — so the const_cast is deliberate and local to
// the test, not a hole in the Shell's interface.
[[nodiscard]] auto game_of(Shell& shell) -> Twenty48* {
  return dynamic_cast<Twenty48*>(
      const_cast<glyphcade::Game*>(static_cast<const glyphcade::Game*>(
          shell.current_game())));
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

}  // namespace

// ── Geometry ─────────────────────────────────────────────────────────────────

TEST_CASE("cell_at is the exact inverse of tile_x/tile_y", "[2048][layout]") {
  // ⚠ The reason layout.hpp exists as its own file: draw() and any hit-testing
  // consume ONE Layout, so a coordinate derived twice cannot disagree. This
  // round-trips every cell of every column and row of every tile, at several
  // sizes — an off-by-one in the gap arithmetic shows up as a click landing on the
  // neighbouring tile, which is invisible until someone plays it.
  for (const auto& [cols, rows] :
       std::vector<std::pair<int, int>>{{29, 19}, {40, 22}, {80, 24}, {120, 40}}) {
    const auto l = compute_layout(cols, rows);
    REQUIRE(l.fits);

    for (int r = 0; r < kSize; ++r) {
      for (int c = 0; c < kSize; ++c) {
        for (int dy = 0; dy < kTileRows; ++dy) {
          for (int dx = 0; dx < kTileCols; ++dx) {
            const auto hit = l.cell_at(l.tile_x(c) + dx, l.tile_y(r) + dy);
            REQUIRE(hit.has_value());
            REQUIRE(*hit == Coord{r, c});
          }
        }
      }
    }
  }
}

TEST_CASE("the gaps between tiles are not part of any tile", "[2048][layout]") {
  // Without the two modulo checks in cell_at, a gap column reports the tile to its
  // left and a click between two tiles acts on one of them.
  const auto l = compute_layout(80, 24);
  REQUIRE(l.fits);

  for (int r = 0; r < kSize; ++r) {
    for (int c = 0; c + 1 < kSize; ++c) {
      // The gap column immediately right of tile (r,c).
      const int gx = l.tile_x(c) + kTileCols;
      for (int dy = 0; dy < kTileRows; ++dy) {
        REQUIRE_FALSE(l.cell_at(gx, l.tile_y(r) + dy).has_value());
      }
    }
  }
  for (int r = 0; r + 1 < kSize; ++r) {
    const int gy = l.tile_y(r) + kTileRows;
    for (int c = 0; c < kSize; ++c) {
      for (int dx = 0; dx < kTileCols; ++dx) {
        REQUIRE_FALSE(l.cell_at(l.tile_x(c) + dx, gy).has_value());
      }
    }
  }
}

TEST_CASE("a fractional position lands on the same column as its integer",
          "[2048][layout]") {
  // What keeps a finished animation landing exactly on the resting grid: the
  // double overload must agree with the int one at whole numbers, or every tile
  // sits one column off for the frame after the tween completes.
  const auto l = compute_layout(80, 24);
  for (int c = 0; c < kSize; ++c) {
    REQUIRE(l.tile_x(static_cast<double>(c)) == l.tile_x(c));
  }
  for (int r = 0; r < kSize; ++r) {
    REQUIRE(l.tile_y(static_cast<double>(r)) == l.tile_y(r));
  }
}

TEST_CASE("the board does not claim to fit below its needed size",
          "[2048][layout]") {
  REQUIRE(needed_cols() == 29);
  REQUIRE(needed_rows() == 19);
  REQUIRE(compute_layout(needed_cols(), needed_rows()).fits);
  REQUIRE_FALSE(compute_layout(needed_cols() - 1, needed_rows()).fits);
  REQUIRE_FALSE(compute_layout(needed_cols(), needed_rows() - 1).fits);

  // ⚠ Even when it does not fit, the status and hint rows are still placed, so
  // the too-small screen can say so. Returning a zeroed Layout there would put
  // both messages on row 0 on top of each other.
  const auto small = compute_layout(24, 10);
  REQUIRE_FALSE(small.fits);
  REQUIRE(small.hint_y == 9);
}

// ── Rendering at the bottom tier ─────────────────────────────────────────────

TEST_CASE("the whole 2048 screen is 7-bit at the ASCII tier",
          "[2048][render][ascii]") {
  // The only case that catches a Unicode lattice glyph reaching a terminal that
  // told us it cannot draw one — the same failure mode term-game#17 found on a real
  // pty for the selector's marker.
  Probe app;
  enter_2048(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  std::vector<int> cells{2,   4,    8,    16,  32,     64, 128, 256,
                         512, 1024, 2048, 4096, 131072, 2,  0,   0};
  g->load(cells, 12345);
  app.step();

  REQUIRE(all_seven_bit(app));
}

TEST_CASE("every tile's number is drawn, including the widest one",
          "[2048][render]") {
  Probe app;
  enter_2048(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  // 131072 is kMaxTile: six digits in a six-column tile, nothing to spare. If the
  // label were ever one column wider it would be clipped mid-number, which reads
  // as a scoring bug rather than a layout one.
  std::vector<int> cells(kCells, 0);
  cells[0] = 131072;
  cells[5] = 2048;
  g->load(cells, 0);
  app.step();

  const auto& l = g->layout();
  REQUIRE(l.fits);
  const std::string r0 = row_text(app, l.tile_y(0) + kTileRows / 2);
  REQUIRE(r0.find("131072") != std::string::npos);
  const std::string r1 = row_text(app, l.tile_y(1) + kTileRows / 2);
  REQUIRE(r1.find("2048") != std::string::npos);
}

TEST_CASE("the lattice makes the grid visible with no colour at all",
          "[2048][render][ascii]") {
  // ⚠ THE bottom-tier case. At the colour tier an empty cell is a filled block, so
  // the grid is legible from the fills. FallbackDriver discards colour, so without
  // rule glyphs in the gaps an empty board is an empty rectangle and the player
  // cannot see where the tiles go.
  Probe app;
  enter_2048(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);
  g->load(std::vector<int>(kCells, 0), 0);
  app.step();

  const auto& l = g->layout();
  // A vertical rule sits in the gap column, on a tile's middle row.
  const int gx = l.tile_x(0) + kTileCols;
  REQUIRE(app.screen().text_at(gx, l.tile_y(0) + kTileRows / 2) ==
          kAsciiLattice.vertical);
  // A horizontal rule sits in the gap row, and a cross where the two meet.
  const int gy = l.tile_y(0) + kTileRows;
  REQUIRE(app.screen().text_at(l.tile_x(0), gy) == kAsciiLattice.horizontal);
  REQUIRE(app.screen().text_at(gx, gy) == kAsciiLattice.cross);
}

TEST_CASE("the outcome is stated in words, not in colour",
          "[2048][render][ascii]") {
  // At this tier "you won" cannot be said by painting a tile amber. Same reason
  // minesweeper writes YOU WIN and BOOM.
  Probe app;
  enter_2048(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  app.step();
  REQUIRE(row_text(app, 0).find("PLAYING") != std::string::npos);

  std::vector<int> win(kCells, 0);
  win[0] = 1024;
  win[1] = 1024;
  g->load(win, 0);
  g->apply(Dir::Left);
  app.step();
  REQUIRE(row_text(app, 0).find("2048 REACHED") != std::string::npos);
}

TEST_CASE("the status counters never overwrite the outcome word",
          "[2048][render]") {
  // ⚠ A REAL BUG this case exists for, found in a headless render rather than
  // reasoned about: write_text clips at the screen edge but NOT against text
  // already on the row, so drawing the counters first produced `movesPLAYING` at
  // 40 columns. The outcome word is the one thing on this row that must survive —
  // at this tier it is the only carrier of win and loss — so it is drawn first and
  // the counters are fitted into what is left.
  for (const int cols : {29, 34, 40, 52, 80, 120}) {
    Probe app;
    enter_2048(app, cols, 24);
    auto* g = game_of(app);
    REQUIRE(g != nullptr);
    std::vector<int> cells(kCells, 0);
    cells[0] = 131072;  // the widest possible counters
    g->load(cells, 999999);
    // ⚠ A move is needed to populate `record`, and it has to be a NO-OP one. The
    // single tile sits at (0,0), so Left cannot slide or merge it: the board,
    // the score and the move count all stay exactly as loaded — which the other
    // three expectations below depend on — while apply() still runs
    // record_best() and puts 999999 in the store. Any other direction spawns a
    // tile and turns "moves 0" into "moves 1".
    app.dispatch_event(key(termforge::Key::Left));
    app.step(1, cols, 24);

    const std::string status = row_text(app, 0);
    REQUIRE(status.find("PLAYING") != std::string::npos);

    // ⚠ EVERY field that appears must appear in full, not just the first one.
    //
    // An earlier version checked only "score", and mutation testing showed that
    // was not enough: with the reserved gap removed, the counters ran into the
    // word and — because the word is drawn last and wins — "moves 0" was left
    // rendered as "moves" with its digits eaten, while "score 999999" and
    // "PLAYING" both survived intact. The case passed on a visibly broken row.
    //
    // A half-written counter is worse than a missing one: a missing field reads
    // as a narrow terminal, a truncated number reads as a wrong score. So the
    // contract is whole-fields-or-nothing, and this is what holds the budget
    // arithmetic to it.
    //
    // ⚠ "record" is in this list, not just the original three. A fourth field
    // whose width nothing checks is a fourth field that can silently run into
    // the word — and it is the one most likely to, being last in the priority
    // order and therefore the one the budget cuts closest.
    for (const auto& [label, full] :
         std::vector<std::pair<std::string, std::string>>{
             {"score", "score 999999"},
             {"best", "best 131072"},
             {"moves", "moves 0"},
             {"record", "record 999999"}}) {
      if (status.find(label) != std::string::npos) {
        REQUIRE(status.find(full) != std::string::npos);
      }
    }
  }
}

TEST_CASE("a screen too small for the board says so", "[2048][render]") {
  Probe app;
  enter_2048(app, 28, 18);  // one short in both directions
  auto* g = game_of(app);
  REQUIRE(g != nullptr);
  app.step(1, 28, 18);

  REQUIRE_FALSE(g->layout().fits);
  bool found = false;
  for (int y = 0; y < 18; ++y) {
    if (row_text(app, y).find("29x19") != std::string::npos) {
      found = true;
    }
  }
  REQUIRE(found);
}

// ── Input ────────────────────────────────────────────────────────────────────

TEST_CASE("arrows, hjkl and wasd all move the board", "[2048][input]") {
  for (const auto& ev : std::vector<termforge::Event>{
           key(termforge::Key::Left), ch('h'), ch('a')}) {
    Probe app;
    enter_2048(app);
    auto* g = game_of(app);
    REQUIRE(g != nullptr);
    std::vector<int> cells(kCells, 0);
    cells[3] = 2;  // (0,3), so Left is always legal
    g->load(cells, 0);

    app.dispatch_event(ev);
    REQUIRE(g->board().moves() == 1);
  }
}

TEST_CASE("undo is bound and restores the previous board", "[2048][input]") {
  Probe app;
  enter_2048(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);
  std::vector<int> cells(kCells, 0);
  cells[2] = 2;
  cells[3] = 2;
  g->load(cells, 0);

  app.dispatch_event(key(termforge::Key::Left));
  REQUIRE(g->board().moves() == 1);
  REQUIRE(g->board().can_undo());

  app.dispatch_event(ch('u'));
  REQUIRE(g->board().moves() == 0);
  REQUIRE(g->board().at(Coord{0, 2}) == 2);
  REQUIRE(g->board().at(Coord{0, 3}) == 2);
}

TEST_CASE("the game never consumes Escape or p", "[2048][input]") {
  // ⚠ Both absences are load-bearing: Escape is the Shell's quit-to-menu and 'p'
  // is its pause. A game that swallowed either would strand the player inside it.
  // Asserted through the Shell, which is the only place the difference shows.
  Probe app;
  enter_2048(app);
  REQUIRE(app.state() == Shell::State::InGame);

  app.dispatch_event(ch('p'));
  REQUIRE(app.state() == Shell::State::Paused);

  app.dispatch_event(key(termforge::Key::Escape));  // dismiss the pause dialog
  app.step();
  REQUIRE(app.state() == Shell::State::InGame);

  app.dispatch_event(key(termforge::Key::Escape));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
}

TEST_CASE("q returns to the menu", "[2048][input]") {
  Probe app;
  enter_2048(app);
  app.dispatch_event(ch('q'));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);
}

// ── Sound ────────────────────────────────────────────────────────────────────
//
// Asserted through Shell::audio().play_count(), which counts INTENT rather than
// sound — so these pass identically on the GLYPHCADE_WITH_AUDIO=OFF arm CI runs,
// and nothing here touches a device or the disk.

TEST_CASE("a move that only slides plays Slide, not Merge", "[2048][audio]") {
  using glyphcade::audio::SfxId;

  Probe app;
  enter_2048(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);
  std::vector<int> cells(kCells, 0);
  cells[3] = 2;  // one tile, nothing to merge with
  g->load(cells, 0);

  const auto slide_before = app.audio().play_count(SfxId::Slide);
  const auto merge_before = app.audio().play_count(SfxId::Merge);

  app.dispatch_event(key(termforge::Key::Left));

  REQUIRE(app.audio().play_count(SfxId::Slide) == slide_before + 1);
  REQUIRE(app.audio().play_count(SfxId::Merge) == merge_before);
}

TEST_CASE("a move that merges plays Merge instead of Slide", "[2048][audio]") {
  // One gesture, one sound — the same principle the selector's click case pins.
  // Merge REPLACES Slide rather than stacking on it.
  using glyphcade::audio::SfxId;

  Probe app;
  enter_2048(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);
  std::vector<int> cells(kCells, 0);
  cells[2] = 2;
  cells[3] = 2;
  g->load(cells, 0);

  const auto slide_before = app.audio().play_count(SfxId::Slide);
  const auto merge_before = app.audio().play_count(SfxId::Merge);

  app.dispatch_event(key(termforge::Key::Left));

  REQUIRE(app.audio().play_count(SfxId::Merge) == merge_before + 1);
  REQUIRE(app.audio().play_count(SfxId::Slide) == slide_before);
}

TEST_CASE("a direction that changes nothing is silent", "[2048][audio]") {
  // ⚠ The case that makes announce()'s move-count comparison load-bearing. There
  // is no deny blip in the bank and inventing one is a feel decision nobody who
  // cannot hear it should make — so a rejected key must produce NO sound at all,
  // not a quieter one.
  using glyphcade::audio::SfxId;

  Probe app;
  enter_2048(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);
  std::vector<int> cells(kCells, 0);
  cells[0] = 2;  // already packed left
  g->load(cells, 0);

  const auto slide_before = app.audio().play_count(SfxId::Slide);
  const auto merge_before = app.audio().play_count(SfxId::Merge);

  app.dispatch_event(key(termforge::Key::Left));

  REQUIRE(g->board().moves() == 0);
  REQUIRE(app.audio().play_count(SfxId::Slide) == slide_before);
  REQUIRE(app.audio().play_count(SfxId::Merge) == merge_before);
}

TEST_CASE("winning plays Win once, and not the merge that caused it",
          "[2048][audio]") {
  using glyphcade::audio::SfxId;

  Probe app;
  enter_2048(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);
  std::vector<int> cells(kCells, 0);
  cells[0] = 1024;
  cells[1] = 1024;
  g->load(cells, 0);

  const auto win_before = app.audio().play_count(SfxId::Win);
  const auto merge_before = app.audio().play_count(SfxId::Merge);

  app.dispatch_event(key(termforge::Key::Left));

  REQUIRE(g->board().state() == State::Won);
  REQUIRE(app.audio().play_count(SfxId::Win) == win_before + 1);
  REQUIRE(app.audio().play_count(SfxId::Merge) == merge_before);

  // And the latch means it fires ONCE, not on every subsequent move.
  app.dispatch_event(key(termforge::Key::Down));
  REQUIRE(app.audio().play_count(SfxId::Win) == win_before + 1);
}

// ── Best score (term-game#14) ────────────────────────────────────────────────
//
// The store's own format and merge live in test/24scores. What is asserted here
// is the two properties that are 2048's rather than the store's: that the record
// belongs to the SHELL and not to the Game, and that recording after every move
// is safe in the presence of undo.

TEST_CASE("a best score outlives the game that set it", "[2048][scores]") {
  // ⚠ A fresh Twenty48 is built per menu entry, so a best score held in the Game
  // would reset on every quit-to-menu — the reason term-game#14 exists at all.
  // Move the store into the Game and this is the case that goes red.
  Probe app;
  enter_2048(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  std::vector<int> cells(kCells, 0);
  cells[0] = 2;
  cells[1] = 2;
  g->load(cells, 0);
  app.dispatch_event(key(termforge::Key::Left));  // merges: 2+2 -> 4, score 4
  app.step();
  REQUIRE(g->board().score() == 4);
  REQUIRE(app.scores().get("2048", "best_score") == 4);
  REQUIRE(row_text(app, 0).find("record 4") != std::string::npos);

  app.dispatch_event(key(termforge::Key::Escape));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);

  enter_2048(app);
  auto* second = game_of(app);
  REQUIRE(second != nullptr);
  // Freshness through state rather than pointer identity: the allocator reuses
  // the address of the game it just destroyed more often than not.
  REQUIRE(second->board().score() == 0);
  REQUIRE(row_text(app, 0).find("record 4") != std::string::npos);
  // The live score is the new game's; only the record carries over.
  REQUIRE(row_text(app, 0).find("score 0") != std::string::npos);
}

TEST_CASE("undo lowers the score and does not lower the record",
          "[2048][scores]") {
  // ⚠ THE CASE THAT LICENSES RECORDING ON EVERY MOVE. Twenty48::apply() calls
  // record_best() unconditionally, with no end-of-run hook and no guard in the
  // undo path — which is only correct because Store::record() is monotone. Make
  // record() assign unconditionally instead of comparing and this goes red while
  // every other 2048 case stays green.
  Probe app;
  enter_2048(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  std::vector<int> cells(kCells, 0);
  cells[0] = 2;
  cells[1] = 2;
  g->load(cells, 0);
  app.dispatch_event(key(termforge::Key::Left));
  app.step();
  const int high = g->board().score();
  REQUIRE(high == 4);
  REQUIRE(app.scores().get("2048", "best_score") == high);

  app.dispatch_event(ch(U'u'));  // undo
  app.step();
  REQUIRE(g->board().score() == 0);  // the live score really did go down...
  REQUIRE(app.scores().get("2048", "best_score") == high);  // ...the record did not
  REQUIRE(row_text(app, 0).find("record 4") != std::string::npos);
}

TEST_CASE("2048 records the highest tile as well as the score", "[2048][scores]") {
  // best_tile is persisted and deliberately NOT displayed — the row has no width
  // for a fifth field and "best" there is already the LIVE maximum tile. It is
  // kept because two keys under one slug is the concrete form of "a record is not
  // one integer", which is the whole reason #14 waited for a second game.
  Probe app;
  enter_2048(app);
  auto* g = game_of(app);
  REQUIRE(g != nullptr);

  std::vector<int> cells(kCells, 0);
  cells[0] = 512;
  cells[1] = 512;
  g->load(cells, 0);
  app.dispatch_event(key(termforge::Key::Left));
  app.step();

  REQUIRE(app.scores().get("2048", "best_tile") == 1024);
  REQUIRE(app.scores().get("2048", "best_score") == 1024);
}
