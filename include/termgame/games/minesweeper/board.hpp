#pragma once

// term-game — Minesweeper: the board model, and nothing else.
//
// ⚠ THIS HEADER DELIBERATELY INCLUDES NO TERMFORGE HEADER, and must not start.
// AGENTS.md asks that game logic be testable by driving N fixed ticks and
// asserting state, with no Screen and no TTY. Keeping termforge out of this
// translation unit turns that from a discipline into a fact: test/14minesweeper
// *cannot* construct a Screen, because it has no declaration for one. If a rule
// in here ever needs a Screen, the model and the view have grown together and
// the fix is upstream of this file.
//
// The rules are ported from HTML-Games' minesweeper (gitea xcaliber/HTML-Games,
// minesweeper/js/game.js). Two deliberate divergences from that reference are
// marked ⚠ DIVERGENCE below; both are bugs there, not rules.

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// For OptionSpec: default_preset() below reads the level default out of the
// options schema, so "which level a fresh game starts on" has one definition
// rather than one here and one in minesweeper.hpp's kMeta.
#include <termgame/arcade/game_meta.hpp>

// The only termgame header this file includes, and still no termforge one — which
// is what makes test/14minesweeper unable to construct a Screen rather than
// merely not doing so.
#include <termgame/arcade/rng.hpp>

namespace termgame::minesweeper {

struct Coord {
  int row{0};
  int col{0};
  auto operator==(const Coord&) const -> bool = default;
};

enum class Level : std::uint8_t { Easy, Medium, Hard };

struct Preset {
  int rows;
  int cols;
  int mines;
  std::string_view name;
};

// The reference's three presets, unchanged: 9x9/10, 16x16/40, 16 rows x 30
// columns/99. Hard is wider than it is tall, which is what makes it the level
// that does not fit an 80-column terminal comfortably — see layout.hpp.
[[nodiscard]] constexpr auto preset(Level level) noexcept -> Preset {
  switch (level) {
    case Level::Medium:
      return {.rows = 16, .cols = 16, .mines = 40, .name = "MEDIUM"};
    case Level::Hard:
      return {.rows = 16, .cols = 30, .mines = 99, .name = "HARD"};
    case Level::Easy:
      break;
  }
  return {.rows = 9, .cols = 9, .mines = 10, .name = "EASY"};
}

inline constexpr Level kLevels[]{Level::Easy, Level::Medium, Level::Hard};

// The pre-start screen's choice labels (gitea #38). Mixed case, unlike
// preset().name — that one is the status row's outcome-adjacent WORD and is
// shouted on purpose; this is a thing you pick from a menu.
//
// ⚠ INDEX-ALIGNED WITH kLevels, and Minesweeper casts the chosen index straight
// back to Level. The static_assert is what makes that cast safe: without it,
// adding a fourth level and forgetting a name here would leave the screen
// offering three choices for four levels and silently starting the wrong one.
inline constexpr std::string_view kLevelNames[]{"Easy", "Medium", "Hard"};
static_assert(std::size(kLevelNames) == std::size(kLevels),
              "every minesweeper::Level needs a name on the options screen, "
              "in the enum's own order — the chosen index is cast to Level");

// The preset a fresh game starts on, read out of the options schema rather than
// named — so "which level is the default" has exactly one definition.
//
// ⚠ Takes the whole span rather than an index so it is total: an empty or
// malformed schema falls back to preset()'s own default rather than reading out
// of range. options_are_well_formed() already forbids both at compile time, but
// this runs at compile time too and a constexpr out-of-range read is a hard
// error rather than a fallback.
[[nodiscard]] constexpr auto default_preset(
    std::span<const OptionSpec> options) noexcept -> Preset {
  if (options.empty()) return preset(Level::Easy);
  const int index = options[0].default_index;
  if (index < 0 || index >= static_cast<int>(std::size(kLevels))) {
    return preset(Level::Easy);
  }
  return preset(kLevels[static_cast<std::size_t>(index)]);
}

enum class Mark : std::uint8_t { None, Flag, Question };

// Ready means "no mines have been placed yet". It is a state rather than the
// reference's separate firstClick bool so that deferred placement is a
// transition a test can assert on, and so it cannot disagree with game-over.
// The first successful reveal() places the mines and moves to Playing;
// load_mines() places them directly and lands in Playing too.
enum class State : std::uint8_t { Ready, Playing, Won, Lost };

struct Cell {
  bool mine{false};
  bool revealed{false};
  Mark mark{Mark::None};
  std::uint8_t adjacent{0};
};

// The PRNG moved to <termgame/arcade/rng.hpp> when 2048 needed the same
// generator, and this alias is why nothing here had to change: `Rng`,
// `minesweeper::Rng` and `termgame::Rng` are all the same type.
//
// Moved rather than copied — two hand-rolled splitmix64s would be two things to
// keep byte-identical, and being byte-identical across toolchains is the entire
// reason the class is hand-rolled instead of <random>. The reasoning lives with
// the code now; see that header before touching it, because this file's mine
// layouts are pinned against fixed seeds in test/14minesweeper.
using termgame::Rng;

// The 8-way neighbourhood, in-bounds only. Adjacency counting, chord's flag
// count and flood fill all go through this, so the bounds check exists exactly
// once — three hand-written dr/dc loops is where an off-by-one lives.
template <class F>
constexpr auto for_each_neighbour(int rows, int cols, Coord p, F&& fn) -> void {
  for (int dr = -1; dr <= 1; ++dr) {
    for (int dc = -1; dc <= 1; ++dc) {
      if (dr == 0 && dc == 0) {
        continue;
      }
      const Coord n{.row = p.row + dr, .col = p.col + dc};
      if (n.row < 0 || n.row >= rows || n.col < 0 || n.col >= cols) {
        continue;
      }
      fn(n);
    }
  }
}

class Board {
 public:
  // A Preset, not a Level. The model has no opinion about difficulty menus —
  // it needs a size, a mine count and a name, and taking them directly is what
  // makes the "denser than the safe zone" guard in place_mines() reachable
  // from a test rather than dead code waiting for a custom difficulty.
  Board(Preset p, std::uint64_t seed);

  auto reset(Preset p, std::uint64_t seed) -> void;

  [[nodiscard]] auto rows() const noexcept -> int { return m_rows; }
  [[nodiscard]] auto cols() const noexcept -> int { return m_cols; }
  [[nodiscard]] auto name() const noexcept -> std::string_view { return m_name; }
  [[nodiscard]] auto total_mines() const noexcept -> int { return m_mines; }
  [[nodiscard]] auto state() const noexcept -> State { return m_state; }
  [[nodiscard]] auto finished() const noexcept -> bool {
    return m_state == State::Won || m_state == State::Lost;
  }
  [[nodiscard]] auto in_bounds(Coord p) const noexcept -> bool {
    return p.row >= 0 && p.row < m_rows && p.col >= 0 && p.col < m_cols;
  }
  // Out of bounds yields a blank sentinel rather than UB — the same defensive
  // policy termforge::Screen::at() applies, for the same reason: a caller bug
  // must not corrupt memory.
  [[nodiscard]] auto at(Coord p) const -> const Cell&;

  [[nodiscard]] auto revealed_count() const noexcept -> int { return m_revealed; }
  [[nodiscard]] auto flag_count() const noexcept -> int { return m_flags; }
  // total_mines() - flag_count(), UNCLAMPED and free to go negative. That is
  // the reference's behaviour and it is the right one: a player who has placed
  // more flags than there are mines has made a mistake and should be told.
  [[nodiscard]] auto mines_remaining() const noexcept -> int {
    return m_mines - m_flags;
  }
  [[nodiscard]] auto exploded() const noexcept -> std::optional<Coord> {
    return m_exploded;
  }

  // ── The three player verbs ────────────────────────────────────────────────
  // Each returns true iff it changed something. That return value is not
  // decoration: it is what the "this does nothing" tests assert on, and it is
  // the hook the SFX layer binds to when Epic 2 (gitea #3) lands.
  auto reveal(Coord p) -> bool;
  auto cycle_mark(Coord p) -> bool;
  auto chord(Coord p) -> bool;

  // ── The clock ─────────────────────────────────────────────────────────────
  // dt, and nothing else. There is no clock in this class and there must never
  // be one: that is what lets a test assert the timer by calling advance() N
  // times with no Screen and no terminal. Callers pass the Shell's fixed dt
  // straight through.
  auto advance(std::chrono::duration<double> dt) -> void;
  [[nodiscard]] auto timer_running() const noexcept -> bool {
    return m_timer_running;
  }
  // Whole seconds, clamped. The reference freezes its display at 999 while the
  // game keeps playing; clamping on the READ rather than the accumulation is
  // that behaviour with none of its setInterval machinery.
  [[nodiscard]] auto seconds() const noexcept -> int;
  static constexpr int kTimerCap = 999;

  // The clock UNCLAMPED, for the record rather than the display.
  //
  // ⚠ Not a convenience duplicate of seconds(). A best time must not inherit a
  // three-column HUD's limit: storing the clamp would write a 1200-second win as
  // 999, which is both wrong and a record no later win can beat outright — every
  // subsequent slow game ties it. So the store gets this, the status row gets
  // seconds(), and a win above the cap displays a frozen BEST 999 over a
  // correctly stored value. test/15minesweeper-ui pins exactly that split.
  [[nodiscard]] auto elapsed() const noexcept -> std::chrono::duration<double> {
    return m_clock;
  }

  // ── Fixture seam ──────────────────────────────────────────────────────────
  // Install an exact mine layout, recompute adjacency, and land in
  // State::Playing — placement has happened, so the next reveal() must not do
  // it again. total_mines() becomes the number actually placed. Every rule test
  // uses this: deriving a layout from a seed would pin the RNG instead of the
  // rule, and every such test would have to be rewritten the day the RNG
  // changes.
  auto load_mines(std::span<const Coord> mines) -> void;

 private:
  auto place_mines(Coord safe) -> void;
  auto compute_adjacency() -> void;
  auto flood_reveal(Coord start) -> void;
  auto check_win() -> void;
  auto win() -> void;
  auto lose(Coord hit) -> void;
  [[nodiscard]] auto index(Coord p) const noexcept -> std::size_t {
    return static_cast<std::size_t>(p.row) * static_cast<std::size_t>(m_cols) +
           static_cast<std::size_t>(p.col);
  }
  [[nodiscard]] auto cell(Coord p) -> Cell& { return m_cells[index(p)]; }

  int m_rows{0};
  int m_cols{0};
  int m_mines{0};
  std::string_view m_name;
  std::vector<Cell> m_cells;
  Rng m_rng;
  State m_state{State::Ready};
  int m_revealed{0};
  int m_flags{0};
  std::optional<Coord> m_exploded;
  std::chrono::duration<double> m_clock{0.0};
  bool m_timer_running{false};
};

}  // namespace termgame::minesweeper
