#pragma once

// Tetris' rules, and every clock it runs on. Ported from HTML-Games'
// tetris/js/{state,game,controls}.js.
//
// ── This file includes no termforge header, and that is the point ────────────
//
// Same discipline as the other three games: the rules cannot construct a
// Screen, so test/27tetris is *unable* to test them through a terminal rather
// than merely choosing not to. It is also what makes DAS testable at all —
// held-key timing is the one thing in this game a headless case could most
// easily be argued out of covering, so it lives here rather than in tetris.cpp.
//
// ── FIVE accumulators, and none of them is the one AGENTS.md bans ────────────
//
// Gravity, lock delay, shift auto-repeat, soft-drop repeat and the line-clear
// freeze all turn dt into game events. AGENTS.md says "do not hand-roll an
// accumulator", and it means the FRAME->TICK one, which is termforge's
// (App::set_tick_hz, chosen once by the Shell at 60 Hz). These turn an
// already-fixed 1/60 s dt into events at rates 60 Hz cannot express — gravity
// alone runs 1000 ms down to 50 ms. Snake established the distinction for one
// accumulator; this game has five, and the table in STATUS.md is the reference.
// No clock is read here; dt is the only time that exists.
//
// ── Nine reference defects, fixed rather than ported ─────────────────────────
//
// The rotation data is good (see pieces.hpp). The control flow is not.
//
//  1. HOLD WORKS EXACTLY ONCE PER GAME. lockPiece ends with `canHold = false`
//     (game.js:66); spawnPiece never sets it true; the only `true` in the
//     directory is in resetGameState. So you may hold once, ever — and
//     renderer.js:238 greys the box out forever after, which reads as intent.
//     Ours re-arms on lock.
//  2. GRAVITY ASSIGNS THE FRAME TIMESTAMP instead of subtracting the interval
//     (game.js:347). Byte for byte the defect Snake found at its own
//     game.js:78: every drop rounds up to the next ~16.7 ms rAF boundary, so
//     the published speed table is intent rather than behaviour. We subtract.
//  3. SOFT DROP IS UNGATED. `if (keys[Down]) softDrop()` runs every frame
//     (game.js:420-422) — ~60 cells/s at 60 fps, and one point per cell per
//     frame, so a held Down is a hard drop that also pays about 20 points.
//     Ours repeats on a clock like every other held key.
//  4. T-SPIN HAS NO ROTATION GATE (game.js:103-123). Any T that lands in a nook
//     with three filled corners scores as one. It is also evaluated AFTER the
//     piece is written into the board and BEFORE full rows are cleared, so
//     about-to-vanish rows inflate the corner count. And T_SPIN_MINI is dead
//     code: scoreLines is only ever reached with lineCount > 0.
//  5. THE LINE-CLEAR ANIMATION NEVER RUNS. lineClearAnimProgress is written to
//     0 in two places, read in one, and incremented in none, so
//     drawLockedBlocks(..., 1 - progress) draws at full alpha for the whole
//     300 ms. The setTimeout is a dead freeze. Exactly the 2048 reference's
//     slide animation, which also never fired.
//  6. NO SPAWN BUFFER. Pieces spawn at y:0 inside the visible field, and a lock
//     above row 0 calls endGame() and returns MID-LOOP (game.js:18-21), leaving
//     the board half-written. Ours has two hidden rows and never bails mid-lock.
//  7. SPAWN DOES NOT RESET THE DROP CLOCK, and gravity only stamps it on a
//     successful move (game.js:344-348), so a fresh piece drops a row on its
//     first frame.
//  8. HOLD DOES NOT VALIDATE THE SWAPPED-IN PIECE (game.js:308-311) — in a high
//     stack it can materialise overlapping locked blocks.
//  9. THE README'S SPEED TABLE IS OFF BY ONE LEVEL from its own code. README
//     says level 10 is 196 ms; getDropInterval computes 1000 * 0.85^(level-1),
//     which is 231 ms at level 10 and 196 at level 11. Same family as Snake's
//     "published table is intent, not behaviour", and the reason the table
//     below was generated from the CODE.
//
// Stripped, on the 2048 power-tile and Snake multiplayer precedent: the
// particle system and the neon canvas presentation. Both are rendering
// flourishes with no glyph analogue, and neither participates in a rule.

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <termgame/arcade/rng.hpp>
#include <termgame/games/tetris/pieces.hpp>

namespace termgame::tetris {

inline constexpr int kCols = 10;
inline constexpr int kVisibleRows = 20;

// Two rows above the visible field that a piece spawns into.
//
// ⚠ Reference defect 6. Without these, a piece has nowhere to be before it
// enters play, "spawned overlapping something" and "locked above the field" are
// the same condition, and the reference resolves that by returning from the
// middle of its lock loop with the board half written. With them, top-out is a
// spawn that does not fit — one check, in one place, on a board that is always
// whole.
inline constexpr int kHiddenRows = 2;
inline constexpr int kRows = kVisibleRows + kHiddenRows;
inline constexpr int kCells = kRows * kCols;

// How many upcoming pieces the player can see. state.js:10.
inline constexpr int kPreview = 3;

// ── The clocks ──────────────────────────────────────────────────────────────

// state.js:11-13, as a table rather than a runtime pow.
//
// ⚠ Generated from getDropInterval's CODE, not from the README's table, which
// is off by one level (defect 9). Precomputed for the same reason Snake's
// interval is closed form: a table cannot drift the way a running subtraction
// can, and it keeps <cmath> out of a game's rules — the same argument the audio
// synth makes about transcendentals, for the same portability reason.
inline constexpr int kGravityFloorMs = 50;
static_assert(kGravityFloorMs > 0,
              "a zero gravity interval would make tick() spin forever");

inline constexpr int kMaxTableLevel = 20;
inline constexpr std::array<int, kMaxTableLevel> kGravityMs{
    1000, 850, 722, 614, 522, 443, 377, 320, 272, 231,
    196,  167, 142, 120, 102, 87,  74,  63,  53,  50};

static_assert(kGravityMs.front() == 1000);
static_assert(kGravityMs.back() == kGravityFloorMs,
              "the table must reach the floor at its last level, or levels "
              "past it would step to a different rate than the one before");

[[nodiscard]] constexpr auto gravity_ms(int level) noexcept -> int {
  if (level < 1) return kGravityMs.front();
  if (level >= kMaxTableLevel) return kGravityFloorMs;
  return kGravityMs[static_cast<std::size_t>(level - 1)];
}

// state.js:14-17. Lock delay, its reset cap, and the two held-key rates.
inline constexpr int kLockDelayMs = 500;
inline constexpr int kMaxLockResets = 15;
inline constexpr int kDasMs = 170;
inline constexpr int kArrMs = 50;

// OURS. The reference has no soft-drop rate at all (defect 3), so there is
// nothing to port and a number had to be chosen. 40 ms is a little under one
// cell per ARR step, which makes a held Down clearly faster than gravity at
// every level without being a hard drop. ⚠ Whether it FEELS right needs a
// human; nothing in this container can judge it.
inline constexpr int kSoftDropMs = 40;

// state.js:18. Ours advances on dt and actually renders (defect 5).
inline constexpr int kLineClearMs = 300;

// ── Scoring, state.js:125-138 ───────────────────────────────────────────────

inline constexpr int kScoreSingle = 100;
inline constexpr int kScoreDouble = 300;
inline constexpr int kScoreTriple = 500;
inline constexpr int kScoreTetris = 800;
inline constexpr int kScoreTSpinMini = 100;
inline constexpr int kScoreTSpin = 400;
inline constexpr int kScoreTSpinDouble = 800;
inline constexpr int kScoreTSpinTriple = 1200;
inline constexpr int kScoreSoftDrop = 1;
inline constexpr int kScoreHardDrop = 2;
inline constexpr int kComboBase = 50;
inline constexpr int kComboIncrement = 50;
inline constexpr int kLinesPerLevel = 10;

// ── Difficulty ──────────────────────────────────────────────────────────────

// Every other game in the suite binds 1/2/3 to a difficulty, and Tetris' own
// difficulty is its level — which advances on its own. So the choice is where
// to START, and it keys the high-score record for exactly Snake's wrap reason:
// beginning at 10 hands the player the score multiplier immediately, so a
// shared key would let a level-10 run permanently outrank every level-1 one.
enum class StartLevel : std::uint8_t { One, Five, Ten };
inline constexpr StartLevel kStartLevels[]{StartLevel::One, StartLevel::Five,
                                           StartLevel::Ten};

// ⚠ Index-aligned with kStartLevels; Tetris::start() casts the chosen index
// back to StartLevel. See minesweeper/board.hpp's kLevelNames for why the
// static_assert is not decoration.
inline constexpr std::string_view kStartLevelNames[]{"1", "5", "10"};
static_assert(std::size(kStartLevelNames) == std::size(kStartLevels),
              "every tetris::StartLevel needs an options-screen name");

[[nodiscard]] constexpr auto start_level_value(StartLevel s) noexcept -> int {
  switch (s) {
    case StartLevel::One: return 1;
    case StartLevel::Five: return 5;
    case StartLevel::Ten: return 10;
  }
  return 1;
}

// ⚠ There is no Won. Tetris does not end well; it ends. Snake has a board-full
// win because a full grid is reachable and has to mean something; here the only
// terminal state is topping out, and inventing a win condition would be a rule
// with no reference behind it.
enum class State : std::uint8_t { Running, ToppedOut };

// Which way a held shift is going. None is "nothing held", which is a real
// state rather than a null: the reference tracks left and right independently
// and moves BOTH when both are down, so its piece jitters. One direction, most
// recent press wins.
enum class Shift : std::uint8_t { None, Left, Right };

// Whether the input layer can see a key come up.
//
// ⚠ This is the degradation contract, as a type. Under KeyboardMode::Enhanced
// on a terminal with the kitty protocol we get Release and can tell held from
// re-pressed; without it we cannot, and no amount of cleverness recovers it —
// press-only input genuinely does not contain the information. So the model
// takes the answer as a parameter and behaves differently, rather than
// pretending and feeling wrong. Discrete is the honest fallback: one press,
// one cell, and the OS's own auto-repeat does the holding.
enum class HoldSupport : std::uint8_t { Held, Discrete };

// ── A held key, as a clock ──────────────────────────────────────────────────

// Delay then rate: nothing for `delay_ms`, then one event every `rate_ms`.
//
// ⚠ Not a bool and a timestamp, which is the reference's shape and the reason
// its DAS state is spread over three parallel maps keyed by key code
// (state.js:177-179) that release() has to remember to clear in three places.
class Repeater {
 public:
  constexpr Repeater(int delay_ms, int rate_ms) noexcept
      : m_delay_ms(delay_ms), m_rate_ms(rate_ms) {}

  // Begin holding. The caller applies the FIRST action itself; this schedules
  // the repeats, which is why press() returns nothing.
  constexpr auto press() noexcept -> void {
    m_active = true;
    m_charged = false;
    m_accum = std::chrono::duration<double>{0.0};
  }

  constexpr auto release() noexcept -> void {
    m_active = false;
    m_charged = false;
    m_accum = std::chrono::duration<double>{0.0};
  }

  [[nodiscard]] constexpr auto active() const noexcept -> bool {
    return m_active;
  }

  // How many repeats the elapsed time has paid for. ⚠ Subtracts rather than
  // zeroing, for defect 2's reason: at 50 ms ARR against a 16.7 ms frame, a
  // clock that reset would lose a third of every interval and the rate would be
  // a function of the frame rate.
  auto advance(std::chrono::duration<double> dt) noexcept -> int;

 private:
  int m_delay_ms;
  int m_rate_ms;
  bool m_active{false};
  bool m_charged{false};  // past the initial delay
  std::chrono::duration<double> m_accum{0.0};
};

// ── What a tick did ─────────────────────────────────────────────────────────

// Sound and score are a function of what the tick actually did, not of a
// before/after comparison — Snake's argument, and stronger here: one tick can
// contain a gravity step, a lock, a line clear and a level-up.
struct TickResult {
  int steps{0};       // gravity steps applied
  int shifts{0};      // auto-repeat shifts applied
  bool locked{false};
  int lines{0};       // rows cleared by this tick's lock
  bool tetris{false}; // four at once
  bool tspin{false};
  bool leveled{false};
  bool topped_out{false};
};

struct Active {
  Piece piece{Piece::T};
  int rot{0};
  int x{0};  // board column of the piece box's left edge
  int y{0};  // board row of the piece box's top edge, hidden rows included
};

class Board {
 public:
  Board(StartLevel start, HoldSupport hold, std::uint64_t seed);

  auto reset(StartLevel start, HoldSupport hold) -> void;

  [[nodiscard]] auto state() const noexcept -> State { return m_state; }
  [[nodiscard]] auto score() const noexcept -> int { return m_score; }
  [[nodiscard]] auto lines() const noexcept -> int { return m_lines; }
  [[nodiscard]] auto level() const noexcept -> int;
  [[nodiscard]] auto start_level() const noexcept -> StartLevel {
    return m_start;
  }
  [[nodiscard]] auto hold_support() const noexcept -> HoldSupport {
    return m_hold_support;
  }
  [[nodiscard]] auto combo() const noexcept -> int { return m_combo; }

  [[nodiscard]] auto active() const noexcept -> const Active& {
    return m_active;
  }
  [[nodiscard]] auto held() const noexcept -> const Piece* {
    return m_has_hold ? &m_hold : nullptr;
  }
  [[nodiscard]] auto can_hold() const noexcept -> bool { return m_can_hold; }
  [[nodiscard]] auto preview() const noexcept -> std::span<const Piece> {
    return {m_next.data(), kPreview};
  }

  // The locked stack. Empty cells read as false; the active piece is NOT in
  // here, which is what lets draw() paint it separately and what stops a T-spin
  // check from seeing the piece it is asking about (defect 4).
  [[nodiscard]] auto filled(int col, int row) const noexcept -> bool;
  // Which tetromino left the block there, for the colour tier. nullopt is the
  // same answer as filled() == false, spelled so a caller cannot forget to ask.
  [[nodiscard]] auto piece_at(int col, int row) const noexcept
      -> std::optional<Piece>;

  // Rows currently vanishing, and how far through the freeze we are (0..1).
  // Non-empty only while State::Running and a clear is in flight.
  [[nodiscard]] auto clearing() const noexcept -> std::span<const int> {
    return {m_clearing.data(), static_cast<std::size_t>(m_clearing_count)};
  }
  [[nodiscard]] auto clear_progress() const noexcept -> double;

  // Where the active piece would land. Recomputed rather than cached: it is a
  // pure function of the stack and the piece, and a cached ghost is a second
  // copy of the truth that a kick can leave stale.
  [[nodiscard]] auto ghost_y() const noexcept -> int;

  // ── Verbs ─────────────────────────────────────────────────────────────────
  //
  // Each returns whether it changed anything, so a caller can be silent about a
  // refused input. ⚠ A refused move must make no sound; there is no deny blip
  // in the bank and inventing one is a feel decision nobody who cannot hear it
  // should make. Same rule Snake's turn() states.

  auto press_shift(Shift dir) -> bool;
  auto release_shift(Shift dir) -> void;
  auto press_soft_drop() -> bool;
  auto release_soft_drop() -> void;

  // ±1. O is a no-op by data rather than by a branch (see pieces.hpp).
  auto rotate(int dir) -> bool;
  auto hard_drop() -> bool;
  auto hold() -> bool;

  // Advance by dt, taking as many gravity steps, auto-repeats and lock-delay
  // expiries as the elapsed time has paid for.
  auto tick(std::chrono::duration<double> dt) -> TickResult;

  // ── Test seam ─────────────────────────────────────────────────────────────
  //
  // ⚠ Not a convenience. A wall kick, a T-spin and a lock-delay reset each
  // depend on a specific stack, and a case that had to play its way there would
  // be asserting about the run-up. `rows` are the VISIBLE field top-down, '#'
  // filled and anything else empty; the hidden rows are always empty, because a
  // fixture that started with something up there could not have been reached by
  // playing.
  //
  // ⚠ It rebuilds the stack and the active piece and deliberately does NOT
  // touch the bag or the preview: a fixture continues its seed's stream from
  // wherever reset() left it, so the piece that follows a fixture's first lock
  // is preview()[0] and not a fresh draw. Two cases depend on that — it is what
  // lets a loop clear the stack between locks without disturbing the sequence
  // it is reading.
  auto load(std::span<const std::string_view> rows, Piece piece, int rot, int x,
            int y) -> bool;

  // For cases that need to observe the clock rather than the board.
  [[nodiscard]] auto gravity_interval_ms() const noexcept -> int {
    return gravity_ms(level());
  }

 private:
  [[nodiscard]] auto fits(const Active& a) const noexcept -> bool;
  [[nodiscard]] auto grounded() const noexcept -> bool;
  auto spawn(Piece p) -> bool;
  auto refill_bag() -> void;
  auto take_next() -> Piece;
  // Shift the preview up and refill its tail from the bag.
  auto advance_preview() -> void;
  // ⚠ The ONLY way a piece enters play. spawn(Piece) lets its caller choose,
  // and gitea #55 was three callers each choosing take_next() — so the preview
  // and the spawn order were two sequences that were meant to agree and never
  // did. Keeping exactly one caller of spawn() is what makes them the same
  // sequence by construction rather than by convention.
  auto spawn_next() -> bool;
  auto try_shift(int dx) -> bool;
  auto step_down() -> bool;
  auto lock_active(TickResult& out) -> void;
  auto clear_full_rows(TickResult& out) -> void;
  auto award(int line_count, bool tspin, bool mini, TickResult& out) -> void;
  auto touch_lock_reset() -> void;
  [[nodiscard]] auto is_tspin(bool& mini) const noexcept -> bool;

  std::array<std::uint8_t, kCells> m_cells{};  // 0 empty, else Piece + 1
  Active m_active{};
  Rng m_rng;
  StartLevel m_start{StartLevel::One};
  HoldSupport m_hold_support{HoldSupport::Held};
  State m_state{State::Running};

  int m_score{0};
  int m_lines{0};
  int m_combo{-1};  // -1 is "no combo running", matching the reference

  Piece m_hold{Piece::T};
  bool m_has_hold{false};
  bool m_can_hold{true};

  // The bag holds at least kPreview + 1 so preview() never has to generate.
  std::vector<Piece> m_bag;
  // ⚠ m_next[0] IS the next piece to spawn — not a forecast of it, not a copy
  // kept alongside it. Every entry here has already left the bag.
  //
  // A span over the head of m_bag would express the same thing with one queue
  // instead of two, and was rejected: m_bag is a vector that reallocates on
  // every refill, so preview() would hand out a view that a later call can
  // dangle. std::array has no such failure mode.
  std::array<Piece, kPreview> m_next{};

  // ⚠ The rotation flag is what makes a T-spin a T-spin rather than a shape
  // that happens to be wedged (defect 4). Set by a successful rotate(), cleared
  // by any successful translation.
  bool m_last_was_rotation{false};
  int m_last_kick_index{0};

  Repeater m_shift_repeat{kDasMs, kArrMs};
  Repeater m_soft_repeat{0, kSoftDropMs};
  Shift m_shift_dir{Shift::None};

  std::chrono::duration<double> m_gravity{0.0};
  std::chrono::duration<double> m_lock{0.0};
  bool m_locking{false};
  int m_lock_resets{0};

  std::array<int, 4> m_clearing{};
  int m_clearing_count{0};
  std::chrono::duration<double> m_clear_elapsed{0.0};
};

}  // namespace termgame::tetris
