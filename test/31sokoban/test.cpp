// Sokoban's rules, its parser and its deadlock detector, with no terminal.
//
// ⚠ NO TERMFORGE HEADER, and it is not a style choice: level.hpp, board.hpp and
// levels.hpp name no termforge type, so a case here *cannot* construct a Screen
// even by accident. Same discipline as test/14minesweeper, test/25snake and
// test/27tetris. Anything that needs a Screen belongs in test/32sokoban-ui.
//
// ⚠ There is no clock in this game and therefore no clock case in this file.
// Snake has one accumulator, 2048 one tween, Tetris five — and every one of
// them shipped a bug that a test found. Sokoban advances only when a key is
// pressed. What replaces the clock as the thing most likely to be wrong is
// UNDO, which is why it gets the counter-restoration cases below rather than
// one "undo works" case.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <glyphcade/games/sokoban/board.hpp>
#include <glyphcade/games/sokoban/level.hpp>
#include <glyphcade/games/sokoban/levels.hpp>

namespace {

using namespace glyphcade::sokoban;

[[nodiscard]] auto make(std::initializer_list<std::string_view> rows)
    -> Board {
  const std::vector<std::string_view> v(rows);
  auto lv = parse(v, "test", 0);
  REQUIRE(lv.has_value());
  return Board(std::move(*lv));
}

}  // namespace

// ── The parser ───────────────────────────────────────────────────────────────

TEST_CASE("the seven standard characters all parse", "[sokoban][parse]") {
  // ⚠ Two levels, not one, and the reason is itself the point: `@` and `+` are
  // both the player, so a level containing both has two players and is refused
  // by the case below. A single fixture "exercising all seven characters" is
  // therefore not a legal level, which is exactly the kind of thing the
  // overloaded charset makes easy to get wrong.
  const std::vector<std::string_view> with_at{
      "#######",
      "#@$ .*#",
      "#######",
  };
  const auto a = parse(with_at, "all seven", 7);
  REQUIRE(a.has_value());
  REQUIRE(a->w == 7);
  REQUIRE(a->h == 3);
  REQUIRE(a->name == "all seven");
  REQUIRE(a->par == 7);

  // `*` is a goal AND a box. The charset overloads one character with two facts
  // and the parser is where that stops.
  REQUIRE(a->boxes.size() == 2);     // the $ and the *
  REQUIRE(a->player == Pos{1, 1});
  REQUIRE(a->is_goal(4, 1));         // .
  REQUIRE(a->is_goal(5, 1));         // *
  REQUIRE_FALSE(a->is_goal(2, 1));   // $
  REQUIRE(a->at(3, 1) == Terrain::Floor);
  REQUIRE(a->is_wall(0, 1));

  // `+` is a goal AND the player, and it is the only way to start standing on
  // one.
  const std::vector<std::string_view> with_plus{"#####", "#+$*#", "#####"};
  const auto b = parse(with_plus, "on a goal", 1);
  REQUIRE(b.has_value());
  REQUIRE(b->player == Pos{1, 1});
  REQUIRE(b->is_goal(1, 1));
  REQUIRE(b->boxes.size() == 2);
}

TEST_CASE("ragged rows are padded, and the short row's edge is not a hole",
          "[sokoban][parse]") {
  // ⚠ THE reference movement bug, and the one that matters most for term-game#8's
  // "the enormous existing level corpus loads directly": isValid() bounds
  // columns with board[0].length (game.js:213) while render() walks
  // board[r].length (:124). Published .sok files have their trailing spaces
  // trimmed, so rows ARE ragged — and there a short row hands back `undefined`,
  // `undefined !== WALL` passes, and the player steps out of the level.
  const std::vector<std::string_view> rows{
      "#######",
      "#@ ",  // short: the wall on the right was trimmed away
      "#  $.#",
      "#######",
  };
  const auto lv = parse(rows, "ragged", 0);
  REQUIRE(lv.has_value());
  REQUIRE(lv->w == 7);
  REQUIRE(lv->h == 4);

  // Padding is FLOOR, not wall — a trimmed space is floor that happened to be
  // outside the ring, and making it wall would change the puzzle.
  REQUIRE(lv->at(6, 1) == Terrain::Floor);

  // And walking off the padded edge is a wall, because off-grid reads answer
  // Wall. Without that this is the reference's step-into-undefined.
  Board b(*lv);
  for (int i = 0; i < 10; ++i) b.step(Dir::Right);
  REQUIRE(b.player().x <= 6);
  REQUIRE(b.player().y == 1);

  // ⚠ MUTATION FINDING. The fixture above has its WIDEST row first, so
  // "width = the first row's length" — which is precisely the reference's
  // board[0].length bug — produced an identical Level and survived. A ragged
  // level only discriminates the two if a LATER row is the widest one.
  const std::vector<std::string_view> narrow_first{
      "#####",
      "#@  $  #",
      "#  .   #",
      "########",
  };
  const auto wide = parse(narrow_first, "narrow first", 0);
  REQUIRE(wide.has_value());
  REQUIRE(wide->w == 8);
  REQUIRE(wide->is_wall(7, 3));
}

TEST_CASE("a level that is already solved is won at move zero",
          "[sokoban][parse]") {
  // ⚠ The reference can never win this level: checkWin() is only reachable from
  // move() (game.js:209) and loadLevel never calls it.
  Board b = make({"#####", "#@* #", "#####"});
  REQUIRE(b.won());
  REQUIRE(b.moves() == 0);
}

// ── Pushing ──────────────────────────────────────────────────────────────────

TEST_CASE("a step onto floor moves the player and nothing else",
          "[sokoban][push]") {
  Board b = make({"#####", "#@ .#", "#$  #", "#####"});
  const auto r = b.step(Dir::Right);
  REQUIRE(r.moved);
  REQUIRE_FALSE(r.pushed);
  REQUIRE(b.player() == Pos{2, 1});
  REQUIRE(b.moves() == 1);
  REQUIRE(b.pushes() == 0);
}

TEST_CASE("a step into a wall does nothing at all", "[sokoban][push]") {
  Board b = make({"#####", "#@ .#", "#$  #", "#####"});
  const auto r = b.step(Dir::Left);
  REQUIRE_FALSE(r.moved);
  REQUIRE(b.player() == Pos{1, 1});
  REQUIRE(b.moves() == 0);
  // ⚠ And it records NOTHING, so a later undo cannot replay a move that never
  // happened. A blocked step that pushed a history entry would rewind the
  // player through a wall.
  REQUIRE(b.history_size() == 0);
}

TEST_CASE("one box moves, two never do", "[sokoban][push]") {
  Board b = make({"########", "#@$$ ..#", "########"});
  // Pushing the first box would need the second to move too.
  REQUIRE_FALSE(b.step(Dir::Right).moved);
  REQUIRE(b.pushes() == 0);
  REQUIRE(b.player() == Pos{1, 1});
}

TEST_CASE("a box cannot be pushed into a wall", "[sokoban][push]") {
  Board b = make({"#####", "#@$#", "#  .", "#####"});
  // The row above is deliberately ragged; the '.' sits at the padded edge.
  REQUIRE_FALSE(b.step(Dir::Right).moved);
}

TEST_CASE("seating and unseating a crate are separate facts",
          "[sokoban][push]") {
  Board b = make({"#######", "#@$.  #", "#    .#", "#  $  #", "#######"});
  const auto on = b.step(Dir::Right);
  REQUIRE(on.pushed);
  REQUIRE(on.seated);
  REQUIRE_FALSE(on.unseated);
  REQUIRE(b.boxes_on_goals() == 1);

  const auto off = b.step(Dir::Right);
  REQUIRE(off.pushed);
  REQUIRE_FALSE(off.seated);
  REQUIRE(off.unseated);
  REQUIRE(b.boxes_on_goals() == 0);
}

TEST_CASE("a crate pushed from one goal to another is neither seated nor unseated",
          "[sokoban][push]") {
  // ⚠ MUTATION FINDING. Both `seated` and `unseated` are guarded with the
  // other's negation — `now_on_goal && !was_on_goal` — and dropping either
  // guard survived the whole suite, because every case above pushes a crate
  // between a goal and plain floor. Goal-to-goal is the only arrangement that
  // tells the two spellings apart.
  //
  // It matters audibly: `seated` is what plays the Seat effect, so the loose
  // version chimes again for a crate that was already home and merely slid one
  // square along a row of goals.
  Board b = make({"########", "#@*. $ #", "########"});
  REQUIRE(b.boxes_on_goals() == 1);

  const auto r = b.step(Dir::Right);
  REQUIRE(r.pushed);
  REQUIRE_FALSE(r.seated);
  REQUIRE_FALSE(r.unseated);
  REQUIRE(b.boxes_on_goals() == 1);
}

TEST_CASE("the level is won only when every crate is seated",
          "[sokoban][push]") {
  Board b = make({"########", "#@$  ..#", "#  $   #", "########"});
  REQUIRE_FALSE(b.won());
  b.step(Dir::Right);
  b.step(Dir::Right);
  b.step(Dir::Right);
  REQUIRE(b.boxes_on_goals() == 1);
  REQUIRE_FALSE(b.won());  // one seated, one not
}

// ── Undo ─────────────────────────────────────────────────────────────────────

TEST_CASE("undo restores the player, the crate AND both counters",
          "[sokoban][undo]") {
  Board b = make({"#######", "#@$ . #", "#######"});
  b.step(Dir::Right);
  REQUIRE(b.moves() == 1);
  REQUIRE(b.pushes() == 1);
  REQUIRE(b.player() == Pos{2, 1});
  REQUIRE(b.has_box(3, 1));

  REQUIRE(b.undo());
  REQUIRE(b.player() == Pos{1, 1});
  REQUIRE(b.has_box(2, 1));
  // ⚠ Both counters, not just moves. A push that is undone but still counted
  // makes "pushes" a number that only ever goes up, which is the one statistic
  // a Sokoban player uses to judge a solution.
  REQUIRE(b.moves() == 0);
  REQUIRE(b.pushes() == 0);
  REQUIRE_FALSE(b.undo());
}

TEST_CASE("undo of a plain step does not move a crate that is in front of it",
          "[sokoban][undo]") {
  // The player walks up to a crate without pushing it, then rewinds. If undo
  // moved "the thing one step ahead" unconditionally it would drag the crate
  // backwards through the player.
  Board b = make({"#######", "#@ $. #", "#######"});
  b.step(Dir::Right);
  REQUIRE(b.pushes() == 0);
  REQUIRE(b.undo());
  REQUIRE(b.has_box(3, 1));
  REQUIRE(b.player() == Pos{1, 1});
}

TEST_CASE("undo unwinds a whole run in reverse order", "[sokoban][undo]") {
  Board b = make({"########", "#@$   .#", "########"});
  for (int i = 0; i < 4; ++i) b.step(Dir::Right);
  REQUIRE(b.won());
  REQUIRE(b.history_size() == 4);

  // ⚠ Undo stays available after the level is solved, deliberately — see the
  // note on Board::undo(). This is the case that fails if someone "helpfully"
  // adds the reference's levelComplete guard.
  while (b.undo()) {
  }
  REQUIRE(b.player() == Pos{1, 1});
  REQUIRE(b.has_box(2, 1));
  REQUIRE(b.moves() == 0);
  REQUIRE_FALSE(b.won());
}

TEST_CASE("reset returns the start position and empties the history",
          "[sokoban][undo]") {
  Board b = make({"########", "#@$   .#", "########"});
  b.step(Dir::Right);
  b.step(Dir::Right);
  b.reset();
  REQUIRE(b.player() == Pos{1, 1});
  REQUIRE(b.has_box(2, 1));
  REQUIRE(b.moves() == 0);
  REQUIRE(b.pushes() == 0);
  REQUIRE(b.history_size() == 0);
  REQUIRE_FALSE(b.undo());
}

// ── Deadlock ─────────────────────────────────────────────────────────────────

TEST_CASE("pushing a crate into a corner turns the deadlock on",
          "[sokoban][deadlock]") {
  // It must be reachable by PLAYING, not only by authoring a fixture that is
  // already lost — a detector that is only ever asked about start positions has
  // never been asked the question the game asks it.
  //
  //   #####
  //   #  .#     the crate starts against the left wall, which blocks it on ONE
  //   #$  #     axis only — it can still be pushed up or down, so this is not
  //   # @ #     yet lost.
  //   #####
  Board b = make({"#####", "#  .#", "#$  #", "# @ #", "#####"});
  REQUIRE_FALSE(b.deadlocked());
  REQUIRE_FALSE(b.is_frozen(Pos{1, 2}));

  // Walk under it and push up. Now the wall above and the wall to the left both
  // hold it, and there is no goal underneath.
  REQUIRE(b.step(Dir::Left).moved);
  REQUIRE(b.step(Dir::Up).moved);
  REQUIRE(b.has_box(1, 1));
  REQUIRE(b.is_frozen(Pos{1, 1}));
  REQUIRE(b.deadlocked());

  // ⚠ And it is recoverable, which is the whole reason the game says so rather
  // than ending the run: one undo and the position is playable again.
  REQUIRE(b.undo());
  REQUIRE_FALSE(b.deadlocked());
}

TEST_CASE("one wall is not a deadlock and two perpendicular walls are",
          "[sokoban][deadlock]") {
  // ⚠ The near-miss half is the half that matters. A detector that fires on a
  // crate merely touching a wall would call almost every Sokoban position lost,
  // and telling a player to give up on a level they can still win is worse than
  // saying nothing — which is why this is written as a PAIR.
  {
    // Crate against the top wall only: still slidable left and right.
    Board b = make({"#####", "# $ #", "# @.#", "#####"});
    REQUIRE_FALSE(b.deadlocked());
    REQUIRE_FALSE(b.is_frozen(Pos{2, 1}));
  }
  {
    // Crate in the top-left corner: blocked above and to the left.
    Board b = make({"#####", "#$  #", "# @.#", "#####"});
    REQUIRE(b.is_frozen(Pos{1, 1}));
    REQUIRE(b.deadlocked());
  }
}

TEST_CASE("the far side of each axis blocks too, not only the near side",
          "[sokoban][deadlock]") {
  // ⚠ MUTATION FINDING, and an embarrassing one: every corner fixture in this
  // file used the TOP-LEFT corner, so each axis was only ever blocked by its
  // negative neighbour. Dropping `|| is_wall(b)` from blocked_on_axis — half
  // the test — survived the entire suite.
  //
  // This is the bottom-RIGHT corner, where both blockers are on the positive
  // side of their axis and the deleted half is the only half that fires.
  Board b = make({
      "#####",
      "# @ #",
      "#  $#",
      "#.  #",
      "#####",
  });
  // Against the right wall only: blocked horizontally by the far neighbour,
  // free vertically, so not yet frozen.
  REQUIRE_FALSE(b.is_frozen(Pos{3, 2}));
  REQUIRE_FALSE(b.deadlocked());

  REQUIRE(b.step(Dir::Right).moved);   // player to (3,1), above the crate
  REQUIRE(b.step(Dir::Down).moved);    // pushes it to (3,3)
  REQUIRE(b.has_box(3, 3));
  REQUIRE(b.is_frozen(Pos{3, 3}));
  REQUIRE(b.deadlocked());
}

TEST_CASE("a cornered crate ON its goal is not a deadlock",
          "[sokoban][deadlock]") {
  // ⚠ The classic corner detector that forgets this declares every finished
  // level unwinnable — the crate you were trying to place is, by construction,
  // in the hardest square on the board.
  Board b = make({"#####", "#*  #", "# @ #", "#####"});
  REQUIRE(b.is_frozen(Pos{1, 1}));  // it IS immovable
  REQUIRE_FALSE(b.deadlocked());    // ...and that is the point of it
  REQUIRE(b.won());
}

TEST_CASE("two crates bracing each other against a wall freeze together",
          "[sokoban][deadlock]") {
  // ⚠ This is what the recursion buys, and nothing else in the suite reaches
  // it. Neither crate is in a corner. Each is blocked vertically by the wall
  // above; horizontally, each is blocked by the OTHER, which is only immovable
  // because of the first one. A corner-only detector says both are fine.
  //
  //   ######
  //   #$$  #      both crates against the top wall, side by side, and the
  //   # @ .#      left one also against the left wall
  //   #  .##
  Board b = make({"######", "#$$  #", "# @ .#", "#  . #", "######"});
  REQUIRE(b.is_frozen(Pos{1, 1}));  // corner: wall above, wall left
  REQUIRE(b.is_frozen(Pos{2, 1}));  // wall above, and (1,1) which cannot move
  REQUIRE(b.deadlocked());
}

TEST_CASE("two crates that hold each other up, with neither in a corner",
          "[sokoban][deadlock]") {
  // ⚠ MUTATION FINDING, and the sharpest one in this file. The case above
  // LOOKS like it exercises the mutual-dependency recursion and does not: its
  // left crate is in a genuine corner, so it resolves by walls alone and the
  // assumption stack is never consulted. Turning that stack's `return true`
  // into `return false` survived the whole suite.
  //
  //   #######
  //   # $$  #    neither crate is cornered — there is open floor at (1,1) and
  //   # @ ..#    at (4,1) — and both are still frozen, because to push either
  //   #######    one sideways the player must stand where the other one is,
  //              and to push either one down the player must stand in a wall.
  //
  // This is the only arrangement in which each crate's immovability is derived
  // FROM the other's, so it is the only one that can tell the two spellings of
  // the recursion apart.
  Board b = make({"#######", "# $$  #", "# @ ..#", "#######"});
  REQUIRE_FALSE(b.level().is_wall(1, 1));
  REQUIRE_FALSE(b.level().is_wall(4, 1));
  REQUIRE(b.is_frozen(Pos{2, 1}));
  REQUIRE(b.is_frozen(Pos{3, 1}));
  REQUIRE(b.deadlocked());
}

TEST_CASE("a crate free on one axis is never frozen, however many neighbours",
          "[sokoban][deadlock]") {
  // Two crates side by side in OPEN FLOOR. Each is blocked horizontally by the
  // other, and neither is blocked vertically at all, so neither is frozen —
  // a detector that stopped at "has a crate neighbour" would call this lost.
  //
  // ⚠ Two earlier drafts of this fixture were wrong in the same instructive
  // way: a row of crates laid along a wall, which LOOKS free because there is
  // floor at both ends of the row. It is not. To push the end crate outward the
  // player has to stand on the crate next to it, and to push any of them away
  // from the wall the player has to stand inside the wall. The whole row is
  // frozen, the detector said so, and the fixture was what was wrong — twice.
  // Open floor above and below is what actually makes a crate free.
  Board b = make({
      "########",
      "#      #",
      "# $$   #",
      "#@   ..#",
      "########",
  });
  REQUIRE_FALSE(b.is_frozen(Pos{2, 2}));
  REQUIRE_FALSE(b.is_frozen(Pos{3, 2}));
  REQUIRE_FALSE(b.deadlocked());
}

// ── The bundled pack ─────────────────────────────────────────────────────────

TEST_CASE("all twenty bundled levels parse and are well formed",
          "[sokoban][pack]") {
  REQUIRE(level_count() == 20);
  for (int i = 0; i < level_count(); ++i) {
    const auto& e = pack()[static_cast<std::size_t>(i)];
    INFO("level " << (i + 1) << " " << e.name);
    const auto lv = parse(e.rows, e.name, e.par);
    REQUIRE(lv.has_value());
    REQUIRE_FALSE(lv->boxes.empty());
    REQUIRE(lv->par > 0);
    REQUIRE_FALSE(lv->name.empty());
    // Not already solved, or the level is not a puzzle.
    Board b(*lv);
    REQUIRE_FALSE(b.won());
    // And not dead on arrival: a bundled level that starts deadlocked would be
    // unsolvable, which is exactly what the reference shipped twice before
    // replacing its whole level set.
    REQUIRE_FALSE(b.deadlocked());
  }
}

TEST_CASE("every level has a distinct name", "[sokoban][pack]") {
  for (int i = 0; i < level_count(); ++i) {
    for (int j = i + 1; j < level_count(); ++j) {
      INFO("levels " << (i + 1) << " and " << (j + 1));
      REQUIRE(pack()[static_cast<std::size_t>(i)].name !=
              pack()[static_cast<std::size_t>(j)].name);
    }
  }
}

TEST_CASE("the pack's names and par fit the status line's budget",
          "[sokoban][pack]") {
  // The widest status string embeds the level name, and the narrowest budget
  // that shows a name is 72 columns. A level called something enormous would
  // push the rest of the line off screen rather than being clipped visibly.
  for (const auto& e : pack()) {
    INFO(e.name);
    REQUIRE(e.name.size() <= 24);
    REQUIRE(e.par < 100);
    // 7-bit: the status row is drawn on the no-colour tier like everything else.
    for (const char c : e.name) {
      REQUIRE(static_cast<unsigned char>(c) < 0x80);
    }
  }
}

// ⚠ THE PAR CHECK. levels.hpp claims every par is the true optimum, measured by
// breadth-first search, and contrasts that with the reference's numbers — eight
// of which are below the mathematical minimum for their own level. A claim like
// that in a comment is worth exactly nothing unless something re-derives it.
//
// A full BFS over the four-crate levels is far too slow for a test (the largest
// took minutes offline), so this covers the levels a BFS clears instantly and
// asserts the rest are merely present. That is a deliberately partial check and
// saying so is the point: it pins the numbers a reader can verify by hand, and
// the method that produced the others.
TEST_CASE("the small levels' pars are the true optimum", "[sokoban][pack]") {
  struct Key {
    int index;
    int par;
  };
  // Levels 1-5, the tutorial. Reference pars were 5, 8, 8, 9, 12.
  constexpr std::array<Key, 5> kKnown{{
      {0, 3}, {1, 5}, {2, 5}, {3, 6}, {4, 8},
  }};

  for (const auto& k : kKnown) {
    const auto& e = pack()[static_cast<std::size_t>(k.index)];
    INFO("level " << (k.index + 1) << " " << e.name);
    REQUIRE(e.par == k.par);

    // Re-derive it: breadth-first over (player, boxes), shortest move count.
    const auto lv = parse(e.rows, e.name, e.par);
    REQUIRE(lv.has_value());

    struct State {
      Pos player;
      std::vector<Pos> boxes;
    };
    const auto encode = [](const State& s) {
      std::string out;
      out += static_cast<char>(s.player.x);
      out += static_cast<char>(s.player.y);
      std::vector<Pos> sorted = s.boxes;
      for (std::size_t i = 0; i < sorted.size(); ++i) {
        for (std::size_t j = i + 1; j < sorted.size(); ++j) {
          const bool swap = sorted[j].y < sorted[i].y ||
                            (sorted[j].y == sorted[i].y &&
                             sorted[j].x < sorted[i].x);
          if (swap) std::swap(sorted[i], sorted[j]);
        }
      }
      for (const Pos p : sorted) {
        out += static_cast<char>(p.x);
        out += static_cast<char>(p.y);
      }
      return out;
    };
    const auto solved = [&](const State& s) {
      for (const Pos p : s.boxes) {
        if (!lv->is_goal(p.x, p.y)) return false;
      }
      return true;
    };

    std::vector<std::pair<State, int>> frontier{{{lv->player, lv->boxes}, 0}};
    std::vector<std::string> seen{encode(frontier.front().first)};
    int best = -1;
    for (std::size_t head = 0; head < frontier.size() && best < 0; ++head) {
      const State cur = frontier[head].first;
      const int dist = frontier[head].second;
      for (const Dir d : {Dir::Up, Dir::Down, Dir::Left, Dir::Right}) {
        const Pos dv = delta(d);
        State nxt = cur;
        nxt.player = {cur.player.x + dv.x, cur.player.y + dv.y};
        if (lv->is_wall(nxt.player.x, nxt.player.y)) continue;

        bool blocked = false;
        for (auto& bx : nxt.boxes) {
          if (!(bx == nxt.player)) continue;
          const Pos beyond{bx.x + dv.x, bx.y + dv.y};
          if (lv->is_wall(beyond.x, beyond.y)) {
            blocked = true;
            break;
          }
          for (const Pos other : cur.boxes) {
            if (other == beyond) blocked = true;
          }
          if (blocked) break;
          bx = beyond;
        }
        if (blocked) continue;

        if (solved(nxt)) {
          best = dist + 1;
          break;
        }
        const std::string k2 = encode(nxt);
        bool known = false;
        for (const auto& s : seen) {
          if (s == k2) {
            known = true;
            break;
          }
        }
        if (known) continue;
        seen.push_back(k2);
        frontier.push_back({nxt, dist + 1});
      }
    }
    REQUIRE(best == e.par);
  }
}
