// glyphcade — Sokoban: the rules.

#include <glyphcade/games/sokoban/board.hpp>

#include <algorithm>
#include <utility>

namespace glyphcade::sokoban {

Board::Board(Level level)
    : m_level(std::move(level)),
      m_boxes(m_level.boxes),
      m_player(m_level.player) {
  recompute_won();
}

auto Board::box_index(int x, int y) const noexcept -> std::size_t {
  for (std::size_t i = 0; i < m_boxes.size(); ++i) {
    if (m_boxes[i].x == x && m_boxes[i].y == y) return i;
  }
  return kNoBox;
}

auto Board::has_box(int x, int y) const noexcept -> bool {
  return box_index(x, y) != kNoBox;
}

auto Board::boxes_on_goals() const noexcept -> int {
  int n = 0;
  for (const Pos b : m_boxes) {
    if (m_level.is_goal(b.x, b.y)) ++n;
  }
  return n;
}

auto Board::recompute_won() noexcept -> void {
  m_won = boxes_on_goals() == static_cast<int>(m_boxes.size());
}

auto Board::step(Dir d) -> MoveResult {
  const Pos dv = delta(d);
  const Pos next{m_player.x + dv.x, m_player.y + dv.y};

  if (m_level.is_wall(next.x, next.y)) return {};

  MoveResult r;
  const std::size_t bi = box_index(next.x, next.y);
  if (bi != kNoBox) {
    const Pos beyond{next.x + dv.x, next.y + dv.y};
    // You may push one box, never two, and never into a wall. Both halves are
    // one lookup each because off-grid reads answer Wall.
    if (m_level.is_wall(beyond.x, beyond.y)) return {};
    if (has_box(beyond.x, beyond.y)) return {};

    const bool was_on_goal = m_level.is_goal(next.x, next.y);
    const bool now_on_goal = m_level.is_goal(beyond.x, beyond.y);
    m_boxes[bi] = beyond;

    r.pushed = true;
    r.seated = now_on_goal && !was_on_goal;
    r.unseated = was_on_goal && !now_on_goal;
    ++m_pushes;
  }

  m_player = next;
  ++m_moves;
  r.moved = true;

  m_history.push_back({d, r.pushed});
  recompute_won();
  r.won = m_won;
  return r;
}

auto Board::undo() -> bool {
  if (m_history.empty()) return false;

  const Move m = m_history.back();
  m_history.pop_back();

  const Pos dv = delta(m.dir);
  if (m.pushed) {
    // The box is one step beyond the player, in the direction they moved. It
    // goes back to the square the player is standing on — which is why a move
    // record is enough and a board copy is not needed.
    const Pos box{m_player.x + dv.x, m_player.y + dv.y};
    const std::size_t bi = box_index(box.x, box.y);
    if (bi != kNoBox) m_boxes[bi] = m_player;
    --m_pushes;
  }
  m_player = {m_player.x - dv.x, m_player.y - dv.y};
  --m_moves;

  recompute_won();
  return true;
}

auto Board::reset() -> void {
  m_boxes = m_level.boxes;
  m_player = m_level.player;
  m_history.clear();
  m_moves = 0;
  m_pushes = 0;
  recompute_won();
}

// ── Deadlock ───────────────────────────────────────────────────────────────

auto Board::blocked_on_axis(Pos box, Pos axis,
                            std::vector<Pos>& assumed) const noexcept -> bool {
  const Pos a{box.x - axis.x, box.y - axis.y};
  const Pos b{box.x + axis.x, box.y + axis.y};

  if (m_level.is_wall(a.x, a.y) || m_level.is_wall(b.x, b.y)) return true;

  for (const Pos n : {a, b}) {
    if (!has_box(n.x, n.y)) continue;
    // ⚠ The recursion needs the assumption stack, not merely a visited set. We
    // are asking "is n frozen GIVEN that box is immovable", and box is already
    // on the stack — so a pair braced against each other resolves to frozen
    // instead of recursing forever. Dropping this and using a plain visited set
    // gives the same answer here but stops being a statement about anything.
    if (std::find(assumed.begin(), assumed.end(), n) != assumed.end()) {
      return true;
    }
    if (frozen(n, assumed)) return true;
  }
  return false;
}

auto Board::frozen(Pos box, std::vector<Pos>& assumed) const noexcept -> bool {
  assumed.push_back(box);
  const bool horiz = blocked_on_axis(box, {1, 0}, assumed);
  // Short-circuit deliberately not fused into one expression: a box blocked on
  // one axis only is free, and evaluating the second axis when the first said
  // "no" costs a recursion for an answer that cannot change.
  const bool result = horiz && blocked_on_axis(box, {0, 1}, assumed);
  assumed.pop_back();
  return result;
}

auto Board::is_frozen(Pos box) const noexcept -> bool {
  std::vector<Pos> assumed;
  return frozen(box, assumed);
}

auto Board::deadlocked() const noexcept -> bool {
  for (const Pos b : m_boxes) {
    // A frozen box ON a goal is not a deadlock — it is a solved box, and the
    // classic corner-detector that forgets this declares every finished level
    // unwinnable.
    if (m_level.is_goal(b.x, b.y)) continue;
    if (is_frozen(b)) return true;
  }
  return false;
}

}  // namespace glyphcade::sokoban
