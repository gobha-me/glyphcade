// Tetris' rules and its five clocks, with no terminal anywhere.
//
// ⚠ This file includes board.hpp and pieces.hpp and nothing else from the
// project. Neither includes a termforge header, so a case here CANNOT construct
// a Screen — it is prevented, not merely discouraged. Same discipline as
// test/14minesweeper, test/22twenty48 and test/25snake.
//
// The rules are cross-checked against the STANDARD SRS tables and against the
// HTML-Games reference's behaviour where the two agree, rather than derived
// from our own implementation. A table generated from the code under test
// proves only that the code is self-consistent.
//
// ⚠ Three traps for whoever extends this file:
//
//   1. `load()` takes the VISIBLE rows top-down. Board row 0 is hidden, so a
//      fixture row index and a board row index differ by kHiddenRows. Use
//      Board::filled with board coordinates and load() with visible ones, and
//      do not mix them in the same expression.
//   2. A piece's y is the TOP OF ITS BOX, not its topmost cell. I's rotation 0
//      is empty on box row 0, so an I at y == 5 has its cells on row 6. Every
//      off-by-one in this file has been that.
//   3. DAS is only armed under HoldSupport::Held. A case that presses and then
//      ticks under Discrete gets exactly one shift, forever, and that is the
//      behaviour rather than a broken fixture.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <string_view>
#include <vector>

#include <glyphcade/games/tetris/board.hpp>

namespace {

using namespace glyphcade::tetris;

[[nodiscard]] auto ms(int n) -> std::chrono::duration<double> {
  return std::chrono::duration<double>{static_cast<double>(n) / 1000.0};
}

constexpr std::chrono::duration<double> kTick{1.0 / 60.0};

// An empty visible field, for fixtures that only care about the piece.
[[nodiscard]] auto empty_rows() -> std::vector<std::string_view> {
  return std::vector<std::string_view>(kVisibleRows, "..........");
}

[[nodiscard]] auto fixture(std::vector<std::string_view> rows, Piece p, int rot,
                          int x, int y,
                          HoldSupport hold = HoldSupport::Held) -> Board {
  Board b(StartLevel::One, hold, 1234);
  REQUIRE(b.load(rows, p, rot, x, y));
  return b;
}

// ⚠ A COPY, not the span. preview() returns a view onto the board's own queue,
// so a case that held the span across a lock would be comparing the queue with
// itself and could never see a shift — the exact blindness gitea #55 lived in.
// ⚠ And it copies kPreview entries rather than three, so raising the preview
// depth widens these cases instead of leaving them checking a prefix.
[[nodiscard]] auto preview_of(const Board& b) -> std::array<Piece, kPreview> {
  std::array<Piece, kPreview> out{};
  std::ranges::copy(b.preview(), out.begin());
  return out;
}

[[nodiscard]] auto occupied_cells(const Board& b) -> int {
  int n = 0;
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      if (b.filled(c, r)) ++n;
    }
  }
  return n;
}

}  // namespace

// ── The piece tables ────────────────────────────────────────────────────────

TEST_CASE("every tetromino has four cells in every rotation",
          "[tetris][pieces]") {
  // The static_assert in pieces.hpp already decides this at compile time. The
  // runtime echo is what makes it visible to someone reading the tests — same
  // reason test/18audio-synth echoes kSfxIds' consteval check.
  for (const Piece p : kPieces) {
    for (int rot = 0; rot < kRotations; ++rot) {
      int n = 0;
      for (int r = 0; r < kBoxMax; ++r) {
        for (int c = 0; c < kBoxMax; ++c) {
          if (cell_at(p, rot, r, c)) ++n;
        }
      }
      REQUIRE(n == 4);
    }
  }
}

TEST_CASE("O is the only piece whose rotations are all identical",
          "[tetris][pieces]") {
  // O not rotating is expressed as DATA rather than as a branch in rotate().
  // If someone "tidies" that into an early return, this still passes — but if
  // they tidy the data instead, the game gains a piece that visibly jitters.
  for (const Piece p : kPieces) {
    bool all_same = true;
    for (int rot = 1; rot < kRotations && all_same; ++rot) {
      for (int r = 0; r < kBoxMax && all_same; ++r) {
        for (int c = 0; c < kBoxMax && all_same; ++c) {
          if (cell_at(p, rot, r, c) != cell_at(p, 0, r, c)) all_same = false;
        }
      }
    }
    REQUIRE(all_same == (p == Piece::O));
  }
}

TEST_CASE("both kick tables match the standard SRS offsets",
          "[tetris][pieces]") {
  // ⚠ Written out here from the STANDARD tables, not copied from pieces.hpp.
  // Two copies of the same typo would agree with each other perfectly, which is
  // the whole failure mode a ported table has.
  REQUIRE(kicks_for(Piece::T, 0, 1) ==
          KickSet{{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}});
  REQUIRE(kicks_for(Piece::T, 1, 0) ==
          KickSet{{{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}});
  REQUIRE(kicks_for(Piece::T, 2, 3) ==
          KickSet{{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}});
  REQUIRE(kicks_for(Piece::T, 3, 0) ==
          KickSet{{{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}});

  REQUIRE(kicks_for(Piece::I, 0, 1) ==
          KickSet{{{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}});
  REQUIRE(kicks_for(Piece::I, 1, 2) ==
          KickSet{{{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}}});
  REQUIRE(kicks_for(Piece::I, 2, 3) ==
          KickSet{{{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}}});
  REQUIRE(kicks_for(Piece::I, 3, 0) ==
          KickSet{{{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}});

  // I and JLSTZ must not be the same table. They differ, and a lookup that
  // returned one for the other would still rotate correctly in open space.
  REQUIRE(kicks_for(Piece::I, 0, 1) != kicks_for(Piece::T, 0, 1));
}

// ── Rotation and kicks ──────────────────────────────────────────────────────

TEST_CASE("a rotation in open space does not translate the piece",
          "[tetris][rotation]") {
  Board b = fixture(empty_rows(), Piece::T, 0, 4, 5);
  const int x = b.active().x;
  const int y = b.active().y;

  REQUIRE(b.rotate(1));
  REQUIRE(b.active().rot == 1);
  REQUIRE(b.active().x == x);
  REQUIRE(b.active().y == y);
}

TEST_CASE("a rotation against the left wall kicks right",
          "[tetris][rotation]") {
  // ⚠ THE CASE THAT PROVES THE Y-SIGN CONVERSION IS APPLIED. The tables are
  // y-up and the board is y-down; board.cpp applies `-y`, as game.js:293 does.
  // A build with the sign wrong still rotates fine in open space, so only a
  // kick that actually displaces the piece can see it.
  Board b = fixture(empty_rows(), Piece::T, 1, -1, 5);
  // Rotation 1 of T occupies box columns 1 and 2, so x == -1 is legal: the
  // leftmost occupied column is 0.
  REQUIRE(b.active().x == -1);

  // Rotating to 2 needs box column 0, which is off the board, so the first kick
  // that fits must move it right.
  REQUIRE(b.rotate(1));
  REQUIRE(b.active().rot == 2);
  REQUIRE(b.active().x == 0);
}

TEST_CASE("a kick that lifts the piece proves the y-sign conversion",
          "[tetris][rotation]") {
  // ⚠ THE CASE THE PREVIOUS ONE ONLY CLAIMED TO BE. "a rotation against the
  // left wall kicks right" asserts an x displacement, and every kick it can
  // reach has y == 0 — so flipping the sign in board.cpp's `a.y -= kick.y`
  // left the whole suite green. Found by mutation, not by reading, and the
  // comment there had confidently said otherwise.
  //
  // The tables are y-up and the board is y-down, exactly as game.js:293 states.
  // Only a kick with a NON-ZERO y can see the difference.
  //
  // Setup: a deep one-wide well with a horizontal I lying across its mouth. The
  // identity kick and the next three are all blocked by the well's shoulders,
  // so the transition resolves on {1, 2} — the LAST candidate in I's 0>1 set,
  // which lifts the piece two rows as it stands it up. With the sign flipped
  // that offset pushes it down into the floor instead, no candidate fits, and
  // the rotation is refused outright.
  auto rows = empty_rows();
  for (int r = 15; r < kVisibleRows; ++r) {
    rows[static_cast<std::size_t>(r)] = "####.#####";
  }
  Board b = fixture(rows, Piece::I, 0, 0, 12 + kHiddenRows);

  REQUIRE(b.rotate(1));
  REQUIRE(b.active().rot == 1);
  REQUIRE(b.active().x == 1);
  // UP two rows. The sign is the whole assertion.
  REQUIRE(b.active().y == 10 + kHiddenRows);
}

TEST_CASE("a piece that fits no kick does not rotate at all",
          "[tetris][rotation]") {
  // A T boxed in on both sides in a one-cell-wide slot.
  auto rows = empty_rows();
  // ⚠ THE WHOLE COLUMN, not a three-row pocket. SRS kick offsets move a piece
  // UP and DOWN as well as sideways, so a slot with open rows above or below it
  // is not a slot: the piece rotates into the gap and the case asserts nothing.
  // Both directions had to be closed before this went red.
  for (int r = 0; r < kVisibleRows; ++r) {
    rows[static_cast<std::size_t>(r)] = "###..#####";
  }
  Board b = fixture(rows, Piece::T, 1, 2, 10 + kHiddenRows);
  const int x = b.active().x;
  const int rot = b.active().rot;

  REQUIRE_FALSE(b.rotate(1));
  REQUIRE(b.active().rot == rot);
  REQUIRE(b.active().x == x);
}

// ── The 7-bag ───────────────────────────────────────────────────────────────

TEST_CASE("every seven spawned pieces contain each tetromino exactly once",
          "[tetris][bag]") {
  // ⚠ THE PROPERTY THAT MAKES IT A BAG rather than seven random draws. A
  // uniform sampler passes any "all seven appear eventually" check and fails
  // this one on the first bag.
  //
  // ⚠ THE SPAWN STREAM, not the preview. This case used to read [active] +
  // preview() as its first four entries, and gitea #55 is why that could not
  // fail: the preview was a dead-end copy, so those four were draws 4,1,2,3 —
  // a PERMUTATION of the first four — and the check below counts a multiset
  // rather than an order. It passed by arithmetic accident of the exact
  // disconnection it should have caught. The bag is a property of what SPAWNS.
  //
  // ⚠ load() clears the stack and deliberately leaves m_bag and m_next alone,
  // which is the only reason this can run thirteen locks: pieces spawn at rot 0
  // into columns 3-6 and never complete a row, so an un-cleared board tops out
  // around the tenth drop and the loop would assert against a dead board.
  Board b(StartLevel::One, HoldSupport::Held, 99);
  std::vector<Piece> seen;
  seen.push_back(b.active().piece);

  while (static_cast<int>(seen.size()) < 2 * kPieceCount) {
    REQUIRE(b.load(empty_rows(), b.active().piece, 0, 3, kHiddenRows));
    b.hard_drop();
    // ⚠ Without this a top-out would be masked by the next load(), and every
    // later entry would be a piece that never spawned.
    REQUIRE(b.state() == State::Running);
    seen.push_back(b.active().piece);
  }

  for (int bag = 0; bag < 2; ++bag) {
    std::array<int, kPieceCount> count{};
    for (int i = 0; i < kPieceCount; ++i) {
      ++count[static_cast<std::size_t>(
          seen[static_cast<std::size_t>((bag * kPieceCount) + i)])];
    }
    for (const int n : count) REQUIRE(n == 1);
  }
}

TEST_CASE("the same seed produces the same sequence", "[tetris][bag]") {
  Board a(StartLevel::One, HoldSupport::Held, 4242);
  Board b(StartLevel::One, HoldSupport::Held, 4242);
  Board c(StartLevel::One, HoldSupport::Held, 4243);

  REQUIRE(a.active().piece == b.active().piece);
  bool same_preview = true;
  for (int i = 0; i < kPreview; ++i) {
    if (a.preview()[static_cast<std::size_t>(i)] !=
        b.preview()[static_cast<std::size_t>(i)]) {
      same_preview = false;
    }
  }
  REQUIRE(same_preview);

  // A different seed must be able to differ. Not asserted as "differs", because
  // two bags can legitimately begin with the same piece.
  const bool differs_somewhere =
      a.active().piece != c.active().piece ||
      a.preview()[0] != c.preview()[0] || a.preview()[1] != c.preview()[1] ||
      a.preview()[2] != c.preview()[2];
  REQUIRE(differs_somewhere);
}

// ── The preview ─────────────────────────────────────────────────────────────
//
// ⚠ gitea #55. m_next was filled once by reset() and read by nothing except
// hold(), while every spawn drew a piece straight from the bag — so the NEXT
// panel advertised three pieces the player would never receive, and it never
// moved. Neither half of that is observable from preview() ALONE, which is why
// a file with a bag case and a determinism case still missed it: it takes
// preview() read BEFORE a lock, compared against active() read AFTER it.

TEST_CASE("the preview is the next three spawns, in order",
          "[tetris][preview]") {
  Board b(StartLevel::One, HoldSupport::Held, 99);

  // Ten locks, not one. A shift that is right on the first lock and wrong on
  // the second — a refill that writes the wrong slot, say — needs more than one
  // sample to show itself.
  for (int i = 0; i < 10; ++i) {
    // ⚠ Clears the stack, not the queue. Same reason as the bag case above.
    REQUIRE(b.load(empty_rows(), b.active().piece, 0, 3, kHiddenRows));
    const auto before = preview_of(b);

    b.hard_drop();
    REQUIRE(b.state() == State::Running);
    REQUIRE(b.clearing().empty());  // the plain-lock spawn path, not the clear

    REQUIRE(b.active().piece == before[0]);  // the head IS what spawned
    REQUIRE(b.preview()[0] == before[1]);    // and the queue moved up by one
    REQUIRE(b.preview()[1] == before[2]);
  }
}

TEST_CASE("a line clear spawns the preview's head too", "[tetris][preview]") {
  // ⚠ A SECOND SPAWN SITE, and a second chance to get it wrong. lock_active
  // awards and RETURNS while the rows are still vanishing, so the piece that
  // follows a clear comes from clear_full_rows — a different call. A fix
  // applied to one and not the other is invisible to every other case here.
  auto rows = empty_rows();
  rows[19] = ".#########";
  // ⚠ x == -2. Trap 2, as at "a completed row clears after the freeze".
  Board b = fixture(rows, Piece::I, 1, -2, kHiddenRows);
  const auto before = preview_of(b);

  b.hard_drop();
  REQUIRE(b.clearing().size() == 1);
  // ⚠ NOTHING has been consumed yet, and that is the assertion. A queue that
  // advanced here as well would advance TWICE per line clear.
  REQUIRE(b.active().piece == Piece::I);
  REQUIRE(preview_of(b) == before);

  b.tick(ms(kLineClearMs + 10));
  REQUIRE(b.clearing().empty());
  REQUIRE(b.active().piece == before[0]);
  REQUIRE(b.preview()[0] == before[1]);
  REQUIRE(b.preview()[1] == before[2]);
}

TEST_CASE("holding into an empty slot consumes the preview's head",
          "[tetris][preview][hold]") {
  // The one path that always moved the queue, even before #55 — and therefore
  // why the panel appeared to lurch exactly once per game and then freeze
  // again, rather than reading as broken from the first frame.
  Board b(StartLevel::One, HoldSupport::Held, 11);
  const Piece current = b.active().piece;
  const auto before = preview_of(b);

  REQUIRE(b.hold());
  REQUIRE(b.active().piece == before[0]);
  REQUIRE(b.held() != nullptr);
  REQUIRE(*b.held() == current);
  REQUIRE(b.preview()[0] == before[1]);
  REQUIRE(b.preview()[1] == before[2]);
}

TEST_CASE("a hold with a piece already in the slot leaves the queue alone",
          "[tetris][preview][hold]") {
  // ⚠ THE ASYMMETRY, as a rule. The first hold takes a piece OUT of the stream;
  // every later one is a swap with the slot and takes nothing. Advancing on
  // both paths silently eats one piece per hold — which no bag case can see,
  // because a bag with a piece missing from the middle still counts as a bag
  // seven entries later.
  Board b(StartLevel::One, HoldSupport::Held, 11);
  REQUIRE(b.hold());  // fills the slot, consuming the head
  b.hard_drop();      // re-arms hold, and spawns
  REQUIRE(b.can_hold());

  const Piece current = b.active().piece;
  const Piece swapped_in = *b.held();
  const auto before = preview_of(b);

  REQUIRE(b.hold());
  REQUIRE(b.active().piece == swapped_in);
  REQUIRE(*b.held() == current);
  REQUIRE(preview_of(b) == before);
}

TEST_CASE("a fresh board's active piece is the head of its own stream",
          "[tetris][preview][bag]") {
  // ⚠ PINNED VALUES, deliberately, and the only case here that can see reset()
  // burning a draw. Before #55 reset() filled the preview with three draws and
  // then spawned a FOURTH, and that extra draw is invisible to every structural
  // property in this file: the bag case counts a set, and the window case above
  // starts one lock too late to see the opening. rng.hpp says a pinned sequence
  // surfacing a generator change is the tripwire working, and test/14minesweeper
  // pins placements against fixed seeds for the same reason.
  //
  // Seed 1234 is the fixture() seed, so this also records what every fixture
  // board's queue holds — see the note on load() in board.hpp.
  Board b(StartLevel::One, HoldSupport::Held, 1234);
  REQUIRE(b.active().piece == Piece::J);
  REQUIRE(b.preview()[0] == Piece::Z);
  REQUIRE(b.preview()[1] == Piece::I);
  REQUIRE(b.preview()[2] == Piece::S);
}

// ── Gravity, and the clock ──────────────────────────────────────────────────

TEST_CASE("the gravity table is the reference's code, not its README",
          "[tetris][clock]") {
  // ⚠ REFERENCE DEFECT 9. The README's table says level 10 is 196 ms; its own
  // getDropInterval computes 1000 * 0.85^(level-1), which is 231 at level 10
  // and 196 at level 11. Same family as Snake's "the published speed table is
  // intent, not behaviour" — and the reason the table was generated from code.
  REQUIRE(gravity_ms(1) == 1000);
  REQUIRE(gravity_ms(10) == 231);
  REQUIRE(gravity_ms(11) == 196);
  // The floor, and everything past it.
  REQUIRE(gravity_ms(20) == kGravityFloorMs);
  REQUIRE(gravity_ms(50) == kGravityFloorMs);
  REQUIRE(gravity_ms(0) == 1000);
}

TEST_CASE("the gravity remainder is carried, not discarded", "[tetris][clock]") {
  // ⚠ THE DIVERGENCE FROM game.js:347, which ASSIGNS the frame timestamp rather
  // than subtracting the interval — so every drop rounds up to the next frame
  // boundary. At level 1 the interval is 1000 ms.
  Board b = fixture(empty_rows(), Piece::T, 0, 4, 0);
  REQUIRE(b.gravity_interval_ms() == 1000);

  REQUIRE(b.tick(ms(600)).steps == 0);
  REQUIRE(b.tick(ms(600)).steps == 1);  // 1200 elapsed, 200 banked
  REQUIRE(b.tick(ms(600)).steps == 0);  // 800 banked, still short
  REQUIRE(b.tick(ms(600)).steps == 1);  // 1400 banked -> one step, 400 left
}

TEST_CASE("the same elapsed time drops the same distance however many frames "
          "carried it",
          "[tetris][clock][determinism]") {
  // ⚠ The epic's acceptance in the form a headless case can hold it: what the
  // player sees is a function of elapsed time, not of frame count.
  //
  // ⚠ 4300 ms is deliberately NOT a multiple of the 1000 ms interval — a whole
  // number of intervals would agree even if the remainder were discarded — and
  // the chunk count is 37 rather than something that divides it, for the same
  // reason. Both anti-patterns come from test/25snake, where 2048's tween case
  // had already shown that reconciling at the endpoint hides everything in
  // between.
  const auto total = ms(4300);

  Board one = fixture(empty_rows(), Piece::T, 0, 4, 0);
  one.tick(total);

  Board many = fixture(empty_rows(), Piece::T, 0, 4, 0);
  for (int i = 0; i < 37; ++i) {
    many.tick(std::chrono::duration<double>{total.count() / 37.0});
  }

  Board sixty = fixture(empty_rows(), Piece::T, 0, 4, 0);
  int n = 0;
  while (n * kTick.count() < total.count()) {
    sixty.tick(kTick);
    ++n;
  }

  REQUIRE(one.active().y == 4);
  REQUIRE(many.active().y == one.active().y);
  // 60 Hz ticks cannot land exactly on 4300 ms, so it is one step either side.
  REQUIRE((sixty.active().y == 4 || sixty.active().y == 5));
}

TEST_CASE("a finished board consumes no more time", "[tetris][clock]") {
  // Every row but the top one, so the piece has somewhere to lock and the NEXT
  // spawn has nowhere to go. Filling every row instead would leave load() with
  // nowhere to put the active piece either, and the fixture would fail rather
  // than the game topping out.
  auto rows = empty_rows();
  for (int r = 2; r < kVisibleRows; ++r) {
    rows[static_cast<std::size_t>(r)] = "##########";
  }
  Board b = fixture(rows, Piece::T, 0, 4, kHiddenRows);
  b.hard_drop();
  REQUIRE(b.state() == State::ToppedOut);

  const auto r = b.tick(ms(10000));
  REQUIRE(r.steps == 0);
  REQUIRE(r.lines == 0);
}

// ── Lock delay ──────────────────────────────────────────────────────────────

TEST_CASE("a hard drop locks immediately, without the lock delay",
          "[tetris][lock]") {
  // The one path that skips the delay entirely: the player has said they are
  // done with this piece, so there is no slide window to grant.
  auto rows = empty_rows();
  rows[19] = "##########";
  Board b = fixture(rows, Piece::T, 0, 4, 16 + kHiddenRows);
  b.hard_drop();
  REQUIRE(occupied_cells(b) == 14);  // 10 floor + 4 piece
}

TEST_CASE("the lock clock starts when the piece lands, not at the next gravity "
          "interval",
          "[tetris][lock]") {
  // ⚠ REFERENCE DEFECT: game.js only enters its locking branch INSIDE the drop
  // -interval check, so at level 1 a piece that lands just after a gravity step
  // waits up to a full second before its 500 ms lock delay even begins. Ours
  // starts it the moment the piece is grounded.
  auto rows = empty_rows();
  rows[19] = "##########";
  Board b = fixture(rows, Piece::T, 0, 4, 16 + kHiddenRows);

  // ⚠ Driven at 60 Hz rather than in one 1000 ms tick, and that is not
  // cosmetic. The lock clock is only credited time from ticks that BEGAN with
  // the piece grounded, so a single coarse tick that both lands the piece and
  // runs out its delay would lock it with no slide window — the behaviour a
  // player would feel as a piece that cannot be adjusted.
  int elapsed = 0;
  while (occupied_cells(b) == 10 && elapsed < 3000) {
    b.tick(kTick);
    elapsed += 17;
  }
  // Landed after ~1000 ms of gravity, locked ~500 ms later. Generous bounds:
  // what is being pinned is that the delay HAPPENS, not its exact rounding.
  REQUIRE(elapsed > 1400);
  REQUIRE(elapsed < 1700);
  REQUIRE(occupied_cells(b) == 14);
}

TEST_CASE("one coarse tick cannot both land a piece and expire its lock delay",
          "[tetris][lock]") {
  // ⚠ THE CASE THE 60 Hz ONES CANNOT BE. At a 1/60 s dt the difference between
  // crediting the landing tick and not crediting it is one frame, which no
  // reasonable bound can see — so the rule was unpinned until a mutation said
  // so. Driven coarsely it is stark: 1600 ms contains a 1000 ms gravity step
  // AND three lock delays, and a piece that landed inside it must still get its
  // full slide window afterwards.
  //
  // This is the fixed-timestep contract paying off. dt is never actually 1600 ms
  // in production — the Shell runs at 60 Hz and clamps — but a rule that is only
  // true at one dt is a rule that will surprise someone.
  auto rows = empty_rows();
  rows[19] = "##########";
  Board b = fixture(rows, Piece::T, 0, 4, 16 + kHiddenRows);

  const auto landed = b.tick(ms(1600));
  REQUIRE(landed.steps == 1);
  REQUIRE_FALSE(landed.locked);
  REQUIRE(occupied_cells(b) == 10);

  // The very next tick begins with the piece already resting, so the clock runs.
  REQUIRE(b.tick(ms(kLockDelayMs + 10)).locked);
  REQUIRE(occupied_cells(b) == 14);
}

TEST_CASE("moving a grounded piece buys more lock delay, but only fifteen "
          "times",
          "[tetris][lock]") {
  auto rows = empty_rows();
  rows[19] = "##########";
  Board b = fixture(rows, Piece::T, 0, 4, 17 + kHiddenRows);
  // Already resting on the floor, so the lock clock is live from the first tick
  // — no gravity step to get out of the way first.
  REQUIRE(b.tick(kTick).locked == false);

  // Each shift resets the clock, so 400 ms of waiting never reaches 500.
  for (int i = 0; i < kMaxLockResets; ++i) {
    REQUIRE(b.tick(ms(400)).locked == false);
    const Shift dir = i % 2 == 0 ? Shift::Left : Shift::Right;
    REQUIRE(b.press_shift(dir));
    b.release_shift(dir);
  }

  // The sixteenth reset is refused, so the clock keeps running and it locks.
  static_cast<void>(b.press_shift(Shift::Left));
  REQUIRE(b.tick(ms(600)).locked);
}

// ── Line clears and scoring ─────────────────────────────────────────────────

TEST_CASE("a completed row clears after the freeze, and not during it",
          "[tetris][lines]") {
  // ⚠ REFERENCE DEFECT 5: its 300 ms freeze renders nothing at all, because
  // lineClearAnimProgress is never incremented. Ours keeps the rows ON the
  // board for the freeze so there is something to flash, then removes them.
  auto rows = empty_rows();
  rows[19] = ".#########";
  // ⚠ x == -2, not 0. I's rotation 1 occupies box COLUMN 2, so the box's left
  // edge sits two columns left of the cells. Trap 2 at the top of this file, in
  // its most expensive form: at x == 0 the piece is over column 2, lands on the
  // prefilled stack, and clears nothing at all.
  Board b = fixture(rows, Piece::I, 1, -2, kHiddenRows);

  b.hard_drop();
  REQUIRE(b.clearing().size() == 1);
  REQUIRE(b.filled(0, 19 + kHiddenRows));  // still there, mid-flash
  REQUIRE(b.clear_progress() == 0.0);

  b.tick(ms(kLineClearMs / 2));
  REQUIRE(b.clearing().size() == 1);
  REQUIRE(b.clear_progress() > 0.4);
  REQUIRE(b.clear_progress() < 0.6);

  const auto r = b.tick(ms(kLineClearMs));
  REQUIRE(r.lines == 0);  // scored at lock, not at removal
  REQUIRE(b.clearing().empty());
  REQUIRE_FALSE(b.filled(5, 19 + kHiddenRows));
  REQUIRE(b.lines() == 1);
}

TEST_CASE("the scoring table matches the reference's constants",
          "[tetris][score]") {
  // Four separate boards rather than four sections of one, so a case that
  // leaves state behind cannot make the next one pass.
  struct Case {
    int prefilled;
    int expect_lines;
    int expect_score;
  };
  // An I laid flat clears as many rows as are one-short beneath it.
  const Case cases[]{{1, 1, kScoreSingle}, {2, 2, kScoreDouble},
                     {3, 3, kScoreTriple}, {4, 4, kScoreTetris}};

  for (const Case& c : cases) {
    auto rows = empty_rows();
    for (int i = 0; i < c.prefilled; ++i) {
      rows[static_cast<std::size_t>(kVisibleRows - 1 - i)] = ".#########";
    }
    Board b = fixture(rows, Piece::I, 1, -2, kHiddenRows);
    b.hard_drop();
    REQUIRE(b.lines() == c.expect_lines);
    // Level 1 multiplier, plus the hard drop's own distance points, which is
    // why this is a >= on the line component rather than an ==.
    REQUIRE(b.score() >= c.expect_score);
    REQUIRE(b.score() < c.expect_score + (kVisibleRows * kScoreHardDrop) +
                            kComboBase + kComboIncrement);
  }
}

TEST_CASE("levelling up every ten lines changes the gravity interval",
          "[tetris][score]") {
  Board b(StartLevel::One, HoldSupport::Held, 7);
  REQUIRE(b.level() == 1);
  REQUIRE(b.gravity_interval_ms() == 1000);

  // Start level five is a different curve from the first line onward, which is
  // the whole reason the record key carries it.
  Board hard(StartLevel::Five, HoldSupport::Held, 7);
  REQUIRE(hard.level() == 5);
  REQUIRE(hard.gravity_interval_ms() == gravity_ms(5));
  REQUIRE(hard.gravity_interval_ms() < b.gravity_interval_ms());
}

// ── T-spins ─────────────────────────────────────────────────────────────────

// ⚠ ONE BOARD, ONE FINAL POSITION, TWO HISTORIES. Both cases below put the
// same T in the same cells of the same stack and clear the same two rows. The
// only difference is whether a rotation was the last thing that happened to it,
// which is the entire rule — and the reason a corner-count-only check like
// game.js:103-123 cannot express it.
//
// The slot is the classic one: a notch under an overhang, with the T arriving
// down the open column and turning into place. It was found by driving the
// model rather than by reasoning about kick tables, because a T-spin fixture
// that is subtly wrong looks exactly like a T-spin fixture that works.
namespace {

[[nodiscard]] auto tspin_rows() -> std::vector<std::string_view> {
  auto rows = empty_rows();
  rows[16] = "###..#####";
  rows[17] = "###..#####";
  rows[18] = "###...####";
  rows[19] = "####.#####";
  return rows;
}

}  // namespace

TEST_CASE("a T rotated into place is a T-spin", "[tetris][tspin]") {
  Board b = fixture(tspin_rows(), Piece::T, 0, 3, 17 + kHiddenRows);
  REQUIRE(b.rotate(1));
  REQUIRE(b.rotate(1));
  REQUIRE(b.active().rot == 2);

  b.hard_drop();
  REQUIRE(b.lines() == 2);
  // T-spin double at level 1, and nothing else: the drop distance is zero and
  // the combo counter is at its first clear, so this number is exact.
  REQUIRE(b.score() == kScoreTSpinDouble);
}

TEST_CASE("the same T in the same cells, without rotating, is not a T-spin",
          "[tetris][tspin]") {
  // ⚠ REFERENCE DEFECT 4, stated as a difference rather than as an absence.
  // Loaded straight into the position the case above rotated into, so the
  // corners it counts are identical — only the history is not.
  Board b = fixture(tspin_rows(), Piece::T, 2, 3, 17 + kHiddenRows);
  b.hard_drop();
  REQUIRE(b.lines() == 2);
  REQUIRE(b.score() == kScoreDouble);
  REQUIRE(b.score() < kScoreTSpinDouble);
}

// ── Hold ────────────────────────────────────────────────────────────────────

TEST_CASE("hold works on every piece, not once per game", "[tetris][hold]") {
  // ⚠ REFERENCE DEFECT 1, and the one a player would notice within a minute.
  // lockPiece ends with `canHold = false` (game.js:66) and nothing ever sets it
  // true again, so the reference lets you hold exactly once per game and then
  // greys the box out forever.
  Board b(StartLevel::One, HoldSupport::Held, 11);

  const Piece first = b.active().piece;
  REQUIRE(b.can_hold());
  REQUIRE(b.hold());
  REQUIRE(b.held() != nullptr);
  REQUIRE(*b.held() == first);
  REQUIRE_FALSE(b.can_hold());

  // A second hold on the same piece is refused — that is the real rule.
  REQUIRE_FALSE(b.hold());

  // Lock, and hold is available again.
  b.hard_drop();
  REQUIRE(b.can_hold());
  REQUIRE(b.hold());
}

TEST_CASE("a hold that would not fit is refused whole", "[tetris][hold]") {
  // ⚠ REFERENCE DEFECT 8: game.js:308-311 assigns the swapped-in piece with no
  // validity check, so in a high stack it materialises inside locked blocks.
  // ⚠ A COLUMN left open, not a row. The hold candidate is built at the spawn
  // position, so the stack has to block THAT — and a fixture that leaves the
  // top rows clear (the obvious way to make room for the active piece) leaves
  // the spawn clear too, and the hold succeeds. Only column 0 is free, and the
  // active piece is a vertical I standing in it.
  auto rows = empty_rows();
  for (int r = 0; r < kVisibleRows; ++r) {
    rows[static_cast<std::size_t>(r)] = ".#########";
  }
  Board b = fixture(rows, Piece::I, 1, -2, kHiddenRows);
  const Piece before = b.active().piece;
  const auto queue = preview_of(b);

  REQUIRE_FALSE(b.hold());
  REQUIRE(b.held() == nullptr);
  REQUIRE(b.active().piece == before);
  REQUIRE(b.can_hold());

  // ⚠ REFUSED WHOLE means the QUEUE too. hold() peeks at the preview's head to
  // build its candidate and advances the queue only after the fit check, so the
  // two touches straddle the guard; hoisting the advance above it would eat a
  // piece on every refusal, and every assertion above this line would still
  // pass.
  REQUIRE(preview_of(b) == queue);
}

// ── DAS, and the degraded arm ───────────────────────────────────────────────

TEST_CASE("a held shift waits for DAS and then repeats at ARR",
          "[tetris][das]") {
  // ⚠ Starts at 7, not mid-board. T's rotation 0 spans three columns from x, so
  // x bottoms out at 0 — and a run that walks into the wall loses its last
  // shift to the bounds check, which reads exactly like a broken ARR clock.
  Board b = fixture(empty_rows(), Piece::T, 0, 7, 5);
  REQUIRE(b.press_shift(Shift::Left));
  REQUIRE(b.active().x == 6);

  // Nothing until the DAS delay has elapsed.
  REQUIRE(b.tick(ms(kDasMs - 20)).shifts == 0);
  REQUIRE(b.active().x == 6);

  // Crossing it yields the first auto-shift.
  REQUIRE(b.tick(ms(30)).shifts == 1);
  REQUIRE(b.active().x == 5);

  // And then one per ARR. 160 ms is three whole 50 ms steps with 10 banked.
  REQUIRE(b.tick(ms(160)).shifts == 3);
  REQUIRE(b.active().x == 2);
}

TEST_CASE("releasing the key stops the repeat", "[tetris][das]") {
  Board b = fixture(empty_rows(), Piece::T, 0, 4, 5);
  REQUIRE(b.press_shift(Shift::Left));
  b.tick(ms(kDasMs + 10));
  const int moved = b.active().x;

  b.release_shift(Shift::Left);
  REQUIRE(b.tick(ms(1000)).shifts == 0);
  REQUIRE(b.active().x == moved);
}

TEST_CASE("releasing the other direction does not stop the one being held",
          "[tetris][das]") {
  // A player rolling from left to right presses right before releasing left.
  // The stale release must not cancel the live hold.
  Board b = fixture(empty_rows(), Piece::T, 0, 4, 5);
  REQUIRE(b.press_shift(Shift::Right));
  b.release_shift(Shift::Left);

  b.tick(ms(kDasMs + 10));
  REQUIRE(b.active().x > 5);
}

TEST_CASE("the most recent direction wins instead of both moving",
          "[tetris][das]") {
  // ⚠ REFERENCE DEFECT: game.js:376-417 tracks left and right independently and
  // auto-repeats BOTH while both are down, so holding two keys makes the piece
  // stutter in place instead of moving.
  Board b = fixture(empty_rows(), Piece::T, 0, 4, 5);
  REQUIRE(b.press_shift(Shift::Left));
  REQUIRE(b.press_shift(Shift::Right));

  b.tick(ms(kDasMs + 10));
  // Two presses moved it -1 then +1, so it is back at 4; the auto-repeat must
  // then go RIGHT, not oscillate.
  REQUIRE(b.active().x > 4);
}

TEST_CASE("without key release there is no auto-repeat at all",
          "[tetris][das][degraded]") {
  // ⚠ THE DEGRADATION CONTRACT AS A RULE, not as a rendering detail. A terminal
  // with no kitty keyboard protocol never delivers a release, so "held" and
  // "pressed again" are the same event and DAS is not expressible. Auto-
  // repeating anyway would slide the piece into a wall on a key the player let
  // go of a second ago.
  //
  // ⚠ This is also the ONLY arm anything in this container can reach: every
  // headless frame runs on FallbackDriver, whose capabilities are all false.
  Board b = fixture(empty_rows(), Piece::T, 0, 4, 5, HoldSupport::Discrete);
  REQUIRE(b.press_shift(Shift::Left));
  REQUIRE(b.active().x == 3);

  // The press still moved one cell. Nothing follows it, however long we wait.
  REQUIRE(b.tick(ms(5000)).shifts == 0);
  REQUIRE(b.active().x == 3);

  // And a second press is a second cell, which is what the OS's own auto-repeat
  // will be delivering.
  REQUIRE(b.press_shift(Shift::Left));
  REQUIRE(b.active().x == 2);
}

TEST_CASE("soft drop repeats on a clock rather than every frame",
          "[tetris][das]") {
  // ⚠ REFERENCE DEFECT 3: `if (keys[Down]) softDrop()` runs once per FRAME, so
  // a held Down falls at the frame rate and pays a point per cell per frame.
  Board b = fixture(empty_rows(), Piece::T, 0, 4, 0);
  REQUIRE(b.press_soft_drop());
  REQUIRE(b.active().y == 1);

  // 200 ms at a 40 ms rate is five cells, not twelve frames' worth.
  const auto r = b.tick(ms(200));
  REQUIRE(r.steps == 5);
  REQUIRE(b.active().y == 6);
}

// ── Top-out ─────────────────────────────────────────────────────────────────

TEST_CASE("a spawn that does not fit is a top-out, on a whole board",
          "[tetris][topout]") {
  // ⚠ REFERENCE DEFECT 6: its lock loop calls endGame() and `return`s from the
  // middle of writing the piece, leaving the board partially updated. Ours
  // finishes the lock and then fails to spawn, so the board a player is left
  // looking at is always consistent.
  // Two free rows: enough for load() to place the piece, not enough for the
  // NEXT spawn once this one has locked in them.
  auto rows = empty_rows();
  for (int r = 2; r < kVisibleRows; ++r) {
    rows[static_cast<std::size_t>(r)] = "##########";
  }
  Board b = fixture(rows, Piece::T, 0, 3, kHiddenRows);

  const int before = occupied_cells(b);
  b.hard_drop();
  REQUIRE(b.state() == State::ToppedOut);
  // The locking piece is fully written: four more cells, not one or two.
  REQUIRE(occupied_cells(b) == before + 4);
}

TEST_CASE("a spawned piece does not fall on its first tick", "[tetris][spawn]") {
  // ⚠ REFERENCE DEFECT 7: spawnPiece never resets lastDropTime, and gravity
  // only stamps it on a successful move, so a fresh piece inherits whatever was
  // banked and can drop a row immediately.
  auto rows = empty_rows();
  rows[19] = "##########";
  Board b = fixture(rows, Piece::T, 0, 4, 16 + kHiddenRows);

  // ⚠ 990 of the 1000 ms interval, not 900. The margin is the whole case: with
  // 900 banked, a 50 ms tick reaches 950 and does not step EITHER WAY, so the
  // case passes against a spawn that never resets the clock. Mutation caught
  // that — the assertion was true and vacuous.
  REQUIRE(b.tick(ms(990)).steps == 0);
  b.hard_drop();
  const int spawn_y = b.active().y;

  // 990 + 20 would be a step if the new piece had inherited the old one's
  // banked time; 20 on its own is nowhere near.
  REQUIRE(b.tick(ms(20)).steps == 0);
  REQUIRE(b.active().y == spawn_y);
}
