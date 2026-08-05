#pragma once

// Snake's rules. Ported from HTML-Games' snake/js/{snake,food,game}.js, single
// player only.
//
// ── This file includes no termforge header, and that is the point ────────────
//
// Same discipline as games/minesweeper/board.hpp and games/twenty48/board.hpp:
// the rules cannot construct a Screen, so test/25snake is *unable* to test them
// through a terminal rather than merely choosing not to.
//
// ── This is the first model in the repo that advances on its own ─────────────
//
// Minesweeper's clock only counts; 2048 moves when you press a key. Snake steps
// whether or not anybody touches anything, which is why gitea #6 called it the
// forcing function for termforge #58 (an idle loop capped at ~7.5 fps, with the
// rate varying by input activity). #58 is fixed and we are pinned past it, so
// the framework hands us honest frames — and everything below is written so that
// what the player sees is a function of ELAPSED TIME, not of how many frames
// carried it.
//
// ⚠ The step accumulator in here is NOT the thing AGENTS.md forbids. The banned
// one is the frame->tick accumulator, which is termforge's (App::set_tick_hz,
// chosen once by the Shell at 60 Hz). This one turns an already-fixed 1/60 s dt
// into game steps at 33-8 per second, because the speed curve IS the game and
// 60 Hz is not one of its values. No clock is read here; dt is the only time
// that exists, which is what keeps a case drivable by N fixed ticks.
//
// ── Three reference defects fixed rather than ported ─────────────────────────
//
// 1. THE REFERENCE'S SPEED TABLE IS INTENT, NOT BEHAVIOUR. game.js:78 reads
//    `if (deltaTime >= speed) { update(); lastUpdateTime = currentTime; }` — it
//    ASSIGNS the frame's timestamp instead of subtracting the interval, and never
//    catches up. Every step therefore rounds up to the next ~16.7 ms rAF
//    boundary, so its "100 ms" is really 100-117 ms and its 30 ms floor is
//    really ~33 ms. tick() below subtracts, so the average rate is the stated
//    one and the remainder survives into the next frame.
//
// 2. THE REFERENCE HAS NO DIRECTION QUEUE, only a single-slot `nextDirection`
//    that a second key overwrites (snake.js:14). That is precisely the bug gitea
//    #6 names — a fast double-turn eaten by the tick boundary. Worse,
//    setDirection validates against `this.direction`, the last APPLIED
//    direction, so Right -> Up -> Left inside one step validates Left against
//    the stale Right and silently drops the Up. Ours queues two and validates
//    each against the PREVIOUS QUEUED direction, which is the only reference
//    that makes both turns legal and still refuses a reversal into the neck.
//
// 3. THE REFERENCE'S FOOD SPAWN IS AN UNBOUNDED REJECTION SAMPLER (food.js:13)
//    with no board-full case, so a filled grid hangs the tab. This repo has met
//    that exact trap before: minesweeper's mine placement replaced the same
//    shape with a bounded algorithm, and restoring the reference's version there
//    makes test/14minesweeper HANG rather than fail. Here a full board is a WIN,
//    and the spawn picks the k-th free cell in two passes with no loop that can
//    fail to terminate.
//
// Stripped, on the 2048 power-tile precedent: local 2-4 player multiplayer
// (multiplayer.js + controls.js, about half the JS in the directory, and carrying
// its own bugs — eliminated snakes stay on the board as invisible lethal
// obstacles, and player 4's keys are compared against strings event.key never
// produces), and the "ghost trail" toggle, which is dead code: it paints
// translucent rectangles at the exact coordinates drawSnakeBody then overpaints
// opaquely, so enabling it has no visual effect at all.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

#include <glyphcade/arcade/rng.hpp>

namespace glyphcade::snake {

// The playfield, in CELLS rather than columns. layout.hpp spends two terminal
// columns per cell so the board reads square; the model has no opinion about
// that and never learns the number.
inline constexpr int kCols = 28;
inline constexpr int kRows = 16;
inline constexpr int kCells = kCols * kRows;

// Three segments at the centre heading right, exactly as snake.js:41.
inline constexpr int kStartLen = 3;

// +10 flat, exactly as game.js:148. The reference has no length bonus, no speed
// bonus and no combo, and inventing one would change the shape of the high-score
// record for no reason the reference supports.
inline constexpr int kFoodScore = 10;

struct Coord {
  int x{0};
  int y{0};
  auto operator==(const Coord&) const -> bool = default;
};

enum class Dir : std::uint8_t { Up, Down, Left, Right };

// For cases that sweep every direction. Same shape as minesweeper's kLevels and
// 2048's kDirs.
inline constexpr Dir kDirs[]{Dir::Up, Dir::Down, Dir::Left, Dir::Right};

[[nodiscard]] constexpr auto delta(Dir d) noexcept -> Coord {
  switch (d) {
    case Dir::Up: return {0, -1};
    case Dir::Down: return {0, 1};
    case Dir::Left: return {-1, 0};
    case Dir::Right: return {1, 0};
  }
  return {0, 0};
}

[[nodiscard]] constexpr auto opposite(Dir d) noexcept -> Dir {
  switch (d) {
    case Dir::Up: return Dir::Down;
    case Dir::Down: return Dir::Up;
    case Dir::Left: return Dir::Right;
    case Dir::Right: return Dir::Left;
  }
  return d;
}

enum class Level : std::uint8_t { Easy, Normal, Hard };
inline constexpr Level kLevels[]{Level::Easy, Level::Normal, Level::Hard};

// ⚠ Index-aligned with kLevels; Snake::start() casts the chosen index back to
// Level. See minesweeper/board.hpp's kLevelNames for the full argument.
inline constexpr std::string_view kLevelNames[]{"Easy", "Normal", "Hard"};
static_assert(std::size(kLevelNames) == std::size(kLevels),
              "every snake::Level needs an options-screen name, in enum order");

// gitea #6 asks for "wrap-vs-wall as a mode". The reference has no wrap at all —
// snake.js:117 is unconditionally fatal — so this half is ours, and it is a real
// player-facing mode rather than a compile-time option, because an unexposed mode
// is dead code and this repo has just finished deleting the last of that.
//
// ⚠ It keys the high-score record alongside Level. Wrap is materially easier than
// Solid, so one shared record would let a wrap run outrank a solid one.
enum class Walls : std::uint8_t { Solid, Wrap };
inline constexpr Walls kWallModes[]{Walls::Solid, Walls::Wrap};

// ⚠ "Solid" and "Wrap" rather than "Deadly"/"Wrap around": the options screen
// has to fit 20 columns, and the row is "> Walls: < Solid >".
inline constexpr std::string_view kWallNames[]{"Solid", "Wrap"};
static_assert(std::size(kWallNames) == std::size(kWallModes),
              "every snake::Walls mode needs an options-screen name");

// Won is the BOARD-FULL case and is genuinely unreachable in play on 28x16 — it
// exists because the alternative is the reference's infinite loop. Unlike 2048's
// Won it is terminal, since there is nothing left to do on a full board.
enum class State : std::uint8_t { Running, Lost, Won };

// The speed curve, from state.js:23. `start_ms` is the step interval at zero
// food; every food removes `step_ms` from it, down to kFloorMs.
struct Speed {
  int start_ms{100};
  int step_ms{3};
};

inline constexpr int kFloorMs = 30;
static_assert(kFloorMs > 0, "a zero step interval would make tick() spin");

[[nodiscard]] constexpr auto speed_for(Level l) noexcept -> Speed {
  switch (l) {
    case Level::Easy: return {.start_ms = 120, .step_ms = 2};
    case Level::Normal: return {.start_ms = 100, .step_ms = 3};
    case Level::Hard: return {.start_ms = 80, .step_ms = 4};
  }
  return {};
}

// max(30, start - eaten*inc), i.e. state.js's increaseSpeed() in closed form.
//
// Closed form rather than the reference's running subtraction on purpose: the
// reference mutates `speed` in place, so a difficulty change mid-run leaves the
// old curve's accumulated decrease applied to the new start value. Deriving it
// from `eaten` makes that state impossible to hold.
[[nodiscard]] constexpr auto interval_ms(Level l, int eaten) noexcept -> int {
  const Speed s = speed_for(l);
  const int ms = s.start_ms - (eaten * s.step_ms);
  return ms < kFloorMs ? kFloorMs : ms;
}

// How many turns may be buffered ahead of the live direction.
//
// Two, not one and not many. One is the reference, which drops the second turn
// of a double-tap. Many would let a burst of keys queue a path the player can no
// longer see the start of — input latency disguised as responsiveness.
inline constexpr int kTurnQueue = 2;

// What one call to tick() did, so the Game can sound it without Board learning
// that audio exists. Same role as 2048's MoveResult; minesweeper instead
// compares board state across the verb, which works there because a move is a
// keystroke. Here a tick may contain zero steps or several.
struct TickResult {
  int steps{0};
  int eaten{0};
  bool died{false};  // transitioned to Lost during this tick
  bool won{false};   // transitioned to Won during this tick
};

class Board {
 public:
  Board(Level level, Walls walls, std::uint64_t seed);

  auto reset(Level level, Walls walls, std::uint64_t seed) -> void;

  [[nodiscard]] auto level() const noexcept -> Level { return m_level; }
  [[nodiscard]] auto walls() const noexcept -> Walls { return m_walls; }
  [[nodiscard]] auto state() const noexcept -> State { return m_state; }
  [[nodiscard]] auto finished() const noexcept -> bool {
    return m_state != State::Running;
  }

  [[nodiscard]] auto eaten() const noexcept -> int { return m_eaten; }
  [[nodiscard]] auto score() const noexcept -> int { return m_eaten * kFoodScore; }
  [[nodiscard]] auto length() const noexcept -> int {
    return static_cast<int>(m_body.size());
  }

  // Head first, tail last — the reference's ordering, and the one that makes
  // "the head" a fixed position rather than a computed one.
  [[nodiscard]] auto body() const noexcept -> const std::deque<Coord>& {
    return m_body;
  }
  [[nodiscard]] auto head() const noexcept -> Coord { return m_body.front(); }
  [[nodiscard]] auto food() const noexcept -> Coord { return m_food; }

  // O(1), off the occupancy grid rather than the reference's linear scan over
  // the body. That is what makes the free-cell count in spawn_food() affordable
  // and the board-full case cheap enough to be a rule instead of a hang.
  [[nodiscard]] auto occupied(Coord p) const noexcept -> bool;

  [[nodiscard]] auto direction() const noexcept -> Dir { return m_dir; }
  [[nodiscard]] auto queued() const noexcept -> std::span<const Dir> {
    return {m_queue.data(), static_cast<std::size_t>(m_queued)};
  }

  [[nodiscard]] auto interval_ms() const noexcept -> int {
    return snake::interval_ms(m_level, m_eaten);
  }
  [[nodiscard]] auto interval() const noexcept -> std::chrono::duration<double> {
    return std::chrono::duration<double>{interval_ms() / 1000.0};
  }

  // Queue a turn. Returns false when the turn was refused — a reversal into the
  // neck, a repeat of where we are already going, or a full queue.
  //
  // ⚠ The return value is load-bearing: a refused turn must be SILENT. There is
  // no deny blip in the SFX bank and inventing one is a feel decision nobody who
  // cannot hear it should be making. Same argument as 2048's announce() guard.
  auto turn(Dir d) -> bool;

  // Advance by dt, taking as many steps as the elapsed time has paid for.
  //
  // ⚠ The remainder is CARRIED, not discarded. That one subtraction is the whole
  // difference between this and the reference's clock, and it is what makes the
  // step rate a function of elapsed time rather than of frame boundaries.
  auto tick(std::chrono::duration<double> dt) -> TickResult;

  // Fixture seam, the analogue of minesweeper's load_mines() and 2048's load():
  // drop in an exact snake and food so a case pins a RULE instead of the RNG.
  // `body` is head-first and must be non-empty; direction is taken from the
  // head->neck vector when there is a neck, else left alone. Lands in
  // State::Running with the accumulator empty.
  //
  // ⚠ `growing` is not a convenience. Whether the tail is held is the ONLY
  // difference between "following your own tail" (legal, and most of playing
  // well) and "closing the loop one step after eating" (fatal), so a fixture
  // that could not express it would leave those two rules testable only through
  // a contrived run-up — which would then be asserting about the run-up.
  auto load(std::span<const Coord> body, Coord food, int eaten = 0,
            bool growing = false) -> void;

 private:
  auto step(TickResult& out) -> void;
  auto spawn_food() -> bool;
  auto rebuild_occupancy() -> void;
  auto set_occupied(Coord p, bool on) noexcept -> void;

  std::deque<Coord> m_body;
  // One byte per cell, maintained incrementally by step(). A vector rather than
  // an array because a 448-byte member by value would be copied into every
  // fixture; the size never changes after construction.
  std::vector<std::uint8_t> m_occupied;

  Coord m_food{};
  Rng m_rng;

  Level m_level{Level::Normal};
  Walls m_walls{Walls::Solid};
  State m_state{State::Running};

  Dir m_dir{Dir::Right};
  std::array<Dir, kTurnQueue> m_queue{};
  int m_queued{0};

  int m_eaten{0};
  // Deferred by one step, exactly as the reference: eating sets this, and the
  // tail is kept on the NEXT step. It is not cosmetic — while it is set, the
  // tail cell stays occupied, so eating with your tail one cell ahead is fatal.
  bool m_growing{false};

  std::chrono::duration<double> m_accum{0.0};
};

}  // namespace glyphcade::snake
