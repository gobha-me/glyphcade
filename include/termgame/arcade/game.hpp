#pragma once

// term-game — Game: one hosted game.
//
// ⚠ A Game is NOT a termforge::App. Only one thing owns a terminal, and that is
// the Shell (DESIGN.md, "One App, many Games"). A game that derives from App,
// or touches Terminal, or reaches another game's headers, is a bug in the
// architecture rather than a style disagreement.
//
// The contracts the Shell actually guarantees are written into each method
// below, because a game author reads this file and nothing else.

#include <chrono>

#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>

#include <termgame/arcade/context.hpp>
#include <termgame/arcade/game_meta.hpp>

namespace termgame {

// LIFETIME: the Shell constructs a fresh instance on entry and destroys it on
// exit. A game therefore never needs start() to reset anything — state that
// survives an entry is a bug that cannot happen, rather than one every game has
// to independently avoid. See the factory in registry.hpp.
//
// SCREEN: while a game is running it owns the WHOLE Screen. The Shell draws no
// chrome over it, so draw() coordinates and MouseEvent coordinates are the same
// coordinates, with no offset to get wrong. Anything the player needs to see —
// score, controls, "press Esc for the menu" — is the game's to draw.
//
// Immediate mode, the same contract termforge Widgets have: draw() runs every
// frame and must fully repaint. (The Shell does clear() the Screen first, so a
// game that only repaints what changed is still correct — but do not build on
// that; it is the Shell's convenience, not a promise to games.)
class Game {
 public:
  virtual ~Game() = default;

  Game(const Game&) = delete;
  auto operator=(const Game&) -> Game& = delete;

  // Identity for the selector. Return a reference to a `static constexpr
  // GameMeta` member; the registry holds a copy of the same object so the menu
  // can be drawn without constructing any game at all.
  [[nodiscard]] virtual auto meta() const -> const GameMeta& = 0;

  // Entering the game. `ctx` outlives every game and is stable across entries,
  // so storing &ctx is safe. Capabilities are already populated by the time
  // this is called.
  virtual auto start(GameContext& ctx) -> void = 0;

  // Advance the simulation by dt.
  //
  // FIXED TIMESTEP: dt is a constant 1/kTickHz seconds, and the Shell calls
  // this an integer number of times per frame — sometimes zero, at most
  // ceil(max_tick_dt * kTickHz). dt is never zero and never larger than that
  // constant.
  //
  // ⚠ Do not read a clock in here. dt is the only time that exists, and that is
  // exactly what makes a game's logic drivable by N ticks in a test with no
  // Screen and no terminal.
  virtual auto tick(std::chrono::duration<double> /*dt*/) -> void {}

  // Input. Return true if consumed.
  //
  // The Shell handles anything declined, which is how every game gets pause and
  // quit-to-menu without writing a line of either.
  //
  // ⚠ Do not consume Key::Escape. Escape is the Shell's quit-to-menu, and a
  // game that swallows it strands the player in that game.
  virtual auto on_event(const termforge::Event& ev) -> bool = 0;

  virtual auto draw(termforge::Screen& screen) -> void = 0;

  // True when the game wants to hand control back to the selector. Polled once
  // per frame by the Shell, never in the middle of one of the calls above.
  [[nodiscard]] virtual auto done() const -> bool { return false; }

 protected:
  Game() = default;
};

}  // namespace termgame
