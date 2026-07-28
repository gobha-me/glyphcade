#pragma once

// term-game — StubGame: the Shell's end-to-end proof, and nothing else.
//
// It exists because four things are only true together, and no unit test proves
// them together: that the registry actually links a game into the binary, that
// the Shell constructs a FRESH one on entry and destroys it on exit, that ticks
// reach Game::tick at a constant dt, and that both exit paths — done() and
// GameContext::quit_to_menu() — work. It is the consumer that makes the Shell's
// interfaces real rather than plausible.
//
// ⚠ DELETION CONDITION. Delete this directory (include/termgame/games/stub/ and
// src/lib/games/stub/), its entry in src/lib/arcade/all_games.cpp, its line in
// src/lib/CMakeLists.txt, and the "stub" slug lookups in test/11selector and
// test/13tick, WHEN EPIC 3 (Minesweeper) LANDS. Minesweeper proves the same
// four things while also being a game. Two stubs is one stub too many.

#include <chrono>

#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>
#include <termforge/widgets/frame.hpp>

#include <termgame/arcade/context.hpp>
#include <termgame/arcade/game.hpp>
#include <termgame/arcade/game_meta.hpp>

namespace termgame {

class StubGame final : public Game {
 public:
  static constexpr GameMeta kMeta{
      .slug = "stub",
      .title = "Stub",
      .description =
          "A diagnostic placeholder. It proves the shell's registry, game "
          "lifecycle and tick path end to end, and is removed when Minesweeper "
          "lands.",
      .tag = "Diagnostic",
      // U+1F9EA TEST TUBE: a single code point inside termforge's wide table,
      // with no variation selector, so measured width and drawn width agree.
      // Read icon_is_safe() in arcade/game_meta.hpp before changing it — the
      // registry static_asserts on it.
      .icon = "\U0001F9EA",
  };

  StubGame() = default;

  [[nodiscard]] auto meta() const -> const GameMeta& override { return kMeta; }

  auto start(GameContext& ctx) -> void override;
  auto tick(std::chrono::duration<double> dt) -> void override;
  auto on_event(const termforge::Event& ev) -> bool override;
  auto draw(termforge::Screen& screen) -> void override;
  [[nodiscard]] auto done() const -> bool override { return m_done; }

  // Observables for the tests. The tick counter and the elapsed total are how
  // test/13tick asserts routing (N ticks per frame, the stall clamp, pause
  // stopping the simulation) through the game rather than through App::on_tick
  // — the layer AGENTS.md's rules are actually written about.
  [[nodiscard]] auto ticks() const noexcept -> int { return m_ticks; }
  [[nodiscard]] auto elapsed() const noexcept -> std::chrono::duration<double> {
    return m_elapsed;
  }
  [[nodiscard]] auto min_dt() const noexcept -> std::chrono::duration<double> {
    return m_min_dt;
  }

  // Marker speed, in cells per SECOND — not cells per tick. A game written in
  // cells-per-tick still looks fine when dt is wrong; this does not.
  static constexpr double kSpeedCellsPerSec = 8.0;

 private:
  GameContext* m_ctx{nullptr};
  int m_ticks{0};
  std::chrono::duration<double> m_elapsed{0.0};
  std::chrono::duration<double> m_min_dt{1.0e9};
  double m_x{0.0};
  double m_dir{1.0};
  bool m_done{false};
  termforge::Frame m_frame{"Stub"};
};

}  // namespace termgame
