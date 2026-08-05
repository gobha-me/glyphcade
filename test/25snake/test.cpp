// Snake's RULES and its STEP CLOCK, with no terminal anywhere in sight.
//
// ⚠ This file includes board.hpp and nothing else from the project. board.hpp
// includes no termforge header, so a case here CANNOT construct a Screen — it is
// prevented, not merely discouraged. Same discipline as test/14minesweeper and
// test/22twenty48. Rendering and input live in test/26snake-ui.
//
// ── Where the expected values come from ──────────────────────────────────────
//
// The rules are cross-checked against the HTML-Games reference
// (snake/js/{snake,food,game}.js) rather than derived from our own
// implementation — a table generated from the code under test proves only that
// the code is self-consistent. Three things below deliberately DIVERGE from that
// reference, each because the reference is wrong rather than different, and each
// has its own case saying so:
//
//   * the step clock accumulates and subtracts (game.js:78 assigns, which
//     silently rounds every step up to a frame boundary);
//   * turns queue two deep and are judged against the QUEUED direction
//     (snake.js:79 keeps one slot and judges against the applied one);
//   * food spawn picks among free cells and a full board is a win (food.js:13
//     is an unbounded rejection loop that hangs on a full grid).

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

#include <glyphcade/games/snake/board.hpp>

namespace {

using namespace glyphcade::snake;

constexpr std::chrono::duration<double> kTick{1.0 / 60.0};

[[nodiscard]] auto ms(int n) -> std::chrono::duration<double> {
  return std::chrono::duration<double>{n / 1000.0};
}

// A board built from an explicit snake, with the food parked somewhere the
// snake will not reach during the case. Far corner unless stated.
[[nodiscard]] auto fixture(std::vector<Coord> body,
                           Coord food = {kCols - 1, kRows - 1}) -> Board {
  Board b(Level::Normal, Walls::Solid, 1234);
  b.load(body, food);
  return b;
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

// Drive exactly one step, whatever the current interval is. Used by rule cases
// that care about the move and not about the clock.
auto one_step(Board& b) -> TickResult {
  return b.tick(ms(b.interval_ms()));
}

[[nodiscard]] auto heads(const Board& b) -> Coord { return b.head(); }

}  // namespace

// ─── Movement and growth ────────────────────────────────────────────────────

TEST_CASE("a fresh board starts with three segments at the centre heading right",
          "[snake][rules]") {
  const Board b(Level::Normal, Walls::Solid, 7);

  // snake.js:41 — three segments, centre, trailing to the left, direction right.
  REQUIRE(b.length() == kStartLen);
  REQUIRE(b.head() == Coord{kCols / 2, kRows / 2});
  REQUIRE(b.body()[1] == Coord{(kCols / 2) - 1, kRows / 2});
  REQUIRE(b.body()[2] == Coord{(kCols / 2) - 2, kRows / 2});
  REQUIRE(b.direction() == Dir::Right);
  REQUIRE(b.state() == State::Running);
  REQUIRE(b.score() == 0);
}

TEST_CASE("a step moves the head one cell and drags the tail after it",
          "[snake][rules]") {
  Board b = fixture(snake_at(10, 5));
  const int len_before = b.length();

  one_step(b);

  REQUIRE(heads(b) == Coord{11, 5});
  REQUIRE(b.length() == len_before);
  // The cell the tail left is free again, which is the whole reason the
  // occupancy grid is maintained incrementally rather than rebuilt.
  REQUIRE_FALSE(b.occupied(Coord{8, 5}));
  REQUIRE(b.occupied(Coord{11, 5}));
}

TEST_CASE("eating extends the snake on the FOLLOWING step, not the step that ate",
          "[snake][rules]") {
  // Deferred growth, snake.js:54 — eating sets `growing`, and the tail is kept
  // on the next move. It is not cosmetic: while the flag is set the tail cell
  // stays occupied, which the self-collision case below depends on.
  Board b = fixture(snake_at(10, 5), Coord{11, 5});
  REQUIRE(b.length() == 3);

  const auto ate = one_step(b);
  REQUIRE(ate.eaten == 1);
  REQUIRE(b.eaten() == 1);
  REQUIRE(b.score() == kFoodScore);
  REQUIRE(b.length() == 3);  // not yet

  one_step(b);
  REQUIRE(b.length() == 4);  // now
}

TEST_CASE("the score is ten per food and nothing else", "[snake][rules]") {
  // game.js:148 — flat +10. No length bonus, no speed bonus, no combo.
  Board b = fixture(snake_at(10, 5), Coord{11, 5});
  one_step(b);
  REQUIRE(b.score() == 10);
  REQUIRE(b.score() == kFoodScore * b.eaten());
}

// ─── Self-collision, and the exception that makes the game playable ─────────

// ⚠ The next two cases share ONE fixture and differ by ONE flag, which is the
// whole point of them. A C-shape whose tail has curled back to sit directly
// ahead of the head:
//
//        10  11  12
//    5    #   H   T        head (11,5) heading Right, tail (12,5)
//    6    #   #   #        body: (11,5) (10,5) (10,6) (11,6) (12,6) (12,5)
//
// Stepping Right lands on the tail. Whether that is the best move on the board
// or instant death depends entirely on whether the tail is about to move off —
// and NOTHING else about the position changes between the two cases.
namespace {
const std::vector<Coord> kTailAhead{{11, 5}, {10, 5}, {10, 6},
                                    {11, 6}, {12, 6}, {12, 5}};
}  // namespace

TEST_CASE("moving into the cell the tail is vacating is legal", "[snake][rules]") {
  // The standard way to survive a long body, and the rule a naive port gets
  // backwards — a game that kills you for following your own tail is a game you
  // cannot play well. The reference reaches the same answer by popping before it
  // scans (snake.js:54, :130).
  Board b(Level::Normal, Walls::Solid, 5);
  b.load(kTailAhead, Coord{0, 0}, 0, /*growing=*/false);
  REQUIRE(b.direction() == Dir::Right);
  REQUIRE(b.occupied(Coord{12, 5}));

  const auto r = one_step(b);

  REQUIRE_FALSE(r.died);
  REQUIRE(b.state() == State::Running);
  REQUIRE(heads(b) == Coord{12, 5});
}

TEST_CASE("moving into the tail while growing is fatal", "[snake][rules]") {
  // Identical position, growing. The tail is held this step, so the cell the
  // previous case moved safely into is occupied by a segment that is not going
  // anywhere. This is the observable consequence of deferred growth.
  Board b(Level::Normal, Walls::Solid, 5);
  b.load(kTailAhead, Coord{0, 0}, 0, /*growing=*/true);
  const int len_before = b.length();
  const Coord head_before = b.head();

  const auto r = one_step(b);

  REQUIRE(r.died);
  REQUIRE(b.state() == State::Lost);
  // ⚠ A fatal step commits NOTHING — no half-applied move with the tail already
  // gone. The board a player is left looking at is the one they died from.
  REQUIRE(b.length() == len_before);
  REQUIRE(b.head() == head_before);
}

TEST_CASE("running into the middle of your own body is fatal", "[snake][rules]") {
  // Not the tail and not a wall: a turn straight into a flank. Same C-shape read
  // the other way round, so the head is heading Left with its own middle
  // directly below it.
  Board b = fixture({{11, 5}, {12, 5}, {12, 6}, {11, 6}, {10, 6}, {10, 5}});
  REQUIRE(b.direction() == Dir::Left);
  const int len_before = b.length();

  REQUIRE(b.turn(Dir::Down));  // (11,6) is body index 3 — not the tail
  const auto r = one_step(b);

  REQUIRE(r.died);
  REQUIRE(b.state() == State::Lost);
  REQUIRE(b.length() == len_before);
}

// ─── Walls: both modes, all four edges ──────────────────────────────────────

TEST_CASE("Walls::Solid kills at every edge", "[snake][rules][walls]") {
  struct Case {
    std::vector<Coord> body;
    Dir go;
  };
  // Each snake sits one cell from an edge, pointing along it, and turns out.
  const Case cases[]{
      {{{5, 0}, {4, 0}, {3, 0}}, Dir::Up},
      {{{5, kRows - 1}, {4, kRows - 1}, {3, kRows - 1}}, Dir::Down},
      {{{0, 5}, {0, 4}, {0, 3}}, Dir::Left},
      {{{kCols - 1, 5}, {kCols - 1, 4}, {kCols - 1, 3}}, Dir::Right},
  };

  for (const auto& c : cases) {
    Board b(Level::Normal, Walls::Solid, 99);
    b.load(c.body, Coord{kCols / 2, kRows / 2});
    REQUIRE(b.turn(c.go));
    const auto r = one_step(b);
    REQUIRE(r.died);
    REQUIRE(b.state() == State::Lost);
  }
}

TEST_CASE("Walls::Wrap carries the head to the far side at every edge",
          "[snake][rules][walls]") {
  // The half of term-game#6 the reference does not have at all: snake.js:117 is
  // unconditionally fatal and no wrap mode exists there.
  struct Case {
    std::vector<Coord> body;
    Dir go;
    Coord lands;
  };
  const Case cases[]{
      {{{5, 0}, {4, 0}, {3, 0}}, Dir::Up, {5, kRows - 1}},
      {{{5, kRows - 1}, {4, kRows - 1}, {3, kRows - 1}}, Dir::Down, {5, 0}},
      {{{0, 5}, {0, 4}, {0, 3}}, Dir::Left, {kCols - 1, 5}},
      {{{kCols - 1, 5}, {kCols - 1, 4}, {kCols - 1, 3}}, Dir::Right, {0, 5}},
  };

  for (const auto& c : cases) {
    Board b(Level::Normal, Walls::Wrap, 99);
    b.load(c.body, Coord{kCols / 2, kRows / 2});
    REQUIRE(b.turn(c.go));
    const auto r = one_step(b);
    REQUIRE_FALSE(r.died);
    REQUIRE(b.state() == State::Running);
    REQUIRE(b.head() == c.lands);
  }
}

TEST_CASE("a wrapped snake still knows which way it is going",
          "[snake][rules][walls]") {
  // load() derives the heading from the head->neck vector, which points the
  // wrong way across a seam. Without the correction a wrapped fixture would
  // silently be pointing backwards, and every case built on one would be
  // asserting about a different game.
  const std::vector<Coord> across_the_seam{{0, 5}, {kCols - 1, 5}, {kCols - 2, 5}};
  Board b(Level::Normal, Walls::Wrap, 5);
  b.load(across_the_seam, Coord{10, 10});
  REQUIRE(b.direction() == Dir::Right);
}

// ─── The direction queue ────────────────────────────────────────────────────

TEST_CASE("two turns inside one step interval both take effect, in order",
          "[snake][input]") {
  // ⚠ THE BUG term-game#6 NAMES. The reference keeps one slot (snake.js:14), so the
  // second key of a fast double-tap overwrites the first and the intermediate
  // turn never happens.
  Board b = fixture(snake_at(10, 5));
  REQUIRE(b.direction() == Dir::Right);

  REQUIRE(b.turn(Dir::Up));
  REQUIRE(b.turn(Dir::Right));
  REQUIRE(b.queued().size() == 2);

  one_step(b);
  REQUIRE(b.direction() == Dir::Up);
  REQUIRE(b.head() == Coord{10, 4});

  one_step(b);
  REQUIRE(b.direction() == Dir::Right);
  REQUIRE(b.head() == Coord{11, 4});
}

TEST_CASE("a reversal is judged against the QUEUED direction, not the applied one",
          "[snake][input]") {
  // Heading Right, queue Up. Down is now a reversal of where the snake WILL be
  // going, and taking it would drive the head straight back through the neck one
  // step later.
  //
  // ⚠ The reference accepts this (snake.js:79 compares with `this.direction`,
  // still Right at that moment). It survives there only because its single slot
  // then DISCARDS the Up — so the bug is masked by the other bug.
  Board b = fixture(snake_at(10, 5));

  REQUIRE(b.turn(Dir::Up));
  REQUIRE_FALSE(b.turn(Dir::Down));
  REQUIRE(b.queued().size() == 1);

  one_step(b);
  REQUIRE(b.direction() == Dir::Up);
}

TEST_CASE("a reversal into the neck is refused", "[snake][input]") {
  Board b = fixture(snake_at(10, 5));
  REQUIRE(b.direction() == Dir::Right);
  REQUIRE_FALSE(b.turn(Dir::Left));
  REQUIRE(b.queued().empty());
}

TEST_CASE("turning where you are already going is refused", "[snake][input]") {
  // snake.js:83 refuses it too. Worth pinning because it is what keeps the
  // two-deep queue from filling with repeats of the current heading and
  // swallowing a real turn.
  Board b = fixture(snake_at(10, 5));
  REQUIRE_FALSE(b.turn(Dir::Right));
  REQUIRE(b.queued().empty());
}

TEST_CASE("a third turn is dropped rather than displacing a queued one",
          "[snake][input]") {
  Board b = fixture(snake_at(10, 5));
  REQUIRE(b.turn(Dir::Up));
  REQUIRE(b.turn(Dir::Left));
  REQUIRE_FALSE(b.turn(Dir::Down));  // legal after Left, but the queue is full
  REQUIRE(b.queued().size() == 2);
}

// ─── The speed curve ────────────────────────────────────────────────────────

TEST_CASE("the speed curve matches the reference table exactly",
          "[snake][clock]") {
  // state.js:23 — easy 120/-2, normal 100/-3, hard 80/-4, floored at 30.
  REQUIRE(interval_ms(Level::Easy, 0) == 120);
  REQUIRE(interval_ms(Level::Normal, 0) == 100);
  REQUIRE(interval_ms(Level::Hard, 0) == 80);

  REQUIRE(interval_ms(Level::Easy, 1) == 118);
  REQUIRE(interval_ms(Level::Normal, 1) == 97);
  REQUIRE(interval_ms(Level::Hard, 1) == 76);

  // The floor is reached at 45 / 24 / 13 foods respectively.
  REQUIRE(interval_ms(Level::Easy, 44) == 32);
  REQUIRE(interval_ms(Level::Easy, 45) == kFloorMs);
  REQUIRE(interval_ms(Level::Normal, 23) == 31);
  REQUIRE(interval_ms(Level::Normal, 24) == kFloorMs);
  REQUIRE(interval_ms(Level::Hard, 12) == 32);
  REQUIRE(interval_ms(Level::Hard, 13) == kFloorMs);
}

TEST_CASE("the interval never falls below the floor, however much is eaten",
          "[snake][clock]") {
  // Without the clamp this goes negative, and a non-positive interval makes
  // tick()'s while loop spin forever rather than merely play fast.
  for (const Level l : kLevels) {
    for (int eaten = 0; eaten < 500; ++eaten) {
      REQUIRE(interval_ms(l, eaten) >= kFloorMs);
    }
  }
}

TEST_CASE("the board's interval follows what it has eaten", "[snake][clock]") {
  // Derived from `eaten` rather than decremented in place, so a difficulty
  // change cannot leave the old curve's accumulated decrease applied to the new
  // start value — which is exactly what state.js:87 does.
  Board b = fixture(snake_at(10, 5), Coord{11, 5});
  REQUIRE(b.interval_ms() == 100);
  one_step(b);
  REQUIRE(b.eaten() == 1);
  REQUIRE(b.interval_ms() == 97);
}

// ─── The clock: the epic's own claim ────────────────────────────────────────

TEST_CASE("no step happens before the interval has elapsed", "[snake][clock]") {
  Board b = fixture(snake_at(10, 5));
  const auto r = b.tick(ms(99));
  REQUIRE(r.steps == 0);
  REQUIRE(b.head() == Coord{10, 5});
}

TEST_CASE("the remainder is carried, not discarded", "[snake][clock]") {
  // ⚠ THE DIVERGENCE FROM game.js:78. There, a step ASSIGNS the frame's
  // timestamp, throwing away the overshoot — so 100 ms of interval driven by
  // 60 ms frames yields a step every 120 ms and the published table is a lie.
  // Here 60 + 60 = 120 pays for one step and banks 20 ms, so the next step needs
  // only 80 ms more.
  Board b = fixture(snake_at(10, 5));

  REQUIRE(b.tick(ms(60)).steps == 0);
  REQUIRE(b.tick(ms(60)).steps == 1);   // 120 elapsed, 20 banked
  REQUIRE(b.tick(ms(60)).steps == 0);   // 80 banked, still short of 100
  REQUIRE(b.tick(ms(60)).steps == 1);   // 140 banked -> one step, 40 left
}

TEST_CASE("a long delta pays for every step it covers", "[snake][clock]") {
  Board b = fixture(snake_at(3, 5), Coord{kCols - 1, kRows - 1});
  const auto r = b.tick(ms(350));  // 3 x 100 with 50 left over
  REQUIRE(r.steps == 3);
  REQUIRE(b.head() == Coord{6, 5});
}

TEST_CASE(
    "the same elapsed time produces the same board however many frames carried "
    "it",
    "[snake][clock][determinism]") {
  // ⚠ THE EPIC'S ACCEPTANCE, in the form a headless case can hold it: what the
  // player sees must be a function of elapsed time, not of frame count. This is
  // what termforge #58 made impossible before it was fixed.
  //
  // ⚠ Compared at a MATCHED ELAPSED INSTANT, not merely at the end. 2048's tween
  // case passed against a broken advance() because finish() reconciled the
  // endpoint, so every chunking agreed no matter what happened in between. The
  // total here is 610 ms, which is deliberately NOT a multiple of the 100 ms
  // interval — a whole number of intervals would agree even if the remainder
  // were being discarded.
  const auto total = ms(610);

  Board one = fixture(snake_at(3, 5));
  one.tick(total);

  Board many = fixture(snake_at(3, 5));
  // 37 equal slices of the same span. Deliberately not 60 Hz ticks: a chunk size
  // that divides the interval evenly would hide a rounding bug.
  for (int i = 0; i < 37; ++i) {
    many.tick(std::chrono::duration<double>{total.count() / 37.0});
  }

  Board sixty = fixture(snake_at(3, 5));
  int n = 0;
  while (n * kTick.count() < total.count()) {
    sixty.tick(kTick);
    ++n;
  }

  REQUIRE(one.head() == Coord{9, 5});  // 6 steps in 610 ms at 100 ms
  REQUIRE(many.head() == one.head());
  REQUIRE(many.length() == one.length());
  // The 60 Hz run overshoots `total` by at most one tick, so it may have taken
  // the 7th step. It must be at 6 or 7 — never 5, which is what a discarded
  // remainder would produce.
  REQUIRE((sixty.head() == Coord{9, 5} || sixty.head() == Coord{10, 5}));
}

TEST_CASE("the step that kills is still counted as a step", "[snake][clock]") {
  // ⚠ board.cpp counts steps BEFORE it can decide the step was fatal, and
  // claimed in a comment that this mattered. It did not: moving the increment
  // below the death returns left every case green, so the claim was decoration
  // until this case existed. Either the contract is asserted or the comment goes
  // — and `steps` is what the frame-rate cases above are written in terms of, so
  // it is worth having mean one thing in every outcome.
  Board b(Level::Normal, Walls::Solid, 3);
  b.load(snake_at(kCols - 1, 5), Coord{0, 0});

  const auto r = b.tick(ms(150));
  REQUIRE(r.died);
  REQUIRE(r.steps == 1);
}

TEST_CASE("a finished board consumes no more time", "[snake][clock]") {
  Board b(Level::Normal, Walls::Solid, 3);
  b.load(snake_at(kCols - 1, 5), Coord{0, 0});
  const auto died = b.tick(ms(1000));
  REQUIRE(died.died);
  REQUIRE(b.state() == State::Lost);

  const auto after = b.tick(ms(1000));
  REQUIRE(after.steps == 0);
  REQUIRE_FALSE(after.died);
}

// ─── Food ───────────────────────────────────────────────────────────────────

TEST_CASE("food never spawns on the snake", "[snake][spawn]") {
  // food.js:13 gets this right; what it gets wrong is HOW (see the next case).
  // Swept over many respawns because a single spawn on a 3-cell snake would pass
  // by luck alone.
  Board b(Level::Normal, Walls::Wrap, 20250730);
  for (int i = 0; i < 400; ++i) {
    REQUIRE_FALSE(b.occupied(b.food()));
    b.tick(ms(b.interval_ms()));
    if (b.finished()) break;
  }
}

TEST_CASE("a board with one free cell puts the food in it", "[snake][spawn]") {
  // The tightest case the rejection sampler survives, and the one where its
  // expected number of draws is the whole board.
  std::vector<Coord> body;
  for (int y = 0; y < kRows; ++y) {
    for (int x = 0; x < kCols; ++x) {
      if (x == kCols - 1 && y == kRows - 1) continue;  // leave one free
      body.push_back(Coord{x, y});
    }
  }
  // Head-first ordering: reverse so the head is a cell with somewhere to go.
  std::vector<Coord> head_first(body.rbegin(), body.rend());

  Board b(Level::Normal, Walls::Solid, 11);
  b.load(head_first, Coord{kCols - 1, kRows - 1});
  REQUIRE(b.length() == kCells - 1);
  REQUIRE_FALSE(b.occupied(Coord{kCols - 1, kRows - 1}));
}

TEST_CASE("filling the board is a win, not a hang", "[snake][spawn]") {
  // ⚠ THE DIVERGENCE FROM food.js:13. There the spawn loop draws random cells
  // until one misses the snake, with no full-board case at all — so a snake that
  // fills the grid spins forever. This repo has met that exact shape before:
  // restoring minesweeper's reference mine placement makes test/14minesweeper
  // HANG rather than fail, which is a much worse failure to diagnose.
  //
  // ⚠ The fixture has to be MID-GROWTH, and working out why is worth the note:
  // with growth deferred, eating the last food does not fill the board. The step
  // that eats also releases the tail, so the length is unchanged and the cell the
  // tail left is free for the next food. The board only fills when the final food
  // is eaten while the previous one is still being grown — which is exactly what
  // `growing` on load() exists to express.
  //
  // Walk the board in boustrophedon order so consecutive cells are adjacent,
  // which is what makes this a legal snake rather than an arbitrary set.
  std::vector<Coord> serpent;
  for (int y = 0; y < kRows; ++y) {
    for (int i = 0; i < kCols; ++i) {
      const int x = (y % 2 == 0) ? i : (kCols - 1 - i);
      serpent.push_back(Coord{x, y});
    }
  }
  // Drop the last cell and make the snake head-first from the second-to-last.
  const Coord gap = serpent.back();
  serpent.pop_back();
  std::vector<Coord> head_first(serpent.rbegin(), serpent.rend());

  Board b(Level::Normal, Walls::Solid, 42);
  b.load(head_first, gap, /*eaten=*/0, /*growing=*/true);
  REQUIRE(b.length() == kCells - 1);
  // The gap is straight ahead, so no turn is needed and the case is about the
  // spawn rather than about steering.
  REQUIRE(b.direction() == Dir::Left);

  const auto r = b.tick(ms(1000));
  REQUIRE(r.eaten == 1);
  REQUIRE(r.won);
  REQUIRE(b.state() == State::Won);
  REQUIRE(b.finished());
}

// ─── Determinism ────────────────────────────────────────────────────────────

TEST_CASE("the same seed produces the same food sequence", "[snake][determinism]") {
  // ⚠ The reason board.hpp uses arcade/rng.hpp and never
  // std::uniform_int_distribution, whose engine-to-range mapping is
  // implementation-defined: this repo builds under libstdc++ AND libc++, so a
  // <random> version of this case would pass on GCC while being a lie on Clang.
  // The values are not written down here because they are not a specification —
  // what is specified is that two runs agree, and that is what is asserted.
  // ⚠ The trace records the FOOD as well as the head, and a first draft that
  // recorded only foods-as-eaten came back empty for both seeds: a snake left to
  // run straight in wrap mode circles one row forever and eats only if the food
  // happens to land on it. An empty trace compares equal to an empty trace, so
  // that draft asserted nothing at all while passing its first half. Hence a
  // scripted walk, and hence recording state per step rather than per meal.
  const auto run = [](std::uint64_t seed) {
    Board b(Level::Hard, Walls::Wrap, seed);
    std::vector<Coord> trace;
    for (int i = 0; i < 200 && !b.finished(); ++i) {
      if (i % 7 == 0) b.turn(kDirs[(i / 7) % 4]);  // refusals are deterministic too
      b.tick(ms(b.interval_ms()));
      trace.push_back(b.head());
      trace.push_back(b.food());
    }
    return trace;
  };

  REQUIRE_FALSE(run(0xC0FFEE).empty());
  REQUIRE(run(0xC0FFEE) == run(0xC0FFEE));
  REQUIRE(run(0xC0FFEE) != run(0xBEEF));
}

TEST_CASE("reset restores a fresh game at the new settings",
          "[snake][rules]") {
  Board b = fixture(snake_at(10, 5), Coord{11, 5});
  one_step(b);
  REQUIRE(b.eaten() == 1);

  b.reset(Level::Hard, Walls::Wrap, 77);
  REQUIRE(b.level() == Level::Hard);
  REQUIRE(b.walls() == Walls::Wrap);
  REQUIRE(b.eaten() == 0);
  REQUIRE(b.score() == 0);
  REQUIRE(b.length() == kStartLen);
  REQUIRE(b.interval_ms() == 80);
  REQUIRE(b.state() == State::Running);
  REQUIRE(b.queued().empty());
}

TEST_CASE("a finished board refuses turns", "[snake][input]") {
  Board b(Level::Normal, Walls::Solid, 3);
  b.load(snake_at(kCols - 1, 5), Coord{0, 0});
  b.tick(ms(200));
  REQUIRE(b.state() == State::Lost);
  REQUIRE_FALSE(b.turn(Dir::Up));
}
