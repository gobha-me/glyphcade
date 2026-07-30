#include <termgame/games/tetris/board.hpp>

#include <algorithm>
#include <cstddef>

namespace termgame::tetris {
namespace {

[[nodiscard]] auto ms(int n) noexcept -> std::chrono::duration<double> {
  return std::chrono::duration<double>{static_cast<double>(n) / 1000.0};
}

// state.js:227. floor((width - box) / 2): I and the 3-wide pieces land on
// column 3, O on column 4, which is where guideline SRS spawns them.
[[nodiscard]] constexpr auto spawn_x(Piece p) noexcept -> int {
  return (kCols - def_for(p).box) / 2;
}

}  // namespace

auto Repeater::advance(std::chrono::duration<double> dt) noexcept -> int {
  if (!m_active) return 0;
  if (dt.count() > 0.0) m_accum += dt;

  int events = 0;
  if (!m_charged) {
    const auto delay = ms(m_delay_ms);
    if (m_accum < delay) return 0;
    m_accum -= delay;
    m_charged = true;
    ++events;
  }

  const auto rate = ms(m_rate_ms);
  // ⚠ Guarded, not assumed. A zero rate would spin here forever, which is the
  // same hazard Snake's kFloorMs static_assert exists for — but this one is a
  // constructor argument rather than a constant, so it is checked at runtime.
  if (rate.count() <= 0.0) return events;
  while (m_accum >= rate) {
    m_accum -= rate;
    ++events;
  }
  return events;
}

Board::Board(StartLevel start, HoldSupport hold, std::uint64_t seed)
    : m_rng(seed) {
  reset(start, hold);
}

auto Board::reset(StartLevel start, HoldSupport hold) -> void {
  m_cells.fill(0);
  m_start = start;
  m_hold_support = hold;
  m_state = State::Running;
  m_score = 0;
  m_lines = 0;
  m_combo = -1;
  m_has_hold = false;
  m_can_hold = true;
  m_last_was_rotation = false;
  m_last_kick_index = 0;
  m_shift_repeat.release();
  m_soft_repeat.release();
  m_shift_dir = Shift::None;
  m_gravity = std::chrono::duration<double>{0.0};
  m_lock = std::chrono::duration<double>{0.0};
  m_locking = false;
  m_lock_resets = 0;
  m_clearing_count = 0;
  m_clear_elapsed = std::chrono::duration<double>{0.0};

  m_bag.clear();
  refill_bag();
  for (int i = 0; i < kPreview; ++i) {
    m_next[static_cast<std::size_t>(i)] = take_next();
  }
  // A fresh board is empty, so this cannot fail. The return value is still
  // checked at the call site in lock_active(), where it can.
  static_cast<void>(spawn(take_next()));
}

auto Board::level() const noexcept -> int {
  // Derived, never stored. state.js's own level is a field it also recomputes,
  // and a stored copy is a second truth that a load() fixture would have to set
  // consistently. ⚠ This is also why level is NOT persisted as a high score: it
  // is an affine restatement of lines, which is the reason Snake refused a
  // best_length record.
  return start_level_value(m_start) + (m_lines / kLinesPerLevel);
}

auto Board::filled(int col, int row) const noexcept -> bool {
  if (col < 0 || col >= kCols || row < 0 || row >= kRows) return false;
  return m_cells[static_cast<std::size_t>((row * kCols) + col)] != 0;
}

auto Board::piece_at(int col, int row) const noexcept -> std::optional<Piece> {
  if (!filled(col, row)) return std::nullopt;
  return static_cast<Piece>(
      m_cells[static_cast<std::size_t>((row * kCols) + col)] - 1);
}

auto Board::clear_progress() const noexcept -> double {
  if (m_clearing_count == 0) return 0.0;
  const double span = ms(kLineClearMs).count();
  if (span <= 0.0) return 1.0;
  const double p = m_clear_elapsed.count() / span;
  return p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
}

auto Board::fits(const Active& a) const noexcept -> bool {
  for (int r = 0; r < kBoxMax; ++r) {
    for (int c = 0; c < kBoxMax; ++c) {
      if (!cell_at(a.piece, a.rot, r, c)) continue;
      const int col = a.x + c;
      const int row = a.y + r;
      // A cell above the field is legal — that is what the hidden rows are for
      // — but off the sides or through the floor is not.
      if (col < 0 || col >= kCols || row >= kRows) return false;
      if (row >= 0 && filled(col, row)) return false;
    }
  }
  return true;
}

auto Board::grounded() const noexcept -> bool {
  Active below = m_active;
  below.y += 1;
  return !fits(below);
}

auto Board::refill_bag() -> void {
  // A 7-bag: a shuffled permutation of the seven, not seven independent draws.
  // Fisher-Yates over Rng::below, which is itself bounded — unlike the
  // reference's food-spawn family of rejection loops that Snake and Minesweeper
  // both had to replace.
  std::array<Piece, kPieceCount> bag{};
  for (int i = 0; i < kPieceCount; ++i) {
    bag[static_cast<std::size_t>(i)] = kPieces[i];
  }
  for (int i = kPieceCount - 1; i > 0; --i) {
    const auto j = static_cast<int>(m_rng.below(static_cast<std::uint64_t>(i) + 1));
    std::swap(bag[static_cast<std::size_t>(i)], bag[static_cast<std::size_t>(j)]);
  }
  m_bag.insert(m_bag.end(), bag.begin(), bag.end());
}

auto Board::take_next() -> Piece {
  if (m_bag.empty()) refill_bag();
  const Piece p = m_bag.front();
  m_bag.erase(m_bag.begin());
  if (static_cast<int>(m_bag.size()) < kPreview + 1) refill_bag();
  return p;
}

auto Board::spawn(Piece p) -> bool {
  Active a{};
  a.piece = p;
  a.rot = 0;
  a.x = spawn_x(p);
  // ⚠ The box's top on the FIRST VISIBLE ROW, not at the top of the hidden
  // rows. Spawning into the buffer looks like the tidier use of it and is
  // wrong twice: the player cannot see the piece they are already steering
  // until gravity drags it into view, and — because every piece's rotation 0
  // sits in the top two box rows, which would then both be hidden — a spawn
  // could only ever be blocked by a stack that had already filled the buffer.
  // Top-out would be unreachable, and so would the "a hold that does not fit is
  // refused" rule, since its candidate is built at this same y.
  //
  // The buffer's job is to be room ABOVE the field for a kick to push into and
  // for a tall piece to lock partly out of sight, not to be where pieces start.
  a.y = kHiddenRows;

  m_active = a;
  m_locking = false;
  m_lock_resets = 0;
  m_lock = std::chrono::duration<double>{0.0};
  m_last_was_rotation = false;
  m_last_kick_index = 0;
  // ⚠ Defect 7: the reference never resets its drop clock on spawn, so a fresh
  // piece inherits whatever was banked and can fall on its first frame.
  m_gravity = std::chrono::duration<double>{0.0};

  if (!fits(m_active)) {
    m_state = State::ToppedOut;
    return false;
  }
  return true;
}

auto Board::touch_lock_reset() -> void {
  // game.js:324-329. A move or rotation while grounded buys another lock delay,
  // up to fifteen times — after which the piece locks whatever the player does,
  // which is what stops an infinite spin.
  if (m_locking && m_lock_resets < kMaxLockResets) {
    m_lock = std::chrono::duration<double>{0.0};
    ++m_lock_resets;
  }
}

auto Board::try_shift(int dx) -> bool {
  if (m_state != State::Running || m_clearing_count > 0) return false;
  Active a = m_active;
  a.x += dx;
  if (!fits(a)) return false;
  m_active = a;
  // ⚠ A translation clears the rotation flag, which is what stops a rotate-then
  // -slide from scoring as a T-spin (defect 4).
  m_last_was_rotation = false;
  touch_lock_reset();
  return true;
}

auto Board::step_down() -> bool {
  Active a = m_active;
  a.y += 1;
  if (!fits(a)) return false;
  m_active = a;
  m_last_was_rotation = false;
  return true;
}

auto Board::press_shift(Shift dir) -> bool {
  if (dir == Shift::None) return false;
  if (m_state != State::Running || m_clearing_count > 0) return false;

  // ⚠ ONE direction, most recent press wins. The reference tracks left and
  // right independently and auto-repeats BOTH while both are down
  // (game.js:376-417), so holding two keys makes the piece stutter in place.
  m_shift_dir = dir;
  if (m_hold_support == HoldSupport::Held) {
    m_shift_repeat.press();
  }
  return try_shift(dir == Shift::Left ? -1 : 1);
}

auto Board::release_shift(Shift dir) -> void {
  // Only the direction currently held may end the hold. Releasing the key you
  // are no longer moving with must not stop the one you are.
  if (m_shift_dir != dir) return;
  m_shift_dir = Shift::None;
  m_shift_repeat.release();
}

auto Board::press_soft_drop() -> bool {
  if (m_state != State::Running || m_clearing_count > 0) return false;
  if (m_hold_support == HoldSupport::Held) m_soft_repeat.press();
  if (!step_down()) return false;
  m_score += kScoreSoftDrop;
  return true;
}

auto Board::release_soft_drop() -> void { m_soft_repeat.release(); }

auto Board::rotate(int dir) -> bool {
  if (m_state != State::Running || m_clearing_count > 0) return false;
  if (dir == 0) return false;

  const int from = m_active.rot & 3;
  const int to = ((from + (dir > 0 ? 1 : -1)) + 4) & 3;
  const KickSet& kicks = kicks_for(m_active.piece, from, to);

  for (int i = 0; i < kKicksPerTransition; ++i) {
    Active a = m_active;
    a.rot = to;
    a.x += kicks[static_cast<std::size_t>(i)].x;
    // ⚠ MINUS. The tables are y-up and the board is y-down; game.js:293 makes
    // the same conversion at the same point. Getting this sign wrong produces
    // kicks that work in open space and fail exactly where a kick matters.
    a.y -= kicks[static_cast<std::size_t>(i)].y;
    if (!fits(a)) continue;

    m_active = a;
    m_last_was_rotation = true;
    m_last_kick_index = i;
    touch_lock_reset();
    return true;
  }
  return false;
}

auto Board::hard_drop() -> bool {
  if (m_state != State::Running || m_clearing_count > 0) return false;
  const int target = ghost_y();
  const int distance = target - m_active.y;
  m_active.y = target;
  m_score += distance * kScoreHardDrop;
  // ⚠ A drop that MOVED the piece is a translation, so it clears the rotation
  // flag exactly as a shift does. Without this, rotating in open space and then
  // slamming into a three-cornered nook scores as a T-spin — which is the
  // reference's own bug (defect 4) wearing a different hat: the corners are
  // right and the history is not. A drop of zero distance changes nothing and
  // must leave a genuine spin standing.
  if (distance > 0) m_last_was_rotation = false;
  // A hard drop locks immediately: there is no lock delay to grant, because the
  // player has just said they are done with this piece.
  TickResult discard{};
  lock_active(discard);
  return true;
}

auto Board::hold() -> bool {
  if (m_state != State::Running || m_clearing_count > 0) return false;
  if (!m_can_hold) return false;

  const Piece current = m_active.piece;
  const Piece incoming = m_has_hold ? m_hold : m_next.front();

  // ⚠ Defect 8: the reference swaps without checking, so in a high stack the
  // incoming piece can materialise inside locked blocks. Build the candidate
  // first and refuse the whole swap if it does not fit.
  Active candidate{};
  candidate.piece = incoming;
  candidate.rot = 0;
  candidate.x = spawn_x(incoming);
  // ⚠ The SAME row spawn() uses, and it has to stay that way. A candidate built
  // in the hidden buffer always fits — those rows are empty by construction —
  // so the validity check below would be decoration and defect 8 would be back.
  candidate.y = kHiddenRows;
  if (!fits(candidate)) return false;

  if (!m_has_hold) {
    // Consuming the preview's head, so the queue has to move up with it.
    for (int i = 0; i + 1 < kPreview; ++i) {
      m_next[static_cast<std::size_t>(i)] =
          m_next[static_cast<std::size_t>(i + 1)];
    }
    m_next[kPreview - 1] = take_next();
  }

  m_hold = current;
  m_has_hold = true;
  m_active = candidate;
  m_can_hold = false;
  m_locking = false;
  m_lock_resets = 0;
  m_lock = std::chrono::duration<double>{0.0};
  m_gravity = std::chrono::duration<double>{0.0};
  m_last_was_rotation = false;
  return true;
}

auto Board::ghost_y() const noexcept -> int {
  Active a = m_active;
  while (true) {
    Active next = a;
    next.y += 1;
    if (!fits(next)) return a.y;
    a = next;
  }
}

auto Board::is_tspin(bool& mini) const noexcept -> bool {
  mini = false;
  if (m_active.piece != Piece::T) return false;
  // ⚠ Defect 4, and the whole rule: a T-spin is something you DID, not a shape
  // you ended up in. Without this the reference scores any T that lands in a
  // three-cornered nook.
  if (!m_last_was_rotation) return false;

  // The four corners of the T's 3x3 box. Out of bounds counts as filled — a
  // wall is as good as a block for wedging a piece.
  const auto occupied = [this](int c, int r) {
    if (c < 0 || c >= kCols || r >= kRows) return true;
    if (r < 0) return false;  // open sky above the field is not a corner
    return filled(c, r);
  };
  const int x = m_active.x;
  const int y = m_active.y;
  const bool tl = occupied(x, y);
  const bool tr = occupied(x + 2, y);
  const bool bl = occupied(x, y + 2);
  const bool br = occupied(x + 2, y + 2);

  const int corners = static_cast<int>(tl) + static_cast<int>(tr) +
                      static_cast<int>(bl) + static_cast<int>(br);
  if (corners < 3) return false;

  // The two corners the T points at. A spin that fills those is a full T-spin;
  // one wedged from behind is a mini — except after the last kick in the set,
  // which guideline treats as a full spin because only a real spin reaches it.
  bool front_a = false;
  bool front_b = false;
  switch (m_active.rot & 3) {
    case 0: front_a = tl; front_b = tr; break;
    case 1: front_a = tr; front_b = br; break;
    case 2: front_a = bl; front_b = br; break;
    default: front_a = tl; front_b = bl; break;
  }
  mini = !(front_a && front_b) && m_last_kick_index != kKicksPerTransition - 1;
  return true;
}

auto Board::lock_active(TickResult& out) -> void {
  bool mini = false;
  // ⚠ BEFORE the piece is written into the stack and before any row is
  // cleared. The reference does both first (game.js:31-35), so the piece's own
  // about-to-vanish rows inflate its corner count.
  const bool tspin = is_tspin(mini);

  for (int r = 0; r < kBoxMax; ++r) {
    for (int c = 0; c < kBoxMax; ++c) {
      if (!cell_at(m_active.piece, m_active.rot, r, c)) continue;
      const int col = m_active.x + c;
      const int row = m_active.y + r;
      if (row < 0 || row >= kRows || col < 0 || col >= kCols) continue;
      m_cells[static_cast<std::size_t>((row * kCols) + col)] =
          static_cast<std::uint8_t>(static_cast<int>(m_active.piece) + 1);
    }
  }

  out.locked = true;
  m_locking = false;
  m_lock_resets = 0;
  m_lock = std::chrono::duration<double>{0.0};
  // ⚠ Defect 1: the reference sets this FALSE here, so hold works once per game
  // and then never again.
  m_can_hold = true;

  // ⚠ Only rows the locked piece actually WROTE A CELL INTO — not the whole
  // board, and not merely its bounding box. A row cannot become full without a
  // piece landing in it, so this finds nothing extra in play, and it makes the
  // four-row bound arithmetic (a tetromino is four rows tall at most) rather
  // than an assumption a fixture can overflow.
  //
  // ⚠ The box is NOT the same thing as the cells. Every rotation leaves at
  // least one box row empty, and a board with an already-full row inside that
  // empty band would have it "cleared" by a piece that never touched it.
  m_clearing_count = 0;
  for (int r = 0; r < kBoxMax; ++r) {
    const int row = m_active.y + r;
    if (row < 0 || row >= kRows) continue;
    bool touched = false;
    for (int c = 0; c < kBoxMax; ++c) {
      if (cell_at(m_active.piece, m_active.rot, r, c)) touched = true;
    }
    if (!touched) continue;
    bool full = true;
    for (int col = 0; col < kCols; ++col) {
      if (!filled(col, row)) {
        full = false;
        break;
      }
    }
    if (full) {
      m_clearing[static_cast<std::size_t>(m_clearing_count)] = row;
      ++m_clearing_count;
    }
  }

  if (m_clearing_count > 0) {
    // The rows stay on the board and vanish when the freeze ends, which is what
    // makes the flash something to draw. The reference froze for 300 ms and
    // drew nothing (defect 5).
    m_clear_elapsed = std::chrono::duration<double>{0.0};
    award(m_clearing_count, tspin, mini, out);
    return;
  }

  m_combo = -1;
  if (!spawn(take_next())) out.topped_out = true;
}

auto Board::clear_full_rows(TickResult& out) -> void {
  for (int i = 0; i < m_clearing_count; ++i) {
    const int row = m_clearing[static_cast<std::size_t>(i)];
    for (int r = row; r > 0; --r) {
      for (int c = 0; c < kCols; ++c) {
        m_cells[static_cast<std::size_t>((r * kCols) + c)] =
            m_cells[static_cast<std::size_t>(((r - 1) * kCols) + c)];
      }
    }
    for (int c = 0; c < kCols; ++c) m_cells[static_cast<std::size_t>(c)] = 0;
  }
  m_clearing_count = 0;
  m_clear_elapsed = std::chrono::duration<double>{0.0};
  if (!spawn(take_next())) out.topped_out = true;
}

auto Board::award(int line_count, bool tspin, bool mini, TickResult& out)
    -> void {
  const int before = level();
  ++m_combo;

  int base = 0;
  if (tspin) {
    if (mini) {
      base = kScoreTSpinMini;
    } else {
      switch (line_count) {
        case 1: base = kScoreTSpin; break;
        case 2: base = kScoreTSpinDouble; break;
        case 3: base = kScoreTSpinTriple; break;
        default: base = kScoreTetris; break;
      }
    }
  } else {
    switch (line_count) {
      case 1: base = kScoreSingle; break;
      case 2: base = kScoreDouble; break;
      case 3: base = kScoreTriple; break;
      default: base = kScoreTetris; break;
    }
  }

  m_score += base * before;
  if (m_combo > 0) {
    m_score += (kComboBase + (kComboIncrement * m_combo)) * before;
  }

  m_lines += line_count;
  out.lines = line_count;
  out.tetris = line_count == 4;
  out.tspin = tspin;
  out.leveled = level() > before;
}

auto Board::tick(std::chrono::duration<double> dt) -> TickResult {
  TickResult out{};
  if (m_state != State::Running) return out;

  // Whether the piece was already resting when this tick began. See the lock
  // block at the bottom for why that has to be answered before anything moves.
  const bool was_locking = m_locking;

  // The line-clear freeze owns the whole tick: no gravity, no auto-repeat, and
  // the rows are still on the board so there is something to flash.
  if (m_clearing_count > 0) {
    if (dt.count() > 0.0) m_clear_elapsed += dt;
    if (m_clear_elapsed >= ms(kLineClearMs)) clear_full_rows(out);
    return out;
  }

  if (m_shift_dir != Shift::None) {
    const int repeats = m_shift_repeat.advance(dt);
    for (int i = 0; i < repeats; ++i) {
      if (!try_shift(m_shift_dir == Shift::Left ? -1 : 1)) break;
      ++out.shifts;
    }
  }

  for (int i = 0, n = m_soft_repeat.advance(dt); i < n; ++i) {
    if (!step_down()) break;
    m_score += kScoreSoftDrop;
    ++out.steps;
  }

  // ⚠ Re-read every iteration, because a level-up inside this loop changes it,
  // and SUBTRACTED rather than zeroed — defect 2. The reference assigns the
  // frame's timestamp, which rounds every drop up to a frame boundary and makes
  // its own speed table wrong.
  if (dt.count() > 0.0) m_gravity += dt;
  while (m_clearing_count == 0 && m_state == State::Running &&
         m_gravity >= ms(gravity_ms(level()))) {
    m_gravity -= ms(gravity_ms(level()));
    if (step_down()) {
      ++out.steps;
      m_locking = false;
    } else if (!m_locking) {
      // ⚠ Grounded, so start the lock clock HERE rather than waiting for the
      // next gravity interval to notice. The reference only enters this branch
      // inside its own drop-interval check, so at level 1 a landed piece can
      // wait a full second before its 500 ms lock delay even begins.
      m_locking = true;
      m_lock = std::chrono::duration<double>{0.0};
    }
  }

  // Also outside the gravity loop: a piece can become grounded by a shift or a
  // rotation, with no gravity step involved.
  if (m_clearing_count == 0 && m_state == State::Running && !m_locking &&
      grounded()) {
    m_locking = true;
    m_lock = std::chrono::duration<double>{0.0};
  }

  if (m_locking && m_clearing_count == 0 && m_state == State::Running) {
    if (!grounded()) {
      m_locking = false;
    } else {
      // ⚠ ONLY time from ticks that began with the piece already grounded. A
      // piece that lands partway through this tick must not be credited with
      // the whole of it — otherwise one 1000 ms tick both drops the piece onto
      // the floor and expires its 500 ms lock delay, and the player gets no
      // slide window at all. In production dt is a fixed 1/60 s so the
      // difference is at most one frame, but the rule should be right rather
      // than merely small, and a test driving coarse ticks is exactly where the
      // wrong version shows.
      if (was_locking && dt.count() > 0.0) m_lock += dt;
      if (m_lock >= ms(kLockDelayMs)) lock_active(out);
    }
  }

  if (m_state == State::ToppedOut) out.topped_out = true;
  return out;
}

auto Board::load(std::span<const std::string_view> rows, Piece piece, int rot,
                 int x, int y) -> bool {
  m_cells.fill(0);
  for (std::size_t i = 0; i < rows.size() && i < kVisibleRows; ++i) {
    const int row = kHiddenRows + static_cast<int>(i);
    const std::string_view line = rows[i];
    for (int c = 0; c < kCols && c < static_cast<int>(line.size()); ++c) {
      if (line[static_cast<std::size_t>(c)] == '#') {
        // Locked cells are all one piece identity: a fixture cares about
        // occupancy, and pretending it knows which tetromino left a block there
        // would be a detail no case could justify.
        m_cells[static_cast<std::size_t>((row * kCols) + c)] =
            static_cast<std::uint8_t>(static_cast<int>(Piece::I) + 1);
      }
    }
  }

  m_state = State::Running;
  m_active = Active{.piece = piece, .rot = rot & 3, .x = x, .y = y};
  m_locking = false;
  m_lock_resets = 0;
  m_lock = std::chrono::duration<double>{0.0};
  m_gravity = std::chrono::duration<double>{0.0};
  m_clearing_count = 0;
  m_clear_elapsed = std::chrono::duration<double>{0.0};
  m_last_was_rotation = false;
  m_last_kick_index = 0;
  m_shift_repeat.release();
  m_soft_repeat.release();
  m_shift_dir = Shift::None;
  return fits(m_active);
}

}  // namespace termgame::tetris
