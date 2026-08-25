// Sokoban through a real Shell: geometry, MapWidget rendering, the camera,
// input routing, sound intent and scores.
//
// ⚠ The three traps test/26snake-ui and test/28tetris-ui carry, unchanged:
//
//   1. test_run_frames installs a FallbackDriver, whose capabilities() report
//      all-false — so the Shell syncs to BorderStyle::Ascii and EVERY case in
//      this file exercises the bottom tier. The Unicode tile table is only
//      reachable here through glyphs.hpp directly, which is where it is
//      asserted.
//   2. NEVER hold a Screen& across a step(). App::test_run_frames reassigns the
//      Screen every call, so a reference taken before a frame dangles after it.
//   3. NEVER read the Game* after dispatching a key that ends the game.
//
// ⚠ And one that is this game's own. Sokoban is the first game whose board size
// is not a compile-time constant — there are twenty of them — and the first
// whose start() depends on the SCORE STORE, because it resumes at the first
// unsolved level. A case that assumes it is looking at level 1 after recording
// a win on level 1 is looking at level 2.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include <termforge/core/types.hpp>

#include <glyphcade/arcade/registry.hpp>
#include <glyphcade/arcade/shell.hpp>
#include <glyphcade/games/sokoban/glyphs.hpp>
#include <glyphcade/games/sokoban/sokoban.hpp>

namespace {

using glyphcade::Shell;
using glyphcade::Sokoban;
using namespace glyphcade::sokoban;

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
[[nodiscard]] auto click(int x, int y) -> termforge::Event {
  return termforge::Event{
      termforge::MouseEvent{.x = x, .y = y, .button = 0, .pressed = true}};
}

[[nodiscard]] auto sokoban_index() -> int {
  const auto games = glyphcade::all_games();
  for (std::size_t i = 0; i < games.size(); ++i) {
    if (games[i].meta.slug == "sokoban") return static_cast<int>(i);
  }
  return -1;
}

auto enter_sokoban(Probe& app, int cols = 80, int rows = 24) -> void {
  app.step(1, cols, rows);
  const int index = sokoban_index();
  REQUIRE(index >= 0);
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);
  // ⚠ A second Enter, AFTER the REQUIRE (which is what proves the Shell entered
  // on the FIRST one). term-game#38: entering Sokoban now opens its twenty-level
  // picker, so a suite that wants a BOARD has to say so.
  app.dispatch_event(key(termforge::Key::Enter));
  app.step(1, cols, rows);
}

[[nodiscard]] auto game_of(Shell& shell) -> Sokoban* {
  return dynamic_cast<Sokoban*>(const_cast<glyphcade::Game*>(
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

[[nodiscard]] auto screen_text(Probe& app) -> std::string {
  std::string out;
  for (int y = 0; y < app.screen().rows(); ++y) out += row_text(app, y) + "\n";
  return out;
}

[[nodiscard]] auto all_seven_bit(Probe& app) -> bool {
  auto& s = app.screen();
  for (int y = 0; y < s.rows(); ++y) {
    for (int x = 0; x < s.cols(); ++x) {
      for (const char c : s.text_at(x, y)) {
        if (static_cast<unsigned char>(c) >= 0x80) return false;
      }
    }
  }
  return true;
}

// The two columns of one map tile, as the screen holds them. The tile's screen
// position is the widget's rect plus (tile - camera) * tile size. This helper
// reads painted tiles; runtime click mapping belongs to MapWidget::tile_at().
[[nodiscard]] auto tile_text(Probe& app, const Sokoban& game, int tx, int ty)
    -> std::string {
  const auto& lay = game.layout();
  const auto [cam_x, cam_y] = game.map().camera();
  const int x0 = lay.view_x + ((tx - cam_x) * kTileCols);
  const int y0 = lay.view_y + ((ty - cam_y) * kTileRows);
  std::string out;
  for (int i = 0; i < kTileCols; ++i) {
    const auto t = app.screen().text_at(x0 + i, y0);
    out += t.empty() ? " " : t;
  }
  return out;
}

}  // namespace

// ── Geometry ─────────────────────────────────────────────────────────────────

TEST_CASE("the floor is 34x12 and it is about playability, not drawing",
          "[sokoban][layout]") {
  REQUIRE(kNeedCols == 34);
  REQUIRE(kNeedRows == 12);

  REQUIRE(compute_layout(kNeedCols, kNeedRows, 8, 5).fits);
  REQUIRE_FALSE(compute_layout(kNeedCols - 1, kNeedRows, 8, 5).fits);
  REQUIRE_FALSE(compute_layout(kNeedCols, kNeedRows - 1, 8, 5).fits);
  // The Shell's own floor, which the selector will happily launch us on.
  REQUIRE_FALSE(compute_layout(20, 8, 8, 5).fits);

  // ⚠ The floor does NOT depend on the level. Every other game in the suite
  // derives its minimum from its board; this one cannot, because it has twenty
  // boards and a camera. The largest bundled level (12x11) and the smallest
  // (8x5) fit at exactly the same terminal size.
  REQUIRE(compute_layout(kNeedCols, kNeedRows, 12, 11).fits);
  REQUIRE(compute_layout(kNeedCols, kNeedRows, 200, 200).fits);
}

TEST_CASE("a level smaller than the window is centred, not jammed into a corner",
          "[sokoban][layout]") {
  // ⚠ Found on a real pty, and it is the case that ALWAYS happens: every
  // bundled level is smaller than a normal terminal, MapWidget's camera clamps
  // to the map, so without shrinking the widget's rect the level draws hard
  // against the top-left of an 80x24 frame with a lake of empty space beside it.
  const auto l = compute_layout(80, 24, 8, 5);
  REQUIRE(l.fits);
  REQUIRE(l.view_w == 8 * kTileCols);
  REQUIRE(l.view_h == 5 * kTileRows);
  REQUIRE(l.view_x > 1);
  REQUIRE(l.view_y > 1);
  // Centred: the space either side differs by at most one column.
  const int left = l.view_x - 1;
  const int right = (80 - kChromeCols) - l.view_w - left;
  REQUIRE(left - right <= 1);
  REQUIRE(right - left <= 1);
}

TEST_CASE("a level larger than the window fills it and the rest scrolls",
          "[sokoban][layout]") {
  // 40x30 tiles into a 34x12 terminal: the view is the whole interior, and the
  // window in tiles is the FLOORED quotient — a trailing partial tile is not
  // drawn, which is MapWidget's documented edge behaviour.
  const auto l = compute_layout(kNeedCols, kNeedRows, 40, 30);
  REQUIRE(l.view_w == kNeedCols - kChromeCols);
  REQUIRE(l.view_h == kNeedRows - kChromeRows);
  REQUIRE(l.view_tiles_w() == 16);
  REQUIRE(l.view_tiles_h() == 8);

  // An odd interior width loses its last column to the floor, not to half a
  // tile.
  const auto odd = compute_layout(kNeedCols + 1, kNeedRows, 40, 30);
  REQUIRE(odd.view_w == kNeedCols + 1 - kChromeCols);
  REQUIRE(odd.view_tiles_w() == 16);
}

// ── Rendering ────────────────────────────────────────────────────────────────

TEST_CASE("the map renders as tiles at the ASCII tier", "[sokoban][render]") {
  Probe app;
  enter_sokoban(app);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);
  REQUIRE(game->load_error().empty());
  game->load(0);
  app.step();

  const auto* b = game->board();
  REQUIRE(b != nullptr);
  const auto& lv = b->level();

  // The corner of every bundled level is wall, and the player is where the
  // parser put them.
  REQUIRE(tile_text(app, *game, 0, 0) == kAsciiTiles.wall);
  REQUIRE(tile_text(app, *game, b->player().x, b->player().y) ==
          kAsciiTiles.player);

  // A crate, a goal and a piece of floor, each as its own glyph.
  const Pos box = b->boxes().front();
  REQUIRE(tile_text(app, *game, box.x, box.y) ==
          (lv.is_goal(box.x, box.y) ? kAsciiTiles.box_on_goal
                                    : kAsciiTiles.box));
}

TEST_CASE("seating a crate changes its glyph, and so does standing on a goal",
          "[sokoban][render]") {
  // ⚠ This is the reference's invisible-player-on-goal defect, asserted rather
  // than asserted-about. There, `.cell.target::after` and `.cell.player::after`
  // decorate the same pseudo-element with equal specificity, so the goal marker
  // silently vanishes under the player.
  Probe app;
  enter_sokoban(app);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);
  game->load(0);  // First Push: @$..goal, three steps right
  app.step();

  auto* b = game->board();
  REQUIRE(b != nullptr);

  for (int i = 0; i < 3; ++i) app.dispatch_event(key(termforge::Key::Right));
  app.step();
  REQUIRE(b->won());

  const Pos seated = b->boxes().front();
  REQUIRE(tile_text(app, *game, seated.x, seated.y) ==
          kAsciiTiles.box_on_goal);
  REQUIRE(tile_text(app, *game, seated.x, seated.y) != kAsciiTiles.box);
}

TEST_CASE("a cornered crate is marked on the overlay layer",
          "[sokoban][render]") {
  Probe app;
  enter_sokoban(app);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);
  // Level 2 (Two Steps): one crate, plenty of room, and a top-left corner to
  // push it into.
  game->load(1);
  app.step();

  auto* b = game->board();
  REQUIRE(b != nullptr);
  REQUIRE_FALSE(b->deadlocked());

  // Walk round and shove the crate into the left wall, then up into the corner.
  app.dispatch_event(ch(U'j'));  // down, alongside the crate
  app.dispatch_event(ch(U'l'));
  app.dispatch_event(ch(U'j'));
  app.dispatch_event(ch(U'h'));
  app.dispatch_event(ch(U'h'));
  app.step();

  if (b->deadlocked()) {
    const Pos box = b->boxes().front();
    REQUIRE(tile_text(app, *game, box.x, box.y) == kAsciiTiles.dead_box);
    REQUIRE(row_text(app, game->layout().hint_y).find("Stuck") !=
            std::string::npos);
  }
}

TEST_CASE("a crate frozen ON its goal is drawn seated, not marked stuck",
          "[sokoban][render]") {
  // ⚠ MUTATION FINDING, and the rendering twin of the model rule two files
  // over. Dropping the goal test from the overlay in sync_map() survived the
  // whole suite, because no case had ever rendered a crate that was BOTH frozen
  // and finished — and a corner goal is exactly where a Sokoban player is most
  // often trying to put one.
  //
  // Level 18 is "Four Corners": its four goals are the four corners of the
  // room, so a seated crate there is immovable by construction.
  Probe app;
  enter_sokoban(app);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);
  game->load(17);
  app.step();

  auto* b = game->board();
  REQUIRE(b != nullptr);
  REQUIRE(b->level().is_goal(1, 1));

  // Drive the model straight there: this case is about what the RENDERER does
  // with the position, not about solving Four Corners in a unit test. The
  // sequence is spelled out rather than looped — "push up until you cannot"
  // walks the player into the wall bar without touching a crate.
  for (const Dir d : {Dir::Up, Dir::Left, Dir::Left, Dir::Down, Dir::Left,
                      Dir::Up, Dir::Up}) {
    REQUIRE(b->step(d).moved);
  }
  REQUIRE(b->has_box(1, 1));
  REQUIRE(b->is_frozen(Pos{1, 1}));
  app.step();

  REQUIRE(tile_text(app, *game, 1, 1) == kAsciiTiles.box_on_goal);
  REQUIRE(tile_text(app, *game, 1, 1) != kAsciiTiles.dead_box);
  // ...and the hint row must not be telling the player they are stuck.
  REQUIRE(row_text(app, game->layout().hint_y).find("Stuck") ==
          std::string::npos);
}

TEST_CASE("the whole screen is 7-bit at the ASCII tier", "[sokoban][render]") {
  // ⚠ Every field of kMeta, the twenty level names, the status budgets and the
  // hint budgets all reach this screen. 2048 shipped an em dash in a
  // description that no rendering test caught, because the offending character
  // fell outside the viewport the test happened to use — so this sweeps several
  // widths, not one.
  for (const int cols : {34, 48, 60, 80, 120}) {
    Probe app;
    enter_sokoban(app, cols, 24);
    auto* game = game_of(app);
    REQUIRE(game != nullptr);
    for (int i = 0; i < 4; ++i) {
      game->load(i * 6);
      app.step(1, cols, 24);
      INFO("cols=" << cols << " level=" << (i * 6));
      REQUIRE(all_seven_bit(app));
    }
  }
}

TEST_CASE("the status and hint rows survive a terminal below the floor",
          "[sokoban][render]") {
  // ⚠ THE mutation that went green in two consecutive epics. Both rows are
  // drawn whether or not the map fits, so every width budget has to hold at
  // widths NARROWER than kNeedCols — which is precisely where a test that only
  // ever renders a playable board never looks.
  for (const int cols : {20, 24, 29, 30, 33}) {
    Probe app;
    enter_sokoban(app, cols, 10);
    INFO("cols=" << cols);
    REQUIRE(all_seven_bit(app));
    // The "does not fit" screen names what it needs.
    REQUIRE(screen_text(app).find("Sokoban needs 34x12") != std::string::npos);
  }
}

TEST_CASE("every status budget fits its own width", "[sokoban][render]") {
  for (int cols = 20; cols <= 120; ++cols) {
    Probe app;
    enter_sokoban(app, cols, 24);
    auto* game = game_of(app);
    REQUIRE(game != nullptr);
    game->load(19);  // the longest name in the pack
    app.step(1, cols, 24);

    const auto& lay = game->layout();
    const int y = lay.fits ? lay.status_y : 0;
    const std::string row = row_text(app, y);
    INFO("cols=" << cols << " row=[" << row << "]");
    // Nothing was written into the last column and lost: the row is exactly as
    // wide as the screen, and the rightmost cell of a clipped string would be
    // the tail of a number.
    REQUIRE(static_cast<int>(row.size()) >= cols);
    // ⚠ The narrowest budget drops the word "Lv" on purpose — below 30 columns
    // there is no room for labels and the row is bare numbers. What must hold
    // at EVERY width is that the level ordinal is still on screen, because that
    // is the one number a player cannot reconstruct from anything else.
    REQUIRE(row.find("/20") != std::string::npos);
    if (cols >= 30) REQUIRE(row.find("Lv") != std::string::npos);
  }
}

// ── The camera ───────────────────────────────────────────────────────────────

TEST_CASE("the camera follows the player and clamps at the map edge",
          "[sokoban][camera]") {
  // ⚠ The bundled pack does NOT exercise this: the largest level is 12x11 tiles
  // and fits any normal terminal with room to spare, so in ordinary play the
  // camera pins to 0,0 and never moves. A viewport smaller than the level is
  // the only way to reach it from here, which is exactly what term-game#8 means by
  // "the case that makes MapWidget's coordinate model earn its keep".
  Probe app;
  enter_sokoban(app, 34, 12);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);
  game->load(11);  // The Courtyard: 12x11, taller than the 8-tile window
  app.step(1, 34, 12);

  const auto& lay = game->layout();
  // ⚠ 12 tiles wide, not the window's 16: the widget's rect shrinks to the
  // LEVEL when the level is narrower, which is what stops a 12-wide map drawing
  // against the left edge of a 16-wide window. The height is the other way
  // round — 11 tiles of level into an 8-tile window — which is the case the
  // camera exists for.
  REQUIRE(lay.view_tiles_w() == 12);
  REQUIRE(lay.view_tiles_h() == 8);

  auto* b = game->board();
  REQUIRE(b != nullptr);
  const int map_h = b->level().h;
  REQUIRE(map_h > lay.view_tiles_h());

  // Wide enough to hold the whole map horizontally, so the camera pins to 0
  // there and moves only vertically. Clamped, never negative.
  const auto [cx0, cy0] = game->map().camera();
  REQUIRE(cx0 == 0);
  REQUIRE(cy0 >= 0);
  REQUIRE(cy0 <= map_h - lay.view_tiles_h());

  // ⚠ Asserted against the CONTRACT, not against a hand-computed number for one
  // level's topology. The first draft drove twenty Up presses and demanded the
  // camera reach 0 — and The Courtyard has a wall directly above its start
  // square, so the player never moved and the camera was right to stay put. The
  // claim worth pinning is that the view is centred on the player and clamped
  // to the map, whatever the level does.
  const auto expect_centred = [&]() {
    const Pos p = game->board()->player();
    const int win = game->layout().view_tiles_h();
    const int want =
        std::clamp(p.y - (win / 2), 0, std::max(0, map_h - win));
    INFO("player y=" << p.y << " window=" << win);
    REQUIRE(game->map().camera().second == want);
    REQUIRE(game->map().camera().first == 0);  // the map fits horizontally
  };
  expect_centred();

  // Walk left along the bottom row, then up the open left-hand column, checking
  // after every step. Somewhere in there the camera hits its top clamp.
  bool reached_top = false;
  for (int i = 0; i < 6; ++i) {
    app.dispatch_event(key(termforge::Key::Left));
    app.step(1, 34, 12);
    expect_centred();
  }
  for (int i = 0; i < 10; ++i) {
    app.dispatch_event(key(termforge::Key::Up));
    app.step(1, 34, 12);
    expect_centred();
    if (game->map().camera().second == 0) reached_top = true;
  }
  REQUIRE(reached_top);
}

// ── Input ────────────────────────────────────────────────────────────────────

TEST_CASE("arrows, hjkl and wasd all push", "[sokoban][input]") {
  Probe app;
  enter_sokoban(app);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);
  game->load(0);
  app.step();
  auto* b = game->board();
  REQUIRE(b != nullptr);

  const Pos start = b->player();
  app.dispatch_event(key(termforge::Key::Right));
  REQUIRE(b->player().x == start.x + 1);
  app.dispatch_event(ch(U'h'));
  REQUIRE(b->player().x == start.x);
  app.dispatch_event(ch(U'd'));
  REQUIRE(b->player().x == start.x + 1);
  app.dispatch_event(ch(U'a'));
  REQUIRE(b->player().x == start.x);
}

TEST_CASE("u undoes and r resets, both still live after the level is solved",
          "[sokoban][input]") {
  Probe app;
  enter_sokoban(app);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);
  game->load(0);
  app.step();
  auto* b = game->board();
  REQUIRE(b != nullptr);

  for (int i = 0; i < 3; ++i) app.dispatch_event(key(termforge::Key::Right));
  REQUIRE(b->won());
  REQUIRE(b->moves() == 3);

  // ⚠ The reference disables both of these the moment the level is complete,
  // and only clears that flag on load — which is how its celebration overlay
  // can strand a player on a frozen board. Ours has no such flag.
  app.dispatch_event(ch(U'u'));
  REQUIRE_FALSE(b->won());
  REQUIRE(b->moves() == 2);

  app.dispatch_event(ch(U'r'));
  REQUIRE(b->moves() == 0);
  REQUIRE_FALSE(b->won());
}

TEST_CASE("[ and ] walk the pack and clamp at both ends",
          "[sokoban][input]") {
  Probe app;
  enter_sokoban(app);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);
  game->load(0);
  app.step();

  app.dispatch_event(ch(U']'));
  REQUIRE(game->index() == 1);
  app.dispatch_event(ch(U'['));
  REQUIRE(game->index() == 0);
  // ⚠ Clamped, not wrapped and not ignored. The reference returns early from
  // loadLevel for an out-of-range index, BEFORE its first render — which with a
  // stale persisted index leaves a permanently blank board and no message.
  app.dispatch_event(ch(U'['));
  REQUIRE(game->index() == 0);
  REQUIRE(game->board() != nullptr);

  for (int i = 0; i < level_count() + 5; ++i) app.dispatch_event(ch(U']'));
  REQUIRE(game->index() == level_count() - 1);
  REQUIRE(game->board() != nullptr);
}

TEST_CASE("changing level rebuilds the map rather than leaving the old one",
          "[sokoban][input]") {
  // ⚠ MapWidget's set_map_size() discards every layer despite a comment saying
  // it preserves the overlapping corner. Loading sizes first and populates
  // second for that reason; this is the case that fails if someone reorders it.
  Probe app;
  enter_sokoban(app);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);

  game->load(0);
  app.step();
  const std::string first = screen_text(app);

  game->load(5);
  app.step();
  const std::string second = screen_text(app);
  REQUIRE(first != second);
  // The new level's own walls are on screen, so the terrain layer was refilled.
  REQUIRE(tile_text(app, *game, 0, 0) == kAsciiTiles.wall);
  REQUIRE(second.find("Inner Chamber") != std::string::npos);
}

TEST_CASE("a click on an adjacent tile steps, and anywhere else does not",
          "[sokoban][input]") {
  Probe app;
  enter_sokoban(app);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);
  game->load(0);
  app.step();
  auto* b = game->board();
  REQUIRE(b != nullptr);

  const auto& lay = game->layout();
  const auto [cam_x, cam_y] = game->map().camera();
  const Pos p = b->player();
  const auto cell_of = [&](int tx, int ty) {
    return std::pair<int, int>{lay.view_x + ((tx - cam_x) * kTileCols),
                               lay.view_y + ((ty - cam_y) * kTileRows)};
  };

  const auto [rx, ry] = cell_of(p.x + 1, p.y);
  app.dispatch_event(click(rx, ry));
  REQUIRE(b->player().x == p.x + 1);

  // Two tiles away is not a direction, and Sokoban's only verb is a direction.
  // No pathfinding — that would be inventing a mechanic the reference does not
  // have.
  const Pos q = b->player();
  const auto [fx, fy] = cell_of(q.x + 3, q.y);
  app.dispatch_event(click(fx, fy));
  REQUIRE(b->player() == q);

  // ⚠ MUTATION FINDING. Every "too far" click above is horizontal, so relaxing
  // the VERTICAL comparison from `dy == -1` to `dy <= -1` — which turns any
  // click in the column above into a step — survived the suite. Each of the
  // four directions needs its own out-of-range click, not one representative.
  for (const auto& [dx, dy] : {std::pair{0, -2}, std::pair{0, 2},
                               std::pair{-3, 0}, std::pair{2, 2}}) {
    const auto [ox, oy] = cell_of(q.x + dx, q.y + dy);
    INFO("offset " << dx << "," << dy);
    app.dispatch_event(click(ox, oy));
    REQUIRE(b->player() == q);
  }

  // A click outside the widget's rect entirely.
  app.dispatch_event(click(0, 0));
  REQUIRE(b->player() == q);
}

TEST_CASE("the game declines p and Escape so the Shell keeps them",
          "[sokoban][input]") {
  // ⚠ Sokoban's level keys are '[' and ']' precisely so 'p' stays the Shell's.
  Probe app;
  enter_sokoban(app);
  REQUIRE(app.state() == Shell::State::InGame);

  app.dispatch_event(ch(U'p'));
  REQUIRE(app.state() == Shell::State::Paused);
  // ⚠ Deliberately not asserting how pause is LEFT. That is the pause dialog's
  // contract and test/11selector owns it; a second 'p' goes to the overlay, not
  // back through open_pause(). What this case is about is that Sokoban declined
  // the key in the first place.

  Probe other;
  enter_sokoban(other);
  other.dispatch_event(key(termforge::Key::Escape));
  other.step();
  REQUIRE(other.state() == Shell::State::Selector);
}

// ── Scores ───────────────────────────────────────────────────────────────────

TEST_CASE("solving a level records its own key, and only its own",
          "[sokoban][scores]") {
  Probe app;
  enter_sokoban(app);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);
  game->load(0);
  app.step();

  REQUIRE_FALSE(app.scores().get("sokoban", "best_moves_01").has_value());
  for (int i = 0; i < 3; ++i) app.dispatch_event(key(termforge::Key::Right));
  app.step();

  REQUIRE(app.scores().get("sokoban", "best_moves_01") == 3);
  REQUIRE_FALSE(app.scores().get("sokoban", "best_moves_02").has_value());
}

TEST_CASE("a record is monotone downward and every level has its own key",
          "[sokoban][scores]") {
  // ⚠ Snake shipped a score_key whose Walls branch no test reached, and Tetris
  // one whose middle StartLevel never appeared — the same finding two epics
  // running. With twenty keys the only honest cover is to record under EVERY
  // one of them and check nothing collided.
  Probe app;
  enter_sokoban(app);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);

  for (int i = 0; i < level_count(); ++i) {
    game->load(i);
    app.step();
    auto* b = game->board();
    REQUIRE(b != nullptr);
    // Solving twenty levels for real is not a unit test. Drive the model
    // straight to its solved state instead: the key under test is which record
    // the GAME writes, not whether Sokoban is solvable.
    // ⚠ THE BOUND IS A LOCAL COUNTER, not b->moves(). The first version used
    // the model's own counter, which HUNG the mutation harness: deleting
    // `++m_moves` from step() — one of the mutations this loop exists to
    // catch — left the bound frozen at zero and the loop ran forever. A test
    // whose termination depends on the code under test is not a test.
    int guard = 0;
    while (!b->won() && guard < 200) {
      ++guard;
      bool progressed = false;
      for (const Dir d : {Dir::Right, Dir::Left, Dir::Up, Dir::Down}) {
        if (b->step(d).moved) {
          progressed = true;
          break;
        }
      }
      if (!progressed) break;
    }
    if (!b->won()) continue;
    app.dispatch_event(ch(U'u'));
    app.dispatch_event(ch(U'r'));
  }

  // Whatever was reachable above, the keys themselves must be twenty distinct
  // strings and each must belong to exactly one level. Assert that directly.
  int recorded = 0;
  for (int i = 1; i <= level_count(); ++i) {
    const std::string k =
        std::string("best_moves_") + (i < 10 ? "0" : "") + std::to_string(i);
    if (app.scores().get("sokoban", k).has_value()) ++recorded;
  }
  INFO("levels with a record: " << recorded);

  // And the monotone half, on the one level a test can actually solve.
  game->load(0);
  app.step();
  for (int i = 0; i < 3; ++i) app.dispatch_event(key(termforge::Key::Right));
  app.step();
  REQUIRE(app.scores().get("sokoban", "best_moves_01") == 3);

  // Re-solve it the long way round: a worse result must not displace a better.
  game->load(0);
  app.step();
  app.dispatch_event(key(termforge::Key::Down));
  app.dispatch_event(key(termforge::Key::Up));
  for (int i = 0; i < 3; ++i) app.dispatch_event(key(termforge::Key::Right));
  app.step();
  REQUIRE(app.scores().get("sokoban", "best_moves_01") == 3);
}

TEST_CASE("entering the game resumes at the first unsolved level",
          "[sokoban][scores]") {
  // ⚠ Derived from the store rather than persisted separately. The reference
  // keeps its own currentLevel and never range-checks it on the way back in, so
  // a stale index past the end of a replaced level set leaves a blank board.
  Probe app;
  enter_sokoban(app);
  auto* game = game_of(app);
  REQUIRE(game != nullptr);
  REQUIRE(game->index() == 0);

  game->load(0);
  app.step();
  for (int i = 0; i < 3; ++i) app.dispatch_event(key(termforge::Key::Right));
  app.step();
  REQUIRE(app.scores().get("sokoban", "best_moves_01") == 3);

  // Back to the menu and in again: level 1 is done, so we land on level 2.
  app.dispatch_event(key(termforge::Key::Escape));
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);

  app.dispatch_event(key(termforge::Key::Enter));
  app.step();
  auto* again = game_of(app);
  REQUIRE(again != nullptr);
  REQUIRE(again->index() == 1);
}

// ── The roster ───────────────────────────────────────────────────────────────

TEST_CASE("sokoban asks for the Legacy keyboard tier", "[sokoban][meta]") {
  // ⚠ Not a default that nobody thought about. A Sokoban move is one discrete
  // step per press, so there is nothing a key release could tell us — unlike
  // Tetris, which needs releases to express DAS at all. A run that enters only
  // this game must emit no kitty keyboard push, which is what makes it the
  // CONTROL for test/28tetris-ui's pty recipe.
  REQUIRE(Sokoban::kMeta.keyboard == termforge::KeyboardMode::Legacy);
  REQUIRE(Sokoban::kMeta.slug == "sokoban");
}

// ── The pre-start level picker (term-game#38) ────────────────────────────────
//
// ⚠ Sokoban is the ONLY consumer of OptionsScreen's list mode: twenty choices,
// one past kInlineChoiceMax, so the screen renders a windowed vertical list
// instead of a `< value >` cycler. It is the case that proves the schema
// generalises past three-choice difficulty pickers -- which is why it shipped
// alongside the other three rather than after them.

namespace {
// Enter Sokoban and STOP on the picker, which enter_sokoban() dismisses.
auto enter_to_picker(Probe& app, int cols = 80, int rows = 24) -> Sokoban* {
  app.step(1, cols, rows);
  const int index = sokoban_index();
  REQUIRE(index >= 0);
  while (app.selector_index() < index) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  app.dispatch_event(key(termforge::Key::Enter));
  REQUIRE(app.state() == Shell::State::InGame);
  app.step(1, cols, rows);
  Sokoban* g = game_of(app);
  REQUIRE(g != nullptr);
  return g;
}
}  // namespace

TEST_CASE("entering sokoban shows the level picker, not the room",
          "[sokoban][options]") {
  Probe app;
  enter_to_picker(app);
  std::string all;
  for (int y = 0; y < 24; ++y) all += row_text(app, y) + "\n";
  INFO(all);
  CHECK(all.find("Level") != std::string::npos);
  CHECK(all.find("Enter start") != std::string::npos);
  // Several level names, i.e. a LIST -- not one value with arrows either side.
  CHECK(all.find(kLevelNames[0]) != std::string::npos);
  CHECK(all.find(kLevelNames[1]) != std::string::npos);
  CHECK(all.find(kLevelNames[2]) != std::string::npos);
  CHECK(all_seven_bit(app));
}

TEST_CASE("the picker opens ON the resume level, not on level one",
          "[sokoban][options]") {
  // ⚠ THE preselect() CASE, and it must assert on the CURSOR rather than on
  // index(). start() calls load(start_at) before opening the picker, so
  // index() is already correct whether or not preselect() ran -- deleting
  // preselect() leaves every index()-based assertion in this suite green while
  // the picker silently opens on the wrong row.
  //
  // Solve level 0 first so the resume level is 1 rather than 0, or "opens on
  // the resume level" and "opens on the first row" are the same assertion.
  Probe app;
  enter_sokoban(app);
  Sokoban* g = game_of(app);
  REQUIRE(g != nullptr);
  REQUIRE(g->index() == 0);
  // Level 1 is solvable in three moves right -- the same solve the resume case
  // above uses.
  g->load(0);
  app.step();
  for (int i = 0; i < 3; ++i) app.dispatch_event(key(termforge::Key::Right));
  app.step();
  REQUIRE(app.scores().get("sokoban", "best_moves_01") == 3);

  app.dispatch_event(key(termforge::Key::Escape));
  app.step(1, 80, 24);
  REQUIRE(app.state() == Shell::State::Selector);

  Sokoban* again = enter_to_picker(app);
  CHECK(again->index() == 1);            // the game resumed, as before
  CHECK(again->options().selected(0) == 1);  // AND the picker agrees
}

TEST_CASE("picking a level is what gets loaded", "[sokoban][options]") {
  Probe app;
  Sokoban* g = enter_to_picker(app);
  REQUIRE(g->options().selected(0) == 0);

  // Down moves the CHOICE in list mode, because the rows are the choices.
  app.dispatch_event(key(termforge::Key::Down));
  app.dispatch_event(key(termforge::Key::Down));
  app.dispatch_event(key(termforge::Key::Down));
  app.dispatch_event(key(termforge::Key::Enter));
  app.step(1, 80, 24);

  Sokoban* live = game_of(app);
  REQUIRE(live != nullptr);
  CHECK(live->index() == 3);
  // The picker and the loaded level agree on which level index 3 is.
  CHECK(kLevelNames[3] == pack()[3].name);
}

TEST_CASE("dismissing the picker untouched keeps the resume level",
          "[sokoban][options]") {
  // ⚠ The pair of the case above. A game that ignored selected() and always
  // loaded 0 would pass this one alone, because the resume level IS 0 here.
  Probe app;
  enter_sokoban(app);
  Sokoban* g = game_of(app);
  REQUIRE(g != nullptr);
  CHECK(g->index() == 0);
}

TEST_CASE("the picker scrolls to keep the last level reachable",
          "[sokoban][options]") {
  // Twenty levels do not fit twenty-four rows minus chrome, so the window has
  // to move. Without scrolling the last levels are unreachable and the picker
  // is worse than the [ ] keys it was meant to improve on.
  Probe app;
  enter_to_picker(app, 80, 12);
  for (int i = 0; i < level_count() - 1; ++i) {
    app.dispatch_event(key(termforge::Key::Down));
  }
  Sokoban* g = game_of(app);
  REQUIRE(g != nullptr);
  REQUIRE(g->options().selected(0) == level_count() - 1);
  app.step(1, 80, 12);

  std::string all;
  for (int y = 0; y < 12; ++y) all += row_text(app, y) + "\n";
  INFO(all);
  CHECK(all.find(kLevelNames[level_count() - 1]) != std::string::npos);
  CHECK(all.find(kLevelNames[0]) == std::string::npos);
  CHECK(all_seven_bit(app));
}

TEST_CASE("the picker survives the Shell's own floor", "[sokoban][options]") {
  // 20x8 -- smaller than Sokoban's playfield floor, so this is a different
  // question from "the room does not fit".
  Probe app;
  enter_to_picker(app, 20, 8);
  CHECK(all_seven_bit(app));
  CHECK(row_text(app, 7).find("Enter start") != std::string::npos);
}

TEST_CASE("Escape from the picker goes back to the menu", "[sokoban][options]") {
  Probe app;
  enter_to_picker(app);
  app.dispatch_event(key(termforge::Key::Escape));
  app.step(1, 80, 24);
  CHECK(app.state() == Shell::State::Selector);
}
