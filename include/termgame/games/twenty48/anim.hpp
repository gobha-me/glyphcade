#pragma once

// The tween. Designed here, not ported — the reference has no working slide
// animation to port.
//
// ── What the reference actually does, since it looks like it animates ────────
//
// 2048/css/style.css:161 declares `transition: transform 0.12s ease-out` on
// .tile, and it never fires. render() (game.js:231) does `tileLayer.innerHTML =
// ''` and rebuilds every tile element from scratch, setting the inline transform
// BEFORE insertion — and a freshly inserted element has no prior computed value
// to interpolate from, so per spec no transition starts. Tiles teleport.
//
// Worse, the two @keyframes that DO fire animate `transform`, the same property
// carrying the tile's position, and a CSS animation outranks a normal inline
// declaration. So for 200ms every new or merged tile drops its translate() and
// renders at the layer origin before snapping back. The reference is not a
// design to imitate here; it is a warning about which frame you interpolate from.
//
// ── What this does instead ──────────────────────────────────────────────────
//
// Board::move() hands over the from->to facts, which is exactly the information
// the reference destroys inside slideRow. Anim owns a list of DrawTile and two
// phases: everything slides for kSlide, then merge results and the new spawn pop
// for kPop. Rendering asks Anim, always — see the note on `tiles()`.
//
// No termforge, no Board, no Screen. It takes a span of cell values, so it can be
// driven by N fixed ticks in a test with no terminal, which is the whole reason
// issue #5 could specify frame-rate independence as an acceptance criterion.

#include <chrono>
#include <span>
#include <vector>

#include <termgame/games/twenty48/board.hpp>

namespace termgame::twenty48 {

// 90ms of travel, then 70ms of pop. Both are FEEL numbers, and feel is the one
// thing this container cannot judge — see STATUS.md. They are here, named, rather
// than inline in the draw code, precisely so that whoever can finally play it has
// one place to change.
//
// The scale they have to live on: the Shell runs a 60Hz fixed timestep, so kSlide
// is ~5.4 ticks and a tile crosses 7 columns in that time. Much shorter and the
// slide is two frames and might as well not exist; much longer and input starts
// feeling queued behind it.
inline constexpr std::chrono::duration<double> kSlide{0.090};
inline constexpr std::chrono::duration<double> kPop{0.070};

// One tile as the renderer should draw it this frame.
//
// `col`/`row` are in board cells and fractional mid-slide; Layout::tile_x/tile_y
// turn them into screen columns. `pop` is 0 when the tile is at rest and rises to
// 1 at the peak of a merge or spawn pop, which the renderer is free to express
// however the tier allows — it is a scalar, not a pixel offset, because a
// character grid cannot scale a glyph.
struct DrawTile {
  double col{0.0};
  double row{0.0};
  int value{0};
  double pop{0.0};
};

class Anim {
 public:
  // The board at rest: one DrawTile per occupied cell, at integer positions,
  // done(). This is also what begin() falls back to for a move that changed
  // nothing, and what the Game calls on start and after an undo.
  auto rest(std::span<const int> cells) -> void;

  // Start animating `result`, whose motions describe travel INTO the board state
  // `after`. A result with moved == false is equivalent to rest(after).
  auto begin(const MoveResult& result, std::span<const int> after) -> void;

  // Advance by a fixed dt. Frame-rate independence comes from accumulating
  // elapsed time and clamping the phase fraction at 1 — never from assuming
  // anything about dt's size or regularity.
  auto advance(std::chrono::duration<double> dt) -> void;

  // Jump straight to rest.
  //
  // ⚠ This is what makes input responsive without making the animation a
  // participant in the rules. A direction key arriving mid-slide calls finish()
  // and then moves; nothing is queued and nothing is dropped, so N moves resolve
  // to the same board whether they arrive one per frame or all in one frame.
  // test/22twenty48 pins exactly that.
  auto finish() -> void;

  [[nodiscard]] auto done() const noexcept -> bool { return m_done; }

  // What to draw, this frame.
  //
  // ⚠ The Game draws from HERE and never from Board directly, even at rest. That
  // is not indirection for its own sake: two paths to the same pixels drift, and
  // this one is structurally incapable of disagreeing with itself because a
  // finished Anim holds exactly the resting board at integer positions. Same
  // argument as one-Layout-per-frame in layout.hpp.
  [[nodiscard]] auto tiles() const noexcept -> std::span<const DrawTile> {
    return m_tiles;
  }

  // Test observability.
  [[nodiscard]] auto elapsed() const noexcept -> std::chrono::duration<double> {
    return m_elapsed;
  }
  [[nodiscard]] auto sliding() const noexcept -> bool {
    return !m_done && m_elapsed < kSlide;
  }

 private:
  auto rebuild() -> void;

  // The frame we animate from, and the board we animate to.
  std::vector<Motion> m_motions;
  std::vector<int> m_after;
  std::vector<Coord> m_pops;  // merge destinations plus the spawn

  std::vector<DrawTile> m_tiles;
  std::chrono::duration<double> m_elapsed{0.0};
  bool m_done{true};
};

}  // namespace termgame::twenty48
