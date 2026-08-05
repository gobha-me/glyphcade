#pragma once

// glyphcade — the game registry.
//
// ⚠ There is no registration *mechanism* here, and that absence is the design.
// See src/lib/arcade/all_games.cpp for the table and DESIGN.md for the full
// argument; the short version is that a self-registering static in a static
// library is dropped by the linker when nothing references its object file, and
// the game then vanishes from the menu with no diagnostic at compile, link or
// run time. A table you have to type a name into fails at *compile* time
// instead.

#include <concepts>
#include <memory>
#include <span>

#include <glyphcade/arcade/game.hpp>
#include <glyphcade/arcade/game_meta.hpp>

namespace glyphcade {

// A plain function pointer, not std::function. Two reasons, both structural:
// it keeps GameEntry a literal type — so the registry table is constexpr, lives
// in .rodata, and can check itself with static_assert — and it allocates
// nothing during static initialisation.
using GameFactory = auto (*)() -> std::unique_ptr<Game>;

struct GameEntry {
  // A COPY of the game's own static constexpr meta, held here so the selector
  // can list every game without constructing one. That is what makes "no game
  // object exists while the menu is up" literally true, which in turn is what
  // makes "fresh state on every entry" free rather than enforced.
  GameMeta meta;
  GameFactory make;
};

template <std::derived_from<Game> T>
[[nodiscard]] auto make_game() -> std::unique_ptr<Game> {
  return std::make_unique<T>();
}

// Every game compiled into this binary, in menu order. Defined in
// src/lib/arcade/all_games.cpp — the one file adding a game touches.
[[nodiscard]] auto all_games() noexcept -> std::span<const GameEntry>;

}  // namespace glyphcade
