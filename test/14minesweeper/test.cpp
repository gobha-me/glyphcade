// Minesweeper RULES, with no Screen, no Shell and no terminal anywhere in the
// file. This is AGENTS.md's "drive the model, assert state" tier.
//
// ⚠ Note what is NOT included below: no termforge header, and no
// termgame/arcade header. board.hpp names no termforge type, so a Screen
// cannot be constructed here even by accident. If a case in this file ever
// needs one, the model and the view have grown together and the fix belongs
// upstream of the test.
//
// Two conventions worth knowing before adding a case:
//
//  1. Anything asserting a specific outcome — a flood-fill set, a chord, a win
//     — builds its board with load_mines() and NOT with a seed. A layout
//     derived from a seed pins the RNG, so changing the RNG would turn every
//     rule test red for no reason. Seeds are used only where the RNG itself is
//     the subject: placement, safety, determinism.
//  2. The clock is driven by advance(dt). There is no clock in the model, so
//     these run instantly and assert exact equalities.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <set>
#include <vector>

#include <termgame/games/minesweeper/board.hpp>
#include <termgame/games/minesweeper/layout.hpp>

namespace {

using namespace termgame::minesweeper;
using Seconds = std::chrono::duration<double>;

constexpr Seconds kTick{1.0 / 60.0};

[[nodiscard]] auto mine_set(const Board& b) -> std::set<std::pair<int, int>> {
  std::set<std::pair<int, int>> out;
  for (int r = 0; r < b.rows(); ++r) {
    for (int c = 0; c < b.cols(); ++c) {
      if (b.at({.row = r, .col = c}).mine) out.emplace(r, c);
    }
  }
  return out;
}

[[nodiscard]] auto revealed_set(const Board& b) -> std::set<std::pair<int, int>> {
  std::set<std::pair<int, int>> out;
  for (int r = 0; r < b.rows(); ++r) {
    for (int c = 0; c < b.cols(); ++c) {
      if (b.at({.row = r, .col = c}).revealed) out.emplace(r, c);
    }
  }
  return out;
}

// Reveal every safe cell, in index order. Used by the win cases; deliberately
// not a Board method, because "reveal everything" is not a thing a player can
// do and does not belong in the model's API.
auto reveal_all_safe(Board& b) -> void {
  for (int r = 0; r < b.rows(); ++r) {
    for (int c = 0; c < b.cols(); ++c) {
      const Coord p{.row = r, .col = c};
      if (!b.at(p).mine) b.reveal(p);
    }
  }
}

}  // namespace

// ─── Presets and the fresh board ───────────────────────────────────────────

TEST_CASE("the presets are the reference's presets") {
  REQUIRE(preset(Level::Easy).rows == 9);
  REQUIRE(preset(Level::Easy).cols == 9);
  REQUIRE(preset(Level::Easy).mines == 10);
  REQUIRE(preset(Level::Medium).rows == 16);
  REQUIRE(preset(Level::Medium).cols == 16);
  REQUIRE(preset(Level::Medium).mines == 40);
  // Hard is 16 rows by 30 columns, not 30x16. Getting this backwards produces a
  // board that fits the terminal and is the wrong game.
  REQUIRE(preset(Level::Hard).rows == 16);
  REQUIRE(preset(Level::Hard).cols == 30);
  REQUIRE(preset(Level::Hard).mines == 99);
}

TEST_CASE("a fresh board has no mines on it at all") {
  // Deferred placement is the mechanism behind first-click safety. If mines
  // exist before the first click, the safety case below is testing nothing.
  for (const Level level : kLevels) {
    Board b(preset(level), 12345);
    REQUIRE(b.state() == State::Ready);
    REQUIRE(b.revealed_count() == 0);
    REQUIRE(b.flag_count() == 0);
    REQUIRE(b.mines_remaining() == preset(level).mines);
    REQUIRE_FALSE(b.timer_running());
    REQUIRE(mine_set(b).empty());
  }
}

TEST_CASE("out-of-range coordinates read as a blank cell and change nothing") {
  Board b(preset(Level::Easy), 1);
  const Coord off{.row = -1, .col = 99};
  REQUIRE_FALSE(b.in_bounds(off));
  REQUIRE_FALSE(b.at(off).mine);
  REQUIRE_FALSE(b.at(off).revealed);
  REQUIRE_FALSE(b.reveal(off));
  REQUIRE_FALSE(b.cycle_mark(off));
  REQUIRE_FALSE(b.chord(off));
  REQUIRE(b.state() == State::Ready);
}

// ─── Placement: safety, count, determinism ─────────────────────────────────

TEST_CASE("the first click and all eight of its neighbours are never mines") {
  // The claim the whole game rests on. Swept over many seeds and over corner,
  // edge and centre clicks, because the safe zone is clipped differently in
  // each and an off-by-one would survive a single centred sample.
  const Coord clicks[]{{0, 0}, {0, 8}, {8, 0}, {8, 8}, {0, 4}, {4, 0}, {4, 4}};
  for (std::uint64_t seed = 0; seed < 200; ++seed) {
    for (const Coord click : clicks) {
      Board b(preset(Level::Easy), seed);
      REQUIRE(b.reveal(click));
      for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
          const Coord n{.row = click.row + dr, .col = click.col + dc};
          if (b.in_bounds(n)) REQUIRE_FALSE(b.at(n).mine);
        }
      }
    }
  }
}

TEST_CASE("the first click therefore always opens a region, never a lone number") {
  // The consequence of the safe zone: with all eight neighbours clear, the
  // clicked cell's adjacent count is zero, so the flood always runs.
  for (std::uint64_t seed = 0; seed < 100; ++seed) {
    Board b(preset(Level::Medium), seed);
    REQUIRE(b.reveal({.row = 8, .col = 8}));
    REQUIRE(b.at({.row = 8, .col = 8}).adjacent == 0);
    REQUIRE(b.revealed_count() >= 9);
  }
}

TEST_CASE("exactly total_mines() mines are placed, on every seed and level") {
  for (const Level level : kLevels) {
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
      Board b(preset(level), seed);
      b.reveal({.row = 1, .col = 1});
      REQUIRE(static_cast<int>(mine_set(b).size()) == preset(level).mines);
      REQUIRE(b.total_mines() == preset(level).mines);
    }
  }
}

TEST_CASE("placement is deterministic per seed, and seeds actually differ") {
  for (std::uint64_t seed = 0; seed < 20; ++seed) {
    Board a(preset(Level::Medium), seed);
    Board b(preset(Level::Medium), seed);
    a.reveal({.row = 3, .col = 3});
    b.reveal({.row = 3, .col = 3});
    REQUIRE(mine_set(a) == mine_set(b));
  }

  // Determinism is worthless if the generator is constant. Distinct seeds must
  // produce distinct boards.
  std::set<std::set<std::pair<int, int>>> layouts;
  for (std::uint64_t seed = 0; seed < 64; ++seed) {
    Board b(preset(Level::Easy), seed);
    b.reveal({.row = 4, .col = 4});
    layouts.insert(mine_set(b));
  }
  REQUIRE(layouts.size() > 1);
}

TEST_CASE("adjacency counts agree with the mines that were placed") {
  for (std::uint64_t seed = 0; seed < 20; ++seed) {
    Board b(preset(Level::Easy), seed);
    b.reveal({.row = 4, .col = 4});
    for (int r = 0; r < b.rows(); ++r) {
      for (int c = 0; c < b.cols(); ++c) {
        const Coord p{.row = r, .col = c};
        if (b.at(p).mine) continue;
        int expect = 0;
        for_each_neighbour(b.rows(), b.cols(), p, [&](Coord n) {
          if (b.at(n).mine) ++expect;
        });
        REQUIRE(static_cast<int>(b.at(p).adjacent) == expect);
      }
    }
  }
}

// ─── Reveal and flood fill ─────────────────────────────────────────────────

TEST_CASE("flood fill opens exactly the reachable region and its border") {
  // 5x5, one mine at (4,4). Clicking (0,0) opens everything except the mine
  // itself: the only cells with a non-zero count are the mine's neighbours, and
  // they are revealed as terminals.
  Board b(preset(Level::Easy), 0);
  const Coord mines[]{{4, 4}};
  b.load_mines(mines);

  REQUIRE(b.reveal({.row = 0, .col = 0}));
  const auto seen = revealed_set(b);
  REQUIRE(seen.size() == static_cast<std::size_t>(b.rows() * b.cols() - 1));
  REQUIRE_FALSE(seen.contains({4, 4}));
}

TEST_CASE("a number reveals but does not recurse") {
  Board b(preset(Level::Easy), 0);
  const Coord mines[]{{0, 0}};
  b.load_mines(mines);
  // (1,1) touches the mine, so it is a 1 and is a terminal for the flood.
  REQUIRE(b.at({.row = 1, .col = 1}).adjacent == 1);
  REQUIRE(b.reveal({.row = 1, .col = 1}));
  REQUIRE(revealed_set(b) == std::set<std::pair<int, int>>{{1, 1}});
}

TEST_CASE("flags block the flood; question marks do not") {
  // ⚠ This is a rule, not an accident. A flag is the player asserting a mine is
  // there, and the flood respects the assertion even when it is wrong — which
  // is why a stray flag leaves a visible hole in an opened region. A question
  // mark is a note to self and blocks nothing.
  const Coord mines[]{{8, 8}};

  Board flagged(preset(Level::Easy), 0);
  flagged.load_mines(mines);
  REQUIRE(flagged.cycle_mark({.row = 0, .col = 1}));  // None -> Flag
  REQUIRE(flagged.at({.row = 0, .col = 1}).mark == Mark::Flag);
  REQUIRE(flagged.reveal({.row = 0, .col = 0}));
  REQUIRE_FALSE(flagged.at({.row = 0, .col = 1}).revealed);

  Board questioned(preset(Level::Easy), 0);
  questioned.load_mines(mines);
  REQUIRE(questioned.cycle_mark({.row = 0, .col = 1}));  // -> Flag
  REQUIRE(questioned.cycle_mark({.row = 0, .col = 1}));  // -> Question
  REQUIRE(questioned.at({.row = 0, .col = 1}).mark == Mark::Question);
  REQUIRE(questioned.reveal({.row = 0, .col = 0}));
  REQUIRE(questioned.at({.row = 0, .col = 1}).revealed);
}

TEST_CASE("an empty hard board floods all 480 cells in one call") {
  // The explicit-stack claim. A recursive flood would be 480 frames deep here,
  // which is the depth at which "works on my machine" starts to mean something
  // different under ASan or a smaller thread stack.
  Board b(preset(Level::Hard), 0);
  b.load_mines({});
  REQUIRE(b.total_mines() == 0);
  REQUIRE(b.reveal({.row = 0, .col = 0}));
  REQUIRE(b.revealed_count() == 16 * 30);
}

TEST_CASE("revealing an already-revealed cell changes nothing") {
  Board b(preset(Level::Easy), 0);
  const Coord mines[]{{0, 0}};
  b.load_mines(mines);
  REQUIRE(b.reveal({.row = 1, .col = 1}));
  const int before = b.revealed_count();
  REQUIRE_FALSE(b.reveal({.row = 1, .col = 1}));
  REQUIRE(b.revealed_count() == before);
}

// ─── Marks ─────────────────────────────────────────────────────────────────

TEST_CASE("marks cycle None -> Flag -> Question -> None") {
  Board b(preset(Level::Easy), 1);
  const Coord p{.row = 2, .col = 2};
  REQUIRE(b.at(p).mark == Mark::None);
  REQUIRE(b.cycle_mark(p));
  REQUIRE(b.at(p).mark == Mark::Flag);
  REQUIRE(b.flag_count() == 1);
  REQUIRE(b.cycle_mark(p));
  REQUIRE(b.at(p).mark == Mark::Question);
  REQUIRE(b.flag_count() == 0);
  REQUIRE(b.cycle_mark(p));
  REQUIRE(b.at(p).mark == Mark::None);
  REQUIRE(b.flag_count() == 0);
}

TEST_CASE("a revealed cell cannot be marked") {
  // Three mines, and the cell revealed is a NUMBER — so the flood stops dead
  // and the board is nowhere near won. A one-mine board would flood into a win
  // here, and the win's auto-flagging would answer the flag_count assertion
  // instead of cycle_mark() declining.
  Board b(preset(Level::Easy), 0);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  b.load_mines(mines);
  const Coord number{.row = 1, .col = 1};
  REQUIRE(b.reveal(number));
  REQUIRE(b.at(number).revealed);
  REQUIRE(b.revealed_count() == 1);
  REQUIRE(b.state() == State::Playing);
  REQUIRE_FALSE(b.cycle_mark(number));
  REQUIRE(b.flag_count() == 0);
}

TEST_CASE("over-flagging drives the mine counter negative") {
  // Unclamped on purpose: a player who has placed more flags than there are
  // mines has made a mistake, and a counter stuck at zero hides it.
  Board b(preset(Level::Easy), 1);
  for (int c = 0; c < 9; ++c) REQUIRE(b.cycle_mark({.row = 0, .col = c}));
  for (int c = 0; c < 9; ++c) REQUIRE(b.cycle_mark({.row = 1, .col = c}));
  REQUIRE(b.flag_count() == 18);
  REQUIRE(b.mines_remaining() == 10 - 18);
}

TEST_CASE("flagging never places mines and never starts the clock") {
  Board b(preset(Level::Easy), 7);
  REQUIRE(b.cycle_mark({.row = 4, .col = 4}));
  REQUIRE(b.state() == State::Ready);
  REQUIRE(mine_set(b).empty());
  REQUIRE_FALSE(b.timer_running());
}

TEST_CASE("a first click on a flagged cell arms nothing") {
  // ⚠ DIVERGENCE from the reference, pinned as a negative. HTML-Games'
  // onLeftClick consumes its firstClick flag, places the mines and starts the
  // timer BEFORE reveal() bails on the flag — so the clock runs on a board the
  // player never opened. All three assertions below are red if the guard in
  // reveal() drifts below the placement block.
  Board b(preset(Level::Easy), 7);
  const Coord p{.row = 4, .col = 4};
  REQUIRE(b.cycle_mark(p));
  REQUIRE_FALSE(b.reveal(p));
  REQUIRE(b.state() == State::Ready);
  REQUIRE(mine_set(b).empty());
  REQUIRE_FALSE(b.timer_running());
}

// ─── Chord ─────────────────────────────────────────────────────────────────

TEST_CASE("a satisfied chord opens the unflagged neighbours") {
  Board b(preset(Level::Easy), 0);
  const Coord mines[]{{0, 0}};
  b.load_mines(mines);
  const Coord one{.row = 1, .col = 1};
  REQUIRE(b.reveal(one));
  REQUIRE(b.at(one).adjacent == 1);
  REQUIRE(b.cycle_mark({.row = 0, .col = 0}));  // the flag is correct

  REQUIRE(b.chord(one));
  REQUIRE(b.at({.row = 0, .col = 1}).revealed);
  REQUIRE(b.at({.row = 1, .col = 0}).revealed);
  REQUIRE(b.at({.row = 2, .col = 2}).revealed);
  REQUIRE(b.state() != State::Lost);
}

TEST_CASE("an unsatisfied chord is a no-op, never a partial reveal") {
  Board b(preset(Level::Easy), 0);
  const Coord mines[]{{0, 0}};
  b.load_mines(mines);
  const Coord one{.row = 1, .col = 1};
  REQUIRE(b.reveal(one));
  const auto before = revealed_set(b);
  REQUIRE_FALSE(b.chord(one));  // zero flags placed, count is 1
  REQUIRE(revealed_set(b) == before);
}

TEST_CASE("a chord on a hidden cell or a revealed zero does nothing") {
  Board b(preset(Level::Easy), 0);
  const Coord mines[]{{8, 8}};
  b.load_mines(mines);
  REQUIRE_FALSE(b.chord({.row = 3, .col = 3}));  // hidden
  REQUIRE(b.reveal({.row = 0, .col = 0}));
  REQUIRE(b.at({.row = 0, .col = 0}).adjacent == 0);
  REQUIRE_FALSE(b.chord({.row = 0, .col = 0}));  // revealed, but a zero
}

TEST_CASE("a chord through a misplaced flag loses the game") {
  // The punishing case, and the reason chord is a real decision rather than a
  // convenience: the count is satisfied, but the flag is on the wrong cell.
  Board b(preset(Level::Easy), 0);
  const Coord mines[]{{0, 0}};
  b.load_mines(mines);
  const Coord one{.row = 1, .col = 1};
  REQUIRE(b.reveal(one));
  REQUIRE(b.cycle_mark({.row = 0, .col = 1}));  // wrong cell, right count

  REQUIRE(b.chord(one));
  REQUIRE(b.state() == State::Lost);
  REQUIRE(b.exploded().has_value());
}

// ─── Terminal states ───────────────────────────────────────────────────────

TEST_CASE("revealing every safe cell wins, with no flags placed") {
  // Flags are irrelevant to the win condition; only the revealed count matters.
  Board b(preset(Level::Easy), 0);
  const Coord mines[]{{0, 0}, {8, 8}};
  b.load_mines(mines);
  reveal_all_safe(b);

  REQUIRE(b.state() == State::Won);
  REQUIRE(b.revealed_count() == 9 * 9 - 2);
  REQUIRE_FALSE(b.timer_running());
  // The win auto-flags what is left, so the counter reads zero.
  REQUIRE(b.at({.row = 0, .col = 0}).mark == Mark::Flag);
  REQUIRE(b.at({.row = 8, .col = 8}).mark == Mark::Flag);
  REQUIRE(b.mines_remaining() == 0);
}

TEST_CASE("hitting a mine loses, names the cell, and latches") {
  Board b(preset(Level::Easy), 0);
  const Coord mines[]{{3, 3}, {5, 5}};
  b.load_mines(mines);

  REQUIRE(b.reveal({.row = 3, .col = 3}));
  REQUIRE(b.state() == State::Lost);
  REQUIRE(b.finished());
  REQUIRE(b.exploded() == Coord{.row = 3, .col = 3});
  REQUIRE_FALSE(b.timer_running());

  // Every verb is inert afterwards.
  REQUIRE_FALSE(b.reveal({.row = 0, .col = 0}));
  REQUIRE_FALSE(b.cycle_mark({.row = 0, .col = 0}));
  REQUIRE_FALSE(b.chord({.row = 0, .col = 0}));
}

TEST_CASE("a loss does not pollute the revealed count") {
  // The reference marks every mine revealed on a loss and lets its counter
  // absorb them, so revealed_count() stops describing what the player opened.
  // Mine visibility is resolved at draw time here instead.
  Board b(preset(Level::Easy), 0);
  const Coord mines[]{{3, 3}, {5, 5}};
  b.load_mines(mines);
  REQUIRE(b.reveal({.row = 3, .col = 3}));
  REQUIRE(b.revealed_count() == 0);
  REQUIRE_FALSE(b.at({.row = 5, .col = 5}).revealed);
}

// ─── The clock ─────────────────────────────────────────────────────────────

TEST_CASE("the clock does not run until the first reveal") {
  Board b(preset(Level::Easy), 3);
  for (int i = 0; i < 600; ++i) b.advance(kTick);
  REQUIRE(b.seconds() == 0);

  REQUIRE(b.cycle_mark({.row = 0, .col = 0}));
  for (int i = 0; i < 600; ++i) b.advance(kTick);
  REQUIRE(b.seconds() == 0);

  REQUIRE(b.reveal({.row = 4, .col = 4}));
  REQUIRE(b.timer_running());
  for (int i = 0; i < 60; ++i) b.advance(kTick);
  REQUIRE(b.seconds() == 1);
}

TEST_CASE("the clock stops on a win and on a loss") {
  {
    Board b(preset(Level::Easy), 0);
    const Coord mines[]{{0, 0}};
    b.load_mines(mines);
    reveal_all_safe(b);
    REQUIRE(b.state() == State::Won);
    const int at_end = b.seconds();
    for (int i = 0; i < 600; ++i) b.advance(kTick);
    REQUIRE(b.seconds() == at_end);
  }
  {
    Board b(preset(Level::Easy), 0);
    const Coord mines[]{{0, 0}};
    b.load_mines(mines);
    REQUIRE(b.reveal({.row = 0, .col = 0}));
    REQUIRE(b.state() == State::Lost);
    for (int i = 0; i < 600; ++i) b.advance(kTick);
    REQUIRE(b.seconds() == 0);
  }
}

TEST_CASE("the clock clamps at 999 and stays there") {
  // Reveal a NUMBER, not a region: the game has to still be running for the
  // clock to run, and a one-mine board floods straight into a win.
  Board b(preset(Level::Easy), 0);
  const Coord mines[]{{0, 0}, {4, 4}, {8, 8}};
  b.load_mines(mines);
  REQUIRE(b.reveal({.row = 1, .col = 1}));
  REQUIRE(b.state() == State::Playing);
  for (int i = 0; i < 60 * 1200; ++i) b.advance(kTick);
  REQUIRE(b.seconds() == Board::kTimerCap);
  for (int i = 0; i < 60 * 2000; ++i) b.advance(kTick);
  REQUIRE(b.seconds() == Board::kTimerCap);
}

// ─── Placement on a board the reference's sampler cannot fill ──────────────

TEST_CASE("a board denser than the safe zone allows still completes") {
  // ⚠ MUTATION SIGNAL. Restoring the reference's rejection sampler
  // (minesweeper/js/game.js:52-62) makes this case HANG rather than fail —
  // that loop retries forever once the eligible pool is smaller than the mine
  // count. If ctest ever times out here, that is what happened.
  //
  // load_mines() is not usable for this one: the subject is place_mines()
  // itself, so the mines must come from a real first click. This is why Board
  // takes a Preset rather than a Level — no shipped preset is dense enough to
  // exercise the guard, and a guard no test can reach is a guess.
  constexpr Preset kDense{.rows = 5, .cols = 5, .mines = 16, .name = "DENSE"};
  Board b(kDense, 11);
  REQUIRE(b.reveal({.row = 2, .col = 2}));
  REQUIRE(static_cast<int>(mine_set(b).size()) == 16);
  REQUIRE(b.total_mines() == 16);
  // The safe zone collapsed to the clicked cell alone — that is the documented
  // degradation, and the clicked cell itself is still never a mine.
  REQUIRE_FALSE(b.at({.row = 2, .col = 2}).mine);

  // Denser still: more mines than cells outside the click. Placement must cap
  // at what fits and say so, rather than looping or over-reporting.
  constexpr Preset kImpossible{
      .rows = 3, .cols = 3, .mines = 50, .name = "IMPOSSIBLE"};
  Board over(kImpossible, 3);
  REQUIRE(over.reveal({.row = 1, .col = 1}));
  REQUIRE(over.total_mines() == 8);
  REQUIRE(static_cast<int>(mine_set(over).size()) == 8);
}

TEST_CASE("the RNG is unbiased enough to reach every cell") {
  // A generator that never selects some region would still pass determinism and
  // count checks while making part of the board permanently safe.
  std::set<std::pair<int, int>> ever;
  for (std::uint64_t seed = 0; seed < 400; ++seed) {
    Board b(preset(Level::Easy), seed);
    b.reveal({.row = 4, .col = 4});
    for (const auto& m : mine_set(b)) ever.insert(m);
  }
  // Every cell outside the fixed safe zone around (4,4) must have held a mine
  // at least once across 400 layouts.
  REQUIRE(ever.size() == static_cast<std::size_t>(9 * 9 - 9));
}

// ─── Geometry ──────────────────────────────────────────────────────────────
// Still no Screen: compute_layout takes two ints and returns coordinates.

TEST_CASE("the required size matches what each level actually needs") {
  REQUIRE(needed_cols(9) == 21);
  REQUIRE(needed_rows(9) == 13);
  REQUIRE(needed_cols(16) == 35);
  REQUIRE(needed_rows(16) == 20);
  // Hard is the level that does not fit an 80-column terminal comfortably, and
  // 63 is the number quoted upstream in termforge #62 as the cost of having no
  // reverse-video attribute to mark the cursor with.
  REQUIRE(needed_cols(30) == 63);
  REQUIRE(needed_rows(16) == 20);
}

TEST_CASE("fits is exact at the boundary") {
  REQUIRE(compute_layout(63, 20, 16, 30).fits);
  REQUIRE_FALSE(compute_layout(62, 20, 16, 30).fits);
  REQUIRE_FALSE(compute_layout(63, 19, 16, 30).fits);
  REQUIRE(compute_layout(21, 13, 9, 9).fits);
  // The Shell's own floor is 20x8, one column short of even Easy.
  REQUIRE_FALSE(compute_layout(20, 8, 9, 9).fits);
}

TEST_CASE("every cell round-trips between drawing and hit-testing") {
  // The claim the whole file exists for. Both columns of every cell, at every
  // size the game can be drawn at, must map back to that cell.
  struct Size {
    int cols;
    int rows;
  };
  const Size sizes[]{{80, 24}, {100, 30}, {63, 20}, {35, 20}, {21, 13}, {120, 40}};
  const Level levels[]{Level::Easy, Level::Medium, Level::Hard};

  for (const Size s : sizes) {
    for (const Level level : levels) {
      const Preset p = preset(level);
      const Layout l = compute_layout(s.cols, s.rows, p.rows, p.cols);
      if (!l.fits) continue;
      for (int r = 0; r < p.rows; ++r) {
        for (int c = 0; c < p.cols; ++c) {
          const Coord want{.row = r, .col = c};
          REQUIRE(l.cell_at(l.glyph_x(c), l.row_y(r)) == want);
          REQUIRE(l.cell_at(l.gutter_x(c), l.row_y(r)) == want);
        }
      }
    }
  }
}

TEST_CASE("the grid never draws onto the border or off the screen") {
  const Preset p = preset(Level::Hard);
  const Layout l = compute_layout(80, 24, p.rows, p.cols);
  REQUIRE(l.fits);
  REQUIRE(l.frame_x >= 0);
  REQUIRE(l.frame_y >= 1);                      // below the status row
  REQUIRE(l.frame_y + l.frame_h <= l.hint_y);   // above the hint row
  REQUIRE(l.gutter_x(0) > l.frame_x);           // inside the left border
  // The furthest write is the closing bracket of a cursor on the last column.
  const int last = l.gutter_x(p.cols - 1) + kCellCols;
  REQUIRE(last < l.frame_x + l.frame_w - 1);
}

TEST_CASE("clicks outside the grid map to nothing") {
  const Preset p = preset(Level::Easy);
  const Layout l = compute_layout(40, 20, p.rows, p.cols);
  REQUIRE(l.fits);

  REQUIRE_FALSE(l.cell_at(l.origin_x - 1, l.row_y(0)).has_value());   // border
  REQUIRE_FALSE(l.cell_at(l.glyph_x(0), l.origin_y - 1).has_value());  // border
  REQUIRE_FALSE(l.cell_at(l.glyph_x(0), l.row_y(p.rows)).has_value());
  REQUIRE_FALSE(l.cell_at(l.glyph_x(0), l.status_y).has_value());
  REQUIRE_FALSE(l.cell_at(l.glyph_x(0), l.hint_y).has_value());
  REQUIRE_FALSE(l.cell_at(-5, -5).has_value());
  REQUIRE_FALSE(l.cell_at(9999, 9999).has_value());
  // The trailing bracket column belongs to no cell.
  REQUIRE_FALSE(l.cell_at(l.gutter_x(p.cols), l.row_y(0)).has_value());
}

TEST_CASE("a board that does not fit maps every click to nothing") {
  const Preset p = preset(Level::Hard);
  const Layout l = compute_layout(60, 20, p.rows, p.cols);
  REQUIRE_FALSE(l.fits);
  for (int y = 0; y < 20; ++y) {
    for (int x = 0; x < 60; ++x) REQUIRE_FALSE(l.cell_at(x, y).has_value());
  }
  // The status and hint rows still exist, because the player still needs them.
  REQUIRE(l.status_y == 0);
  REQUIRE(l.hint_y == 19);
}
