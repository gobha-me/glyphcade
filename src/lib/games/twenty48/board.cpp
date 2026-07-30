#include <termgame/games/twenty48/board.hpp>

#include <algorithm>
#include <cstddef>

namespace termgame::twenty48 {

namespace {

// The two spawn values and their odds: 90% a 2, 10% a 4.
//
// ⚠ THE trap when stripping the power-tile mechanic. In the reference these odds
// live INSIDE the power ternary (game.js:57):
//
//     value: isPower ? 2 : (Math.random() < 0.9 ? 2 : 4),
//
// so deleting the ternary along with the rest of the mechanic quietly deletes the
// chance of a 4 as well, and produces a game that looks right and plays wrong —
// every tile a 2 makes the board strictly easier and the score curve wrong. The
// odds are classic 2048 and have nothing to do with power tiles.
constexpr int kSpawnCommon = 2;
constexpr int kSpawnRare = 4;
constexpr std::uint64_t kSpawnRareIn = 10;  // 1-in-10 -> the 10% case

}  // namespace

Board::Board(std::uint64_t seed) : m_cells(kCells, 0), m_rng(seed) {
  reset(seed);
}

auto Board::reset(std::uint64_t seed) -> void {
  std::ranges::fill(m_cells, 0);
  m_rng = Rng{seed};
  m_state = State::Playing;
  m_score = 0;
  m_moves = 0;
  m_has_undo = false;
  m_undo_cells.clear();

  // Two starting tiles, sequentially, as the reference does (game.js:330-331).
  // Sequential rather than "two at once" matters: the second draw sees the first
  // tile already placed, so they cannot collide.
  spawn_tile();
  spawn_tile();
}

auto Board::index(Coord p) const noexcept -> int { return p.row * kSize + p.col; }

auto Board::coord_of(int idx) noexcept -> Coord {
  return Coord{idx / kSize, idx % kSize};
}

auto Board::at(Coord p) const -> int {
  if (p.row < 0 || p.row >= kSize || p.col < 0 || p.col >= kSize) {
    return 0;
  }
  return m_cells[static_cast<std::size_t>(index(p))];
}

auto Board::cells() const noexcept -> std::span<const int> { return m_cells; }

auto Board::best_tile() const noexcept -> int {
  return *std::ranges::max_element(m_cells);
}

auto Board::empty_count() const noexcept -> int {
  return static_cast<int>(std::ranges::count(m_cells, 0));
}

auto Board::line(Dir d, int i) -> std::array<int, kSize> {
  std::array<int, kSize> out{};
  for (int k = 0; k < kSize; ++k) {
    switch (d) {
      // k == 0 is the destination edge in every case, which is what lets the
      // compaction below be direction-agnostic.
      case Dir::Left:
        out[static_cast<std::size_t>(k)] = i * kSize + k;
        break;
      case Dir::Right:
        out[static_cast<std::size_t>(k)] = i * kSize + (kSize - 1 - k);
        break;
      case Dir::Up:
        out[static_cast<std::size_t>(k)] = k * kSize + i;
        break;
      case Dir::Down:
        out[static_cast<std::size_t>(k)] = (kSize - 1 - k) * kSize + i;
        break;
    }
  }
  return out;
}

auto Board::can_move() const noexcept -> bool {
  // Any gap is a move. Checked first because it is the common case and it makes
  // the pair scan below unreachable on all but a full board.
  if (std::ranges::find(m_cells, 0) != m_cells.end()) {
    return true;
  }

  // Only right and down neighbours, which is sufficient rather than lazy:
  // adjacency is symmetric, so every pair is seen exactly once from its
  // upper-left member. Same reasoning as the reference (game.js:210-219).
  for (int r = 0; r < kSize; ++r) {
    for (int c = 0; c < kSize; ++c) {
      const int v = at(Coord{r, c});
      if (c + 1 < kSize && at(Coord{r, c + 1}) == v) {
        return true;
      }
      if (r + 1 < kSize && at(Coord{r + 1, c}) == v) {
        return true;
      }
    }
  }
  return false;
}

auto Board::snapshot() -> void {
  m_undo_cells = m_cells;
  m_undo_state = m_state;
  m_undo_score = m_score;
  m_undo_moves = m_moves;
  m_has_undo = true;
}

auto Board::undo() -> bool {
  if (!m_has_undo) {
    return false;
  }
  m_cells = m_undo_cells;
  m_state = m_undo_state;
  m_score = m_undo_score;
  m_moves = m_undo_moves;

  // One level only, exactly like the reference: consuming the undo clears it.
  m_has_undo = false;
  return true;
}

auto Board::spawn_tile() -> std::optional<Spawn> {
  // Collect the gaps rather than rejection-sampling for one. Same argument
  // minesweeper's board.cpp makes for partial Fisher-Yates over mine placement:
  // a rejection loop on a nearly-full board is unbounded in principle, and this
  // is called on a board that may have exactly one gap left.
  std::array<int, kCells> gaps{};
  int n = 0;
  for (int i = 0; i < kCells; ++i) {
    if (m_cells[static_cast<std::size_t>(i)] == 0) {
      gaps[static_cast<std::size_t>(n++)] = i;
    }
  }
  if (n == 0) {
    return std::nullopt;
  }

  const int idx = gaps[static_cast<std::size_t>(
      m_rng.below(static_cast<std::uint64_t>(n)))];

  // One draw, 1-in-10. Two draws happen per spawn (position, then value) and the
  // order is part of the seed contract — swapping them changes every game.
  const bool rare = m_rng.below(kSpawnRareIn) == 0;
  const int value = rare ? kSpawnRare : kSpawnCommon;
  m_cells[static_cast<std::size_t>(idx)] = value;
  return Spawn{coord_of(idx), value};
}

auto Board::move(Dir d) -> MoveResult {
  MoveResult result;
  // Reserve the worst case once: every cell occupied and every pair merging
  // still emits at most one motion per tile.
  result.motions.reserve(kCells);

  std::vector<int> next(kCells, 0);

  for (int i = 0; i < kSize; ++i) {
    const auto idxs = line(d, i);

    // Gather the occupied cells in travel order, keeping their source coords.
    // This is the step the reference throws away: its filter(Boolean) at
    // game.js:78 drops the positions, so by the time slideRow decides what
    // merges, there is nothing left to say where anything came from.
    std::array<Coord, kSize> src_at{};
    std::array<int, kSize> src_val{};
    int count = 0;
    for (int k = 0; k < kSize; ++k) {
      const int v = m_cells[static_cast<std::size_t>(idxs[static_cast<std::size_t>(k)])];
      if (v != 0) {
        src_at[static_cast<std::size_t>(count)] =
            coord_of(idxs[static_cast<std::size_t>(k)]);
        src_val[static_cast<std::size_t>(count)] = v;
        ++count;
      }
    }

    int slot = 0;
    for (int k = 0; k < count;) {
      const int dest_idx = idxs[static_cast<std::size_t>(slot)];
      const Coord dest = coord_of(dest_idx);

      // ── The double-merge rule ────────────────────────────────────────────
      // Both operands are PRE-MOVE tiles, always. Nothing is ever compared
      // against an already-merged result, so a tile produced by this move is
      // structurally incapable of merging again within it — [2,2,4] left gives
      // [4,4], not [8]. The reference gets this right too (game.js:85) and it is
      // the single fiddliest rule in the game, which is why AGENTS.md names it
      // as a reason to read the reference before reinventing.
      //
      // ⚠ Do not "simplify" this by folding the merged value back into src_val
      // and rescanning. That is exactly the bug this shape avoids.
      if (k + 1 < count &&
          src_val[static_cast<std::size_t>(k)] ==
              src_val[static_cast<std::size_t>(k + 1)]) {
        const int merged = src_val[static_cast<std::size_t>(k)] * 2;
        next[static_cast<std::size_t>(dest_idx)] = merged;

        // Two motions, one destination. The tween needs both journeys: the
        // surviving tile and the one that lands on top of it.
        result.motions.push_back(Motion{src_at[static_cast<std::size_t>(k)], dest,
                                        src_val[static_cast<std::size_t>(k)], true});
        result.motions.push_back(Motion{src_at[static_cast<std::size_t>(k + 1)], dest,
                                        src_val[static_cast<std::size_t>(k + 1)], true});

        // Score is the RESULTING tile's value, classic 2048 (game.js:83).
        result.score_delta += merged;
        ++result.merges;
        k += 2;
      } else {
        next[static_cast<std::size_t>(dest_idx)] = src_val[static_cast<std::size_t>(k)];
        result.motions.push_back(Motion{src_at[static_cast<std::size_t>(k)], dest,
                                        src_val[static_cast<std::size_t>(k)], false});
        k += 1;
      }
      ++slot;
    }
  }

  // "Did anything change" judged on the resulting board, not on whether some
  // motion has from != to. The reference compares value strings per line
  // (game.js:142-146) for the same reason: it is the definition of a legal move,
  // and it cannot be fooled by a tile that merged without travelling.
  result.moved = next != m_cells;

  if (!result.moved) {
    // Nothing happens. No score, no spawn, no move count — and critically the
    // undo slot is untouched, which is the reference bug not ported (see the
    // note on undo() in board.hpp).
    result.motions.clear();
    return result;
  }

  // Snapshot BEFORE committing, and only now that the move is known to be legal.
  snapshot();

  m_cells = std::move(next);
  m_score += result.score_delta;
  ++m_moves;

  result.spawn = spawn_tile();

  // Win latches once, on the transition, so the Game can play the sound exactly
  // once and the banner can stay up while play continues.
  //
  // >= rather than the reference's `=== 2048` (game.js:195). Values only ever
  // double, so the two agree in practice; >= is the honest predicate and does
  // not depend on that argument staying true.
  if (m_state == State::Playing && best_tile() >= kWinTile) {
    m_state = State::Won;
  }

  // Loss is evaluated after the spawn, and overrides Won — reaching 2048 and
  // then filling the board is a finished game.
  if (!can_move()) {
    m_state = State::Lost;
  }

  return result;
}

auto Board::load(std::span<const int> cells, int score) -> void {
  const auto n = std::min<std::size_t>(cells.size(), kCells);
  std::ranges::fill(m_cells, 0);
  std::copy_n(cells.begin(), n, m_cells.begin());
  m_score = score;
  m_state = State::Playing;
  m_moves = 0;
  m_has_undo = false;
  m_undo_cells.clear();
}

}  // namespace termgame::twenty48
