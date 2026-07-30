// 2048's RULES and its TWEEN, with no terminal anywhere in sight.
//
// ⚠ This file includes board.hpp and anim.hpp and nothing else from the project.
// Neither of those includes a termforge header, so a case here CANNOT construct a
// Screen — it is prevented, not merely discouraged. That is the same discipline
// test/14minesweeper has, and it is what makes "the rules are testable without a
// terminal" a structural fact rather than a habit. Rendering and input live in
// test/23twenty48-ui.
//
// ── Where the expected values come from ─────────────────────────────────────
//
// The slide/merge table below is canonical 2048, cross-checked against the
// HTML-Games reference's slideRow (2048/js/game.js:76-108) rather than derived
// from our own implementation — a table generated from the code under test proves
// only that the code is self-consistent. The fiddly one is the double-merge rule,
// which AGENTS.md names as a reason to read the reference first.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include <termgame/games/twenty48/anim.hpp>
#include <termgame/games/twenty48/board.hpp>

namespace {

using namespace termgame::twenty48;

constexpr std::chrono::duration<double> kTick{1.0 / 60.0};

// A board with one row filled and the rest empty, so a case reads as the row it
// is about. Row 0 unless stated.
[[nodiscard]] auto row_board(std::vector<int> row, int at_row = 0)
    -> std::vector<int> {
  std::vector<int> cells(kCells, 0);
  for (int c = 0; c < kSize && c < static_cast<int>(row.size()); ++c) {
    cells[static_cast<std::size_t>(at_row * kSize + c)] = row[static_cast<std::size_t>(c)];
  }
  return cells;
}

[[nodiscard]] auto read_row(const Board& b, int r) -> std::vector<int> {
  std::vector<int> out;
  for (int c = 0; c < kSize; ++c) {
    out.push_back(b.at(Coord{r, c}));
  }
  return out;
}

// ⚠ Reading a row after a move REQUIRES masking the spawn, and this cost a
// debugging round. Every legal move spawns a tile into some empty cell, and that
// cell can be in the row under test — so `read_row` alone makes a slide assertion
// depend on the RNG, and it fails intermittently as the generator's state
// advances between cases in the same Board. A first draft of this file passed
// standalone and failed inside the suite for exactly that reason.
//
// Masking is exact rather than approximate: a spawn only ever lands on a cell
// that is EMPTY after the slide, so setting it back to 0 recovers the slide's own
// output with nothing else disturbed.
[[nodiscard]] auto row_after(const Board& b, const MoveResult& r, int row)
    -> std::vector<int> {
  auto out = read_row(b, row);
  if (r.spawn.has_value() && r.spawn->at.row == row) {
    out[static_cast<std::size_t>(r.spawn->at.col)] = 0;
  }
  return out;
}

// A board that is full and has no equal orthogonal neighbours, so nothing can
// move in any direction. Built by striping powers of two.
[[nodiscard]] auto deadlocked() -> std::vector<int> {
  return {
      2,    4,    8,    16,
      4,    8,    16,   32,
      8,    16,   32,   64,
      16,   32,   64,   128,
  };
}

// A FULL board on which exactly one move is legal — Left, merging row 0's pair —
// and which is deadlocked immediately afterwards, for either spawn value.
//
// ⚠ The obvious way to build this is to take deadlocked() and poke a hole in it.
// That does NOT work, and the failure is instructive: closing the hole shifts a
// whole row, which slides a 16 directly underneath another 16 and leaves the
// board playable. Verified by watching this exact fixture report Playing.
//
// Full is the key property. A merge frees exactly one cell and the spawn
// immediately refills it, so the board stays full AND the spawn's position is
// forced rather than random — which is what makes the outcome assertions below
// deterministic instead of seed-dependent.
//
//   pre                          after Left + the forced spawn S at (0,3)
//   2   2   8   16               4   8   16  S
//   2   4   8   16               2   4   8   16
//   8   16  32  64               8   16  32  64
//   2   4   8   16               2   4   8   16
//
// Rows 1-3 are left-packed with no equal neighbours, so Left cannot disturb them.
// In the result no orthogonal pair is equal for S == 2 or S == 4, so it is Lost.
[[nodiscard]] auto full_one_merge() -> std::vector<int> {
  return {
      2, 2,  8,  16,
      2, 4,  8,  16,
      8, 16, 32, 64,
      2, 4,  8,  16,
  };
}

}  // namespace

// ── The slide and merge rules ───────────────────────────────────────────────

TEST_CASE("a row compacts toward the direction of travel", "[2048][rules]") {
  Board b(1);

  b.load(row_board({0, 0, 0, 2}));
  auto r = b.move(Dir::Left);
  REQUIRE(r.moved);
  REQUIRE(read_row(b, 0)[0] == 2);
  REQUIRE(r.score_delta == 0);
  REQUIRE(r.merges == 0);

  b.load(row_board({2, 0, 0, 0}));
  r = b.move(Dir::Right);
  REQUIRE(r.moved);
  REQUIRE(read_row(b, 0)[kSize - 1] == 2);
}

TEST_CASE("a tile produced by a move cannot merge again in that move",
          "[2048][rules][double-merge]") {
  // ⚠ THE case. The reference gets this right by always comparing two PRE-move
  // tiles (game.js:85), and the tempting "simplification" — fold the merged value
  // back and rescan — silently turns [2,2,4] into [8]. That is a different game:
  // it roughly halves the number of moves a 2048 run takes.
  Board b(1);

  b.load(row_board({2, 2, 4, 0}));
  auto r = b.move(Dir::Left);
  REQUIRE(row_after(b, r, 0) == std::vector<int>{4, 4, 0, 0});
  REQUIRE(r.score_delta == 4);
  REQUIRE(r.merges == 1);

  b.load(row_board({4, 2, 2, 0}));
  r = b.move(Dir::Left);
  REQUIRE(row_after(b, r, 0) == std::vector<int>{4, 4, 0, 0});
  REQUIRE(r.score_delta == 4);

  // Two independent pairs both merge; that is not a double merge.
  b.load(row_board({2, 2, 2, 2}));
  r = b.move(Dir::Left);
  REQUIRE(row_after(b, r, 0) == std::vector<int>{4, 4, 0, 0});
  REQUIRE(r.score_delta == 8);
  REQUIRE(r.merges == 2);

  // Three equal tiles: the PAIR NEAREST THE DESTINATION merges, the odd one
  // trails. Reversing that is the other classic off-by-one here.
  b.load(row_board({2, 2, 2, 0}));
  r = b.move(Dir::Left);
  REQUIRE(row_after(b, r, 0) == std::vector<int>{4, 2, 0, 0});
  REQUIRE(r.score_delta == 4);

  // ...and the same row moved right merges the other pair.
  b.load(row_board({2, 2, 2, 0}));
  r = b.move(Dir::Right);
  REQUIRE(row_after(b, r, 0) == std::vector<int>{0, 0, 2, 4});
  REQUIRE(r.score_delta == 4);

  b.load(row_board({4, 4, 2, 2}));
  r = b.move(Dir::Left);
  REQUIRE(row_after(b, r, 0) == std::vector<int>{8, 4, 0, 0});
  REQUIRE(r.score_delta == 12);
  REQUIRE(r.merges == 2);
}

TEST_CASE("every direction packs to its own edge", "[2048][rules]") {
  // One tile in the middle, pushed four ways. Catches a transposed or reversed
  // adapter, which is a bug in exactly one direction and therefore easy to ship.
  for (const Dir d : kDirs) {
    Board b(1);
    std::vector<int> cells(kCells, 0);
    cells[1 * kSize + 1] = 8;  // (1,1)
    b.load(cells);
    const auto r = b.move(d);
    REQUIRE(r.moved);

    switch (d) {
      case Dir::Left:
        REQUIRE(b.at(Coord{1, 0}) == 8);
        break;
      case Dir::Right:
        REQUIRE(b.at(Coord{1, kSize - 1}) == 8);
        break;
      case Dir::Up:
        REQUIRE(b.at(Coord{0, 1}) == 8);
        break;
      case Dir::Down:
        REQUIRE(b.at(Coord{kSize - 1, 1}) == 8);
        break;
    }
  }
}

TEST_CASE("a move that changes nothing is not a move", "[2048][rules]") {
  Board b(1);
  b.load(row_board({2, 0, 0, 0}));
  const int before_score = b.score();
  const int before_moves = b.moves();

  const auto r = b.move(Dir::Left);  // already packed left

  REQUIRE_FALSE(r.moved);
  REQUIRE(r.motions.empty());
  REQUIRE_FALSE(r.spawn.has_value());
  REQUIRE(r.score_delta == 0);
  REQUIRE(b.score() == before_score);
  REQUIRE(b.moves() == before_moves);
  // And the board is untouched: no phantom spawn.
  REQUIRE(read_row(b, 0) == std::vector<int>{2, 0, 0, 0});
}

// ── The motion facts the tween needs ────────────────────────────────────────

TEST_CASE("a merge reports both journeys into one destination",
          "[2048][rules][motion]") {
  // This is the information the reference destroys: its filter(Boolean) drops
  // positions before slideRow decides anything, so nothing downstream can say
  // where a tile came from. Without both motions a merge can only be animated as
  // one tile moving and one vanishing.
  Board b(1);
  b.load(row_board({2, 0, 0, 2}));
  const auto r = b.move(Dir::Left);

  REQUIRE(r.moved);
  REQUIRE(r.merges == 1);

  int into_origin = 0;
  for (const auto& m : r.motions) {
    if (m.to == Coord{0, 0}) {
      ++into_origin;
      REQUIRE(m.merged);
      // The PRE-move value travels, not the doubled one — the 4 does not exist
      // until the slide lands.
      REQUIRE(m.value == 2);
    }
  }
  REQUIRE(into_origin == 2);

  // Both source cells are represented exactly once.
  const bool from_c0 = std::ranges::any_of(
      r.motions, [](const Motion& m) { return m.from == Coord{0, 0}; });
  const bool from_c3 = std::ranges::any_of(
      r.motions, [](const Motion& m) { return m.from == Coord{0, 3}; });
  REQUIRE(from_c0);
  REQUIRE(from_c3);
}

TEST_CASE("a tile that does not move still reports a motion",
          "[2048][rules][motion]") {
  // The tween draws from the motion list alone during a slide, so a stationary
  // tile that reports nothing would blink out for the duration of the animation.
  Board b(1);
  b.load(row_board({2, 0, 0, 4}));
  const auto r = b.move(Dir::Left);

  REQUIRE(r.moved);  // the 4 travelled
  REQUIRE(r.motions.size() == 2);
  const bool stationary = std::ranges::any_of(r.motions, [](const Motion& m) {
    return m.from == m.to && m.from == Coord{0, 0};
  });
  REQUIRE(stationary);
}

// ── Spawning ───────────────────────────────────────────────────────────────

TEST_CASE("a new board has exactly two tiles, each a 2 or a 4",
          "[2048][rules][spawn]") {
  for (std::uint64_t seed = 1; seed <= 200; ++seed) {
    Board b(seed);
    REQUIRE(b.empty_count() == kCells - 2);
    for (const int v : b.cells()) {
      REQUIRE((v == 0 || v == 2 || v == 4));
    }
  }
}

TEST_CASE("a legal move spawns exactly one tile", "[2048][rules][spawn]") {
  Board b(7);
  b.load(row_board({2, 0, 0, 2}));
  REQUIRE(b.empty_count() == kCells - 2);

  const auto r = b.move(Dir::Left);
  REQUIRE(r.moved);
  REQUIRE(r.spawn.has_value());
  // Two tiles became one by merging, then one spawned: 2 - 1 + 1 = 2 occupied.
  REQUIRE(b.empty_count() == kCells - 2);
  REQUIRE(b.at(r.spawn->at) == r.spawn->value);
  REQUIRE((r.spawn->value == 2 || r.spawn->value == 4));
}

TEST_CASE("both spawn values occur, in roughly the documented ratio",
          "[2048][rules][spawn]") {
  // ⚠ Not a distribution test with a tolerance anyone should tune — it exists to
  // catch the ONE mistake that matters, which is losing the 4 entirely. In the
  // reference the 90/10 split lives inside the power-tile ternary (game.js:57),
  // so stripping that mechanic naively deletes every 4 and produces a game that
  // looks correct and plays easier. The bounds are wide on purpose.
  int twos = 0;
  int fours = 0;
  for (std::uint64_t seed = 1; seed <= 2000; ++seed) {
    Board b(seed);
    for (const int v : b.cells()) {
      if (v == 2) ++twos;
      if (v == 4) ++fours;
    }
  }
  REQUIRE(fours > 0);
  REQUIRE(twos > 0);
  const double pct = 100.0 * fours / (twos + fours);
  REQUIRE(pct > 5.0);
  REQUIRE(pct < 16.0);
}

// ── Win and loss ───────────────────────────────────────────────────────────

TEST_CASE("reaching the win tile latches Won and play continues",
          "[2048][rules][outcome]") {
  Board b(1);
  b.load(row_board({1024, 1024, 0, 0}));
  REQUIRE(b.state() == State::Playing);

  const auto r = b.move(Dir::Left);
  REQUIRE(r.moved);
  REQUIRE(b.best_tile() >= kWinTile);
  REQUIRE(b.state() == State::Won);

  // ⚠ Won is NOT terminal. The reference shows a win overlay that does not gate
  // input, so play continues behind it; ours says so honestly instead — the game
  // is only over when there are no moves left.
  REQUIRE_FALSE(b.finished());
  REQUIRE(b.can_move());

  // And the latch holds: a further move must not drop back to Playing, which is
  // what a naive "recompute the state each move" would do.
  b.move(Dir::Left);
  REQUIRE(b.state() == State::Won);
}

TEST_CASE("a full board with no equal neighbours is Lost",
          "[2048][rules][outcome]") {
  Board b(1);
  b.load(full_one_merge());
  REQUIRE(b.can_move());
  REQUIRE(b.empty_count() == 0);  // full before, and full after — see the fixture

  const auto r = b.move(Dir::Left);
  REQUIRE(r.moved);
  REQUIRE(r.merges == 1);
  REQUIRE(b.empty_count() == 0);
  REQUIRE_FALSE(b.can_move());
  REQUIRE(b.state() == State::Lost);
  REQUIRE(b.finished());
}

TEST_CASE("a deadlocked board reports no legal move in any direction",
          "[2048][rules][outcome]") {
  Board b(1);
  b.load(deadlocked());
  REQUIRE_FALSE(b.can_move());
  for (const Dir d : kDirs) {
    Board probe(1);
    probe.load(deadlocked());
    REQUIRE_FALSE(probe.move(d).moved);
  }
}

// ── Undo ───────────────────────────────────────────────────────────────────

TEST_CASE("undo restores the board, the score and the move count",
          "[2048][rules][undo]") {
  Board b(3);
  b.load(row_board({2, 2, 0, 0}), 100);
  const auto before = std::vector<int>(b.cells().begin(), b.cells().end());

  REQUIRE_FALSE(b.can_undo());
  const auto r = b.move(Dir::Left);
  REQUIRE(r.moved);
  REQUIRE(b.can_undo());
  REQUIRE(b.score() == 104);
  REQUIRE(b.moves() == 1);

  REQUIRE(b.undo());
  REQUIRE(std::vector<int>(b.cells().begin(), b.cells().end()) == before);
  REQUIRE(b.score() == 100);
  REQUIRE(b.moves() == 0);

  // One level only: consuming it clears it.
  REQUIRE_FALSE(b.can_undo());
  REQUIRE_FALSE(b.undo());
}

TEST_CASE("a move that changed nothing does not consume the undo",
          "[2048][rules][undo]") {
  // ⚠ A REFERENCE BUG, deliberately not ported. There, saveState() runs
  // unconditionally at move() entry (game.js:112) and an illegal move then sets
  // prevState = null (:179) — so making a good move and then pressing a direction
  // that does nothing silently throws your undo away, while leaving the button
  // enabled because that path never re-disables it.
  // ⚠ The full-board fixture, not a sparse one. On a sparse board the first move
  // spawns a tile somewhere random, and that tile can make a SECOND move in the
  // same direction legal — so "the follow-up move is a no-op" would be true only
  // for some seeds. Here the board is full throughout and Left is provably a
  // no-op afterwards.
  Board b(3);
  b.load(full_one_merge(), 0);

  REQUIRE(b.move(Dir::Left).moved);
  REQUIRE(b.can_undo());

  REQUIRE_FALSE(b.move(Dir::Left).moved);  // now packed; a no-op
  REQUIRE(b.can_undo());                   // and the undo SURVIVED

  REQUIRE(b.undo());
  REQUIRE(read_row(b, 0) == std::vector<int>{2, 2, 8, 16});
}

TEST_CASE("undo restores a lost game to playable", "[2048][rules][undo]") {
  Board b(1);
  b.load(full_one_merge());

  REQUIRE(b.move(Dir::Left).moved);
  REQUIRE(b.state() == State::Lost);

  REQUIRE(b.undo());
  // The recorded state comes back, rather than the reference's force-set
  // gameOver = false (game.js:313) — which is the same answer here only because
  // the snapshot happens to be Playing, and would be wrong after a win.
  REQUIRE(b.state() == State::Playing);
  REQUIRE(b.can_move());
}

// ── Determinism ────────────────────────────────────────────────────────────

TEST_CASE("the same seed produces the same game", "[2048][rules][determinism]") {
  // The reason board.hpp uses arcade/rng.hpp rather than <random>:
  // std::uniform_int_distribution is not specified bit-for-bit, so this case
  // would pass under libstdc++ and be a lie under libc++.
  for (std::uint64_t seed : {1ULL, 42ULL, 999983ULL}) {
    Board a(seed);
    Board c(seed);
    REQUIRE(std::vector<int>(a.cells().begin(), a.cells().end()) ==
            std::vector<int>(c.cells().begin(), c.cells().end()));

    // And the whole sequence, not just the opening: drive both identically.
    for (int i = 0; i < 40; ++i) {
      const Dir d = kDirs[static_cast<std::size_t>(i % 4)];
      a.move(d);
      c.move(d);
    }
    REQUIRE(std::vector<int>(a.cells().begin(), a.cells().end()) ==
            std::vector<int>(c.cells().begin(), c.cells().end()));
    REQUIRE(a.score() == c.score());
  }
}

TEST_CASE("different seeds produce different games", "[2048][rules][determinism]") {
  // Guards the other direction: a broken seed path that ignores its argument
  // would satisfy every assertion above.
  Board a(1);
  Board b(2);
  bool differs = false;
  for (int i = 0; i < kCells; ++i) {
    differs = differs || a.cells()[static_cast<std::size_t>(i)] !=
                             b.cells()[static_cast<std::size_t>(i)];
  }
  REQUIRE(differs);
}

// ── The tween ──────────────────────────────────────────────────────────────

TEST_CASE("a finished animation holds exactly the resting board",
          "[2048][anim]") {
  // The property that lets draw() render from Anim alone, with no second path
  // that could drift: once done, the tween IS the board at integer positions.
  Board b(5);
  b.load(row_board({2, 0, 0, 2}));
  const auto r = b.move(Dir::Left);

  Anim a;
  a.begin(r, b.cells());
  REQUIRE_FALSE(a.done());

  for (int i = 0; i < 60 && !a.done(); ++i) {
    a.advance(kTick);
  }
  REQUIRE(a.done());

  // One DrawTile per occupied cell, at integer positions, values matching.
  int occupied = 0;
  for (const int v : b.cells()) {
    if (v != 0) ++occupied;
  }
  REQUIRE(static_cast<int>(a.tiles().size()) == occupied);
  for (const auto& t : a.tiles()) {
    REQUIRE(t.col == static_cast<double>(static_cast<int>(t.col)));
    REQUIRE(t.row == static_cast<double>(static_cast<int>(t.row)));
    REQUIRE(t.pop == 0.0);
    REQUIRE(b.at(Coord{static_cast<int>(t.row), static_cast<int>(t.col)}) ==
            t.value);
  }
}

TEST_CASE("the tween lands identically however dt is chopped up",
          "[2048][anim][framerate]") {
  // ⚠ Issue #5's acceptance criterion: tween state driven by N fixed ticks must
  // be INDEPENDENT of frame rate. Position must be a function of accumulated
  // elapsed time and nothing else — not of how many advance() calls delivered it.
  //
  // ⚠ An earlier version of this case compared only the FINAL state across
  // chunkings, and mutation testing showed that proved almost nothing: the final
  // state is set by finish(), so every chunking agreed no matter what the
  // interpolation did. It never exercised a mid-slide frame at all. The load-
  // bearing comparison is at MATCHED ELAPSED TIME, part-way through, which is
  // what the first block below does.
  Board b(11);
  b.load(row_board({2, 2, 4, 4}));
  const auto r = b.move(Dir::Left);

  // Reach the same instant — 40% of the way through the slide — by one big step
  // and by many small ones. Anything that advances by a fixed amount per call,
  // or accumulates position instead of interpolating from the source cell,
  // diverges here.
  const auto at_same_instant = [&](int calls) {
    Anim a;
    a.begin(r, b.cells());
    const auto target = kSlide * 0.4;
    for (int i = 0; i < calls; ++i) {
      a.advance(target / calls);
    }
    return std::vector<DrawTile>(a.tiles().begin(), a.tiles().end());
  };

  const auto one_step = at_same_instant(1);
  const auto many_steps = at_same_instant(37);  // deliberately not a round number

  REQUIRE_FALSE(one_step.empty());
  REQUIRE(one_step.size() == many_steps.size());
  for (std::size_t i = 0; i < one_step.size(); ++i) {
    REQUIRE(one_step[i].value == many_steps[i].value);
    // Floating-point accumulation over 37 additions is not bit-identical to one
    // addition, so this is the one place a tolerance is honest. It is far tighter
    // than a single cell — the property is "same position", not "same bits".
    REQUIRE(std::fabs(one_step[i].col - many_steps[i].col) < 1e-9);
    REQUIRE(std::fabs(one_step[i].row - many_steps[i].row) < 1e-9);
  }

  // And the final state still agrees, which is the weaker half of the claim but
  // the one a player would notice: tiles must come to rest on the grid.
  const auto run_to_end = [&](int chunks) {
    Anim a;
    a.begin(r, b.cells());
    const auto total = kSlide + kPop;
    for (int i = 0; i < chunks + 2; ++i) {
      a.advance(total / chunks);
    }
    std::vector<DrawTile> out(a.tiles().begin(), a.tiles().end());
    return std::pair{a.done(), out};
  };

  const auto [done_fine, fine] = run_to_end(60);   // ~1000 fps
  const auto [done_coarse, coarse] = run_to_end(3);  // ~19 fps
  const auto [done_one, one] = run_to_end(1);        // one giant frame

  REQUIRE(done_fine);
  REQUIRE(done_coarse);
  REQUIRE(done_one);
  REQUIRE(fine.size() == coarse.size());
  REQUIRE(fine.size() == one.size());
  for (std::size_t i = 0; i < fine.size(); ++i) {
    REQUIRE(fine[i].col == coarse[i].col);
    REQUIRE(fine[i].row == coarse[i].row);
    REQUIRE(fine[i].value == coarse[i].value);
    REQUIRE(fine[i].col == one[i].col);
    REQUIRE(fine[i].row == one[i].row);
  }
}

TEST_CASE("mid-slide the travellers carry their pre-move values",
          "[2048][anim]") {
  Board b(13);
  b.load(row_board({2, 0, 0, 2}));
  const auto r = b.move(Dir::Left);

  Anim a;
  a.begin(r, b.cells());
  a.advance(kSlide / 2);
  REQUIRE(a.sliding());

  // Two 2s in flight. A 4 on screen here would mean the implementation resolved
  // the board and then animated something that never happened.
  REQUIRE(a.tiles().size() == 2);
  for (const auto& t : a.tiles()) {
    REQUIRE(t.value == 2);
  }

  // And they are genuinely between cells, not snapped.
  const bool fractional = std::ranges::any_of(a.tiles(), [](const DrawTile& t) {
    return t.col != static_cast<double>(static_cast<int>(t.col));
  });
  REQUIRE(fractional);
}

TEST_CASE("finish() snaps straight to rest", "[2048][anim]") {
  Board b(17);
  b.load(row_board({2, 0, 0, 2}));
  const auto r = b.move(Dir::Left);

  Anim a;
  a.begin(r, b.cells());
  a.advance(kSlide / 3);
  REQUIRE_FALSE(a.done());

  a.finish();
  REQUIRE(a.done());
  for (const auto& t : a.tiles()) {
    REQUIRE(t.col == static_cast<double>(static_cast<int>(t.col)));
    REQUIRE(t.pop == 0.0);
  }
}

TEST_CASE("a move that changed nothing animates nothing", "[2048][anim]") {
  Board b(19);
  b.load(row_board({2, 0, 0, 0}));
  const auto r = b.move(Dir::Left);
  REQUIRE_FALSE(r.moved);

  Anim a;
  a.begin(r, b.cells());
  // Immediately at rest: there is nothing to interpolate, so a slide here would
  // be a visible stutter on every rejected key press.
  REQUIRE(a.done());
  REQUIRE(a.tiles().size() == 1);
}

TEST_CASE("animation is not a participant in the rules",
          "[2048][anim][framerate]") {
  // ⚠ AGENTS.md's hard rule, as a test rather than a comment. Ten moves resolved
  // back-to-back with NO ticks between them must produce the identical board to
  // ten moves each allowed to animate fully — same seed, same order. If the
  // animation could influence the model, or if input were queued behind it, these
  // two diverge.
  const auto play = [](bool animate) {
    Board b(2024);
    Anim a;
    a.rest(b.cells());
    for (int i = 0; i < 10; ++i) {
      const Dir d = kDirs[static_cast<std::size_t>(i % 4)];
      if (!a.done()) {
        a.finish();  // the Game's input policy, verbatim
      }
      const auto r = b.move(d);
      a.begin(r, b.cells());
      if (animate) {
        for (int f = 0; f < 60 && !a.done(); ++f) {
          a.advance(kTick);
        }
      }
    }
    return std::tuple{std::vector<int>(b.cells().begin(), b.cells().end()),
                      b.score(), b.moves()};
  };

  const auto [cells_fast, score_fast, moves_fast] = play(false);
  const auto [cells_slow, score_slow, moves_slow] = play(true);

  REQUIRE(cells_fast == cells_slow);
  REQUIRE(score_fast == score_slow);
  REQUIRE(moves_fast == moves_slow);
}
