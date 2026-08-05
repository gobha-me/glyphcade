#include <glyphcade/games/twenty48/anim.hpp>

#include <algorithm>
#include <cstddef>

namespace glyphcade::twenty48 {

namespace {

// Linear, deliberately. An ease curve is a feel decision, and nothing in this
// container can judge feel — see the note on kSlide. Linear is the honest
// default: it is obviously not tuned, rather than looking tuned and being wrong.
[[nodiscard]] auto lerp(double a, double b, double t) -> double {
  return a + (b - a) * t;
}

// Phase fraction: how far through a span the animation is.
//
// Frame-rate independence comes from this being a function of ACCUMULATED ELAPSED
// TIME and nothing else — not of how many advance() calls delivered it. That is
// the property, and it is what issue #5's acceptance criterion asks for.
//
// ⚠ This used to clamp the result into [0, 1], and mutation testing showed the
// clamp was UNREACHABLE: no test could tell it from a plain division, because both
// call sites are already bounded. rebuild()'s slide branch runs only while
// m_elapsed < kSlide, and its pop branch only while m_elapsed >= kSlide, with
// advance() having already called finish() past kSlide + kPop. So both fractions
// are in [0, 1) by construction.
//
// Removed rather than kept-with-a-comment, on the precedent of announce()'s bool
// parameter in minesweeper.cpp: a guard that restates what the surrounding code
// already guarantees is a second, weaker statement of the same fact, and it later
// reads as load-bearing to whoever tries to simplify around it. If a future caller
// needs an unbounded input bounded, the bound belongs at that caller, where a test
// can see it.
[[nodiscard]] auto phase(std::chrono::duration<double> elapsed,
                         std::chrono::duration<double> span) -> double {
  if (span.count() <= 0.0) {
    return 1.0;
  }
  return elapsed / span;
}

// A triangle: 0 at the start, 1 at the midpoint, 0 at the end. The reference's
// @keyframes pop peaks at 50% too (style.css), which is the one thing about its
// animation worth keeping.
[[nodiscard]] auto pop_curve(double t) -> double {
  return t < 0.5 ? t * 2.0 : (1.0 - t) * 2.0;
}

}  // namespace

auto Anim::rest(std::span<const int> cells) -> void {
  m_motions.clear();
  m_pops.clear();
  m_after.assign(cells.begin(), cells.end());
  m_elapsed = std::chrono::duration<double>{0.0};
  m_done = true;
  rebuild();
}

auto Anim::begin(const MoveResult& result, std::span<const int> after) -> void {
  m_after.assign(after.begin(), after.end());
  m_elapsed = std::chrono::duration<double>{0.0};

  if (!result.moved) {
    // Nothing travelled, so there is nothing to interpolate and no pop to show.
    // Identical to rest() — stated by delegating rather than by duplicating, so
    // the two cannot diverge.
    rest(after);
    return;
  }

  m_motions = result.motions;

  // What pops: every merge destination, plus the spawned tile. A merge emits two
  // motions sharing one `to`, so dedupe or the destination pops twice as hard.
  m_pops.clear();
  for (const auto& m : m_motions) {
    if (m.merged && std::ranges::find(m_pops, m.to) == m_pops.end()) {
      m_pops.push_back(m.to);
    }
  }
  if (result.spawn.has_value()) {
    m_pops.push_back(result.spawn->at);
  }

  m_done = false;
  rebuild();
}

auto Anim::advance(std::chrono::duration<double> dt) -> void {
  if (m_done) {
    return;
  }
  // Negative dt would run the animation backwards; a fixed timestep never
  // produces one, but Anim is a public type in a header and should not trust it.
  if (dt.count() > 0.0) {
    m_elapsed += dt;
  }
  if (m_elapsed >= kSlide + kPop) {
    finish();
    return;
  }
  rebuild();
}

auto Anim::finish() -> void {
  m_motions.clear();
  m_pops.clear();
  m_elapsed = std::chrono::duration<double>{0.0};
  m_done = true;
  rebuild();
}

auto Anim::rebuild() -> void {
  m_tiles.clear();

  const bool is_sliding = !m_done && m_elapsed < kSlide;

  if (is_sliding) {
    // Phase one: draw the travellers, at their PRE-move values. The merged tile
    // does not exist yet — two tiles are still on their way to the same cell, and
    // showing the doubled value early is the tell that an implementation resolved
    // the board and then animated a lie.
    const double t = phase(m_elapsed, kSlide);
    m_tiles.reserve(m_motions.size());
    for (const auto& m : m_motions) {
      m_tiles.push_back(DrawTile{
          lerp(static_cast<double>(m.from.col), static_cast<double>(m.to.col), t),
          lerp(static_cast<double>(m.from.row), static_cast<double>(m.to.row), t),
          m.value,
          0.0,
      });
    }
    return;
  }

  // Phase two, and rest: the resolved board at integer positions. When the pop
  // window has expired every pop is 0, so this is also exactly what rest()
  // produces — which is why a finished Anim cannot disagree with the board.
  const double pop_t =
      m_done ? 0.0 : pop_curve(phase(m_elapsed - kSlide, kPop));

  m_tiles.reserve(m_after.size());
  for (std::size_t i = 0; i < m_after.size(); ++i) {
    const int value = m_after[i];
    if (value == 0) {
      continue;
    }
    const auto row = static_cast<int>(i) / kSize;
    const auto col = static_cast<int>(i) % kSize;
    const bool pops =
        pop_t > 0.0 && std::ranges::find(m_pops, Coord{row, col}) != m_pops.end();
    m_tiles.push_back(DrawTile{
        static_cast<double>(col),
        static_cast<double>(row),
        value,
        pops ? pop_t : 0.0,
    });
  }
}

}  // namespace glyphcade::twenty48
