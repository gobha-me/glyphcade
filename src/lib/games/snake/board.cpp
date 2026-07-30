#include <termgame/games/snake/board.hpp>

#include <algorithm>
#include <cstddef>

namespace termgame::snake {
namespace {

[[nodiscard]] auto index_of(Coord p) noexcept -> std::size_t {
  return static_cast<std::size_t>((p.y * kCols) + p.x);
}

[[nodiscard]] auto in_bounds(Coord p) noexcept -> bool {
  return p.x >= 0 && p.x < kCols && p.y >= 0 && p.y < kRows;
}

// Only ever called on a coordinate one step outside the grid, so a subtraction
// is enough and no modulo is needed. Written as a wrap of exactly one width
// rather than a general modulo because that is the only case that exists, and a
// general one would hide an out-of-range bug instead of tripping on it.
[[nodiscard]] auto wrapped(Coord p) noexcept -> Coord {
  if (p.x < 0) p.x = kCols - 1;
  if (p.x >= kCols) p.x = 0;
  if (p.y < 0) p.y = kRows - 1;
  if (p.y >= kRows) p.y = 0;
  return p;
}

}  // namespace

Board::Board(Level level, Walls walls, std::uint64_t seed)
    : m_occupied(static_cast<std::size_t>(kCells), 0), m_rng(seed) {
  reset(level, walls, seed);
}

auto Board::reset(Level level, Walls walls, std::uint64_t seed) -> void {
  m_level = level;
  m_walls = walls;
  m_state = State::Running;
  m_rng = Rng{seed};

  m_dir = Dir::Right;
  m_queued = 0;
  m_eaten = 0;
  m_growing = false;
  m_accum = std::chrono::duration<double>{0.0};

  // Centre, heading right, with the body trailing to the left — snake.js:41.
  const int cx = kCols / 2;
  const int cy = kRows / 2;
  m_body.clear();
  for (int i = 0; i < kStartLen; ++i) {
    m_body.push_back(Coord{cx - i, cy});
  }

  rebuild_occupancy();
  // A fresh board always has room, so this cannot fail. The return value is
  // still checked at the call site in step(), where it can.
  static_cast<void>(spawn_food());
}

auto Board::occupied(Coord p) const noexcept -> bool {
  if (!in_bounds(p)) return false;
  return m_occupied[index_of(p)] != 0;
}

auto Board::set_occupied(Coord p, bool on) noexcept -> void {
  if (!in_bounds(p)) return;
  m_occupied[index_of(p)] = on ? std::uint8_t{1} : std::uint8_t{0};
}

auto Board::rebuild_occupancy() -> void {
  std::ranges::fill(m_occupied, std::uint8_t{0});
  for (const Coord& c : m_body) {
    set_occupied(c, true);
  }
}

auto Board::turn(Dir d) -> bool {
  if (m_state != State::Running) return false;

  // ⚠ Against the LAST QUEUED direction, not the live one. The reference
  // compares with `this.direction` (snake.js:79), which is why Right -> Up ->
  // Left inside one step silently drops the Up: Left is judged against a Right
  // the snake is no longer committed to. Judging against the queue is what makes
  // a double-turn work AND still refuses a reversal into the neck the second
  // turn would create.
  const Dir reference = m_queued > 0
                            ? m_queue[static_cast<std::size_t>(m_queued - 1)]
                            : m_dir;

  if (d == reference) return false;
  if (d == opposite(reference)) return false;
  if (m_queued >= kTurnQueue) return false;

  m_queue[static_cast<std::size_t>(m_queued)] = d;
  ++m_queued;
  return true;
}

auto Board::spawn_food() -> bool {
  // Two passes and no rejection loop. The reference (food.js:13) draws random
  // coordinates until one misses the snake, which is unbounded by construction
  // and hangs outright on a full board.
  int free = 0;
  for (std::size_t i = 0; i < m_occupied.size(); ++i) {
    if (m_occupied[i] == 0) ++free;
  }
  if (free == 0) return false;

  auto k = static_cast<int>(m_rng.below(static_cast<std::uint64_t>(free)));
  for (std::size_t i = 0; i < m_occupied.size(); ++i) {
    if (m_occupied[i] != 0) continue;
    if (k == 0) {
      m_food = Coord{static_cast<int>(i) % kCols, static_cast<int>(i) / kCols};
      return true;
    }
    --k;
  }
  return false;  // unreachable: `free` counted the cells this loop walks
}

auto Board::step(TickResult& out) -> void {
  // The queued turn becomes the live one at the step boundary, which is the only
  // place a direction may change — a mid-step change would let a key alter a
  // move the player has already seen begin.
  if (m_queued > 0) {
    m_dir = m_queue[0];
    for (int i = 1; i < m_queued; ++i) {
      m_queue[static_cast<std::size_t>(i - 1)] = m_queue[static_cast<std::size_t>(i)];
    }
    --m_queued;
  }

  // Counted here rather than on the way out, so a step that kills is still a
  // step. The frame-rate case compares step COUNTS across chunkings, and a
  // fatal step that did not count would let two chunkings disagree silently.
  ++out.steps;

  const Coord d = delta(m_dir);
  Coord next{m_body.front().x + d.x, m_body.front().y + d.y};

  if (!in_bounds(next)) {
    if (m_walls == Walls::Solid) {
      m_state = State::Lost;
      out.died = true;
      return;
    }
    next = wrapped(next);
  }

  // ⚠ THE CELL THE TAIL IS VACATING IS LEGAL, and that exception is most of
  // playing snake well — following your own tail is the standard way to survive
  // a long body. The reference reaches the same answer from the other end: it
  // unshifts, pops, and only then scans body[1..], so the vacated cell is simply
  // no longer in the list. Stating it as a condition instead of as an ordering
  // is what lets a fatal step leave the board untouched (below), and it puts the
  // rule somewhere a reader can find it.
  //
  // While growing the tail is NOT released, so the same move is fatal. That is
  // the observable consequence of deferred growth, not a detail of it.
  const bool releases_tail = !m_growing;
  const bool into_own_tail = releases_tail && next == m_body.back();

  if (occupied(next) && !into_own_tail) {
    m_state = State::Lost;
    out.died = true;
    return;
  }

  // ⚠ Nothing above this line mutates the snake, so a step that kills commits
  // NOTHING — the board a player is left looking at is the position they died
  // from, not a half-applied move with the tail already gone. The reference
  // instead leaves the head sitting inside the body; that is not available to us
  // anyway, since a wall death would have to store an out-of-bounds head that
  // draw_field() could not paint.
  if (releases_tail) {
    set_occupied(m_body.back(), false);
    m_body.pop_back();
  } else {
    m_growing = false;
  }

  m_body.push_front(next);
  set_occupied(next, true);

  if (next != m_food) return;

  ++m_eaten;
  ++out.eaten;
  m_growing = true;

  // A board with no free cell has nowhere to put the next food, and the snake
  // has filled the grid. The reference spins forever here.
  if (!spawn_food()) {
    m_state = State::Won;
    out.won = true;
  }
}

auto Board::tick(std::chrono::duration<double> dt) -> TickResult {
  TickResult out{};
  if (m_state != State::Running) return out;
  if (dt.count() > 0.0) m_accum += dt;

  // ⚠ interval() is re-read every iteration because eating changes it, and the
  // remainder is SUBTRACTED rather than zeroed. The reference assigns the
  // frame's timestamp instead (game.js:78), which silently rounds every step up
  // to the next frame boundary and makes its own published speed table wrong.
  while (m_state == State::Running && m_accum >= interval()) {
    m_accum -= interval();
    step(out);
  }
  return out;
}

auto Board::load(std::span<const Coord> body, Coord food, int eaten,
                 bool growing) -> void {
  if (body.empty()) return;

  m_body.assign(body.begin(), body.end());
  m_food = food;
  m_eaten = eaten;
  m_state = State::Running;
  m_growing = growing;
  m_queued = 0;
  m_accum = std::chrono::duration<double>{0.0};

  // Derive the heading from the snake rather than taking it as an argument: a
  // fixture whose direction disagreed with its own neck would be a shape the
  // game can never produce, and cases built on it would prove nothing.
  if (m_body.size() >= 2) {
    const Coord h = m_body[0];
    const Coord neck = m_body[1];
    int dx = h.x - neck.x;
    int dy = h.y - neck.y;
    // A neck a whole grid away means the head wrapped across the edge, so the
    // raw difference points the wrong way. Only a one-cell step can produce
    // this, which is why clamping to +/-1 is a correction rather than a guess.
    if (dx > 1) dx = -1;
    if (dx < -1) dx = 1;
    if (dy > 1) dy = -1;
    if (dy < -1) dy = 1;
    if (dx > 0) m_dir = Dir::Right;
    else if (dx < 0) m_dir = Dir::Left;
    else if (dy > 0) m_dir = Dir::Down;
    else if (dy < 0) m_dir = Dir::Up;
  }

  rebuild_occupancy();
}

}  // namespace termgame::snake
