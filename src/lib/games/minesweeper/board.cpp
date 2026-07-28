#include <termgame/games/minesweeper/board.hpp>

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace termgame::minesweeper {
namespace {

// Returned by Board::at() for out-of-range coordinates. A caller that indexes
// off the edge gets a hidden, empty, unmarked cell rather than undefined
// behaviour; every rule below then treats it as uninteresting and moves on.
constexpr Cell kOutOfBounds{};

}  // namespace

Board::Board(Preset p, std::uint64_t seed) : m_rng(seed) {
  reset(p, seed);
}

auto Board::reset(Preset p, std::uint64_t seed) -> void {
  m_name = p.name;
  m_rows = p.rows;
  m_cols = p.cols;
  m_mines = p.mines;
  m_cells.assign(static_cast<std::size_t>(m_rows) * static_cast<std::size_t>(m_cols),
                 Cell{});
  m_rng = Rng{seed};
  m_state = State::Ready;
  m_revealed = 0;
  m_flags = 0;
  m_exploded.reset();
  m_clock = std::chrono::duration<double>{0.0};
  m_timer_running = false;
}

auto Board::at(Coord p) const -> const Cell& {
  if (!in_bounds(p)) {
    return kOutOfBounds;
  }
  return m_cells[index(p)];
}

auto Board::load_mines(std::span<const Coord> mines) -> void {
  for (Cell& c : m_cells) {
    c = Cell{};
  }
  // Playing, not Ready: the mines exist now, so the deferred-placement phase is
  // over. Leaving it Ready would let the next reveal() scatter a fresh random
  // layout on top of the fixture — the test would then be asserting the RNG,
  // which is the exact thing this seam exists to avoid.
  m_state = State::Playing;
  m_revealed = 0;
  m_flags = 0;
  m_exploded.reset();
  m_clock = std::chrono::duration<double>{0.0};
  m_timer_running = false;

  int placed = 0;
  for (const Coord p : mines) {
    if (!in_bounds(p) || cell(p).mine) {
      continue;
    }
    cell(p).mine = true;
    ++placed;
  }
  // The mine counter must describe the board that exists, not the one the
  // preset asked for — otherwise a fixture silently makes mines_remaining()
  // lie and every counter assertion downstream is meaningless.
  m_mines = placed;
  compute_adjacency();
}

auto Board::place_mines(Coord safe) -> void {
  // ⚠ NOT the reference's rejection sampler (minesweeper/js/game.js:52-62),
  // which draws random cells and retries on a collision. That loop has no
  // bound: once mines > rows*cols - 9 the safe zone makes the remaining pool
  // too small and it spins forever, with no diagnostic. A partial Fisher-Yates
  // over the eligible cells is O(n), always terminates, and always places
  // exactly the number of mines it claims to.
  std::vector<int> pool;
  pool.reserve(static_cast<std::size_t>(m_rows) * static_cast<std::size_t>(m_cols));

  // The safe zone is the clicked cell AND all eight of its neighbours
  // (Chebyshev radius 1). That is what guarantees the first click has adjacent
  // == 0 and therefore always opens a region rather than a lone number. It
  // shrinks to the clicked cell alone only when the board could not otherwise
  // hold its mines; no shipped preset reaches that, but a future custom size
  // would, and it must degrade rather than spin.
  const int radius = (m_mines > m_rows * m_cols - 9) ? 0 : 1;
  for (int r = 0; r < m_rows; ++r) {
    for (int c = 0; c < m_cols; ++c) {
      if (std::abs(r - safe.row) > radius || std::abs(c - safe.col) > radius) {
        pool.push_back(r * m_cols + c);
      }
    }
  }

  const int n = std::min(m_mines, static_cast<int>(pool.size()));
  for (int i = 0; i < n; ++i) {
    const auto remaining = pool.size() - static_cast<std::size_t>(i);
    const auto j = static_cast<std::size_t>(i) + m_rng.below(remaining);
    std::swap(pool[static_cast<std::size_t>(i)], pool[j]);
    m_cells[static_cast<std::size_t>(pool[static_cast<std::size_t>(i)])].mine = true;
  }
  m_mines = n;
}

auto Board::compute_adjacency() -> void {
  for (int r = 0; r < m_rows; ++r) {
    for (int c = 0; c < m_cols; ++c) {
      const Coord p{.row = r, .col = c};
      if (at(p).mine) {
        cell(p).adjacent = 0;
        continue;
      }
      int count = 0;
      for_each_neighbour(m_rows, m_cols, p, [&](Coord n) {
        if (at(n).mine) {
          ++count;
        }
      });
      cell(p).adjacent = static_cast<std::uint8_t>(count);
    }
  }
}

auto Board::reveal(Coord p) -> bool {
  if (!in_bounds(p) || finished()) {
    return false;
  }

  // ⚠ DIVERGENCE, and it must stay above the placement block. The reference
  // (minesweeper/js/game.js:201-211) consumes its firstClick flag, places the
  // mines and starts the clock before reveal() bails on a flagged cell — so a
  // first click that lands on a flag arms the game, starts the timer, and
  // reveals nothing. A move that did nothing must not start the clock.
  const Cell& c = at(p);
  if (c.revealed || c.mark == Mark::Flag) {
    return false;
  }

  if (m_state == State::Ready) {
    place_mines(p);
    compute_adjacency();
    m_state = State::Playing;
  }
  // The timer starts on the first successful REVEAL and never on a flag. It is
  // set here rather than inside the Ready branch above because a board built by
  // load_mines() has its mines already and is therefore never Ready — it would
  // otherwise play a whole game with a stopped clock.
  m_timer_running = true;

  if (at(p).mine) {
    lose(p);
    return true;
  }

  flood_reveal(p);
  check_win();
  return true;
}

auto Board::flood_reveal(Coord start) -> void {
  // An explicit stack, not recursion. An empty 16x30 board is 480 cells deep,
  // and depth that scales with the board is a stack overflow waiting for a
  // bigger difficulty rather than a bug that shows up in testing.
  std::vector<Coord> stack;
  stack.push_back(start);

  while (!stack.empty()) {
    const Coord p = stack.back();
    stack.pop_back();

    Cell& c = cell(p);
    if (c.revealed) {
      continue;
    }
    // ⚠ FLAGS BLOCK THE FLOOD, deliberately. A flag is the player asserting a
    // mine is there; the flood respects that assertion even when it is wrong,
    // which is why a stray flag leaves a visible hole in an opened region.
    // Question marks do NOT block — they are a note to self, not an assertion,
    // which is also what Windows Minesweeper does.
    if (c.mark == Mark::Flag) {
      continue;
    }

    c.revealed = true;
    ++m_revealed;

    // Numbers reveal but do not recurse; only a zero opens its neighbours. No
    // mine can enter this loop — cells are only pushed from a cell whose
    // adjacent count is zero, and the directly-clicked mine was handled in
    // reveal() before the call.
    if (c.adjacent != 0) {
      continue;
    }
    for_each_neighbour(m_rows, m_cols, p, [&](Coord n) {
      if (!at(n).revealed) {
        stack.push_back(n);
      }
    });
  }
}

auto Board::cycle_mark(Coord p) -> bool {
  if (!in_bounds(p) || finished() || at(p).revealed) {
    return false;
  }
  Cell& c = cell(p);
  switch (c.mark) {
    case Mark::None:
      c.mark = Mark::Flag;
      ++m_flags;
      break;
    case Mark::Flag:
      c.mark = Mark::Question;
      --m_flags;
      break;
    case Mark::Question:
      c.mark = Mark::None;
      break;
  }
  return true;
}

auto Board::chord(Coord p) -> bool {
  if (!in_bounds(p) || finished()) {
    return false;
  }
  const Cell& c = at(p);
  // A chord is a claim about a number the player has already satisfied. On a
  // hidden cell, or on a revealed zero, there is nothing to claim.
  if (!c.revealed || c.adjacent == 0) {
    return false;
  }

  int flags = 0;
  for_each_neighbour(m_rows, m_cols, p, [&](Coord n) {
    if (at(n).mark == Mark::Flag) {
      ++flags;
    }
  });
  // Wrong count: a visible no-op, never a partial reveal. Revealing "the ones
  // we're sure about" would make the gesture unpredictable.
  if (flags != static_cast<int>(c.adjacent)) {
    return false;
  }

  bool changed = false;
  for_each_neighbour(m_rows, m_cols, p, [&](Coord n) {
    // reveal() declines flagged and already-revealed cells itself, and latches
    // once the board is finished — so a chord through a misplaced flag hits a
    // mine and loses the game, exactly as it should.
    changed = reveal(n) || changed;
  });
  return changed;
}

auto Board::check_win() -> void {
  // Flags are irrelevant to winning: the condition is that every safe cell has
  // been revealed, which is the reference's rule and the classic one.
  if (m_revealed == m_rows * m_cols - m_mines) {
    win();
  }
}

auto Board::win() -> void {
  m_state = State::Won;
  m_timer_running = false;
  // Auto-flag what is left, so the counter reads zero and the finished board
  // shows the player where the mines were. A mine is never `revealed` — the
  // only way to open one ends the game through lose() — so every mine on a won
  // board wants a flag.
  for (int r = 0; r < m_rows; ++r) {
    for (int c = 0; c < m_cols; ++c) {
      const Coord p{.row = r, .col = c};
      if (at(p).mine) {
        cell(p).mark = Mark::Flag;
      }
    }
  }
  m_flags = m_mines;
}

auto Board::lose(Coord hit) -> void {
  m_state = State::Lost;
  m_timer_running = false;
  m_exploded = hit;
  // Nothing else. Showing every mine on a lost board, and marking the flags
  // that were wrong, are both facts derivable from the resolved state
  // (state == Lost && mine; mark == Flag && !mine) — so they are resolved at
  // draw time, in glyphs.hpp, and no presentation concern enters the model.
  //
  // The reference sets revealed = true on each mine here and lets its revealed
  // counter absorb them (minesweeper/js/game.js:139-146), which leaves
  // revealed_count() describing something other than what the player opened.
  // Ours keeps counting exactly that.
}

auto Board::advance(std::chrono::duration<double> dt) -> void {
  if (!m_timer_running) {
    return;
  }
  m_clock += dt;
}

auto Board::seconds() const noexcept -> int {
  const double s = m_clock.count();
  if (s >= static_cast<double>(kTimerCap)) {
    return kTimerCap;
  }
  return static_cast<int>(s);
}

}  // namespace termgame::minesweeper
