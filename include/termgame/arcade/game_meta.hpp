#pragma once

// term-game — GameMeta: everything the selector knows about a game without
// instantiating one.
//
// Mirrors the shape of HTML-Games' games.json (slug/title/description/tag/icon)
// minus href (there are no URLs here) and theme (there is no CSS), so the two
// arcades describe their rosters the same way.
//
// The fields are string_view, not string, and that is load-bearing rather than
// a micro-optimisation: it makes GameMeta a literal type. A game's metadata is
// then a `static constexpr` in its own translation unit, the registry table in
// src/lib/arcade/all_games.cpp is a constexpr array, and the icon rule below is
// a static_assert instead of a runtime check nobody remembers to run.
//
// ⚠ Every value must be a string literal with static storage duration. A
// GameMeta pointing at a std::string is a dangling read the moment that string
// goes out of scope, and nothing here can detect it.

#include <string_view>

#include <termforge/widgets/detail/width.hpp>

namespace termgame {

struct GameMeta {
  std::string_view slug;         // stable id, kebab-case: "minesweeper"
  std::string_view title;        // "Minesweeper"
  std::string_view description;  // a sentence or two; the selector wraps it
  std::string_view tag;          // "Puzzle", "Arcade Classic"
  std::string_view icon;         // ONE two-column emoji, or empty. See below.
};

// Columns the selector reserves for an icon, so a game without one still lines
// up with a game that has one.
inline constexpr int kIconCols = 2;

// True iff `icon` is safe to place in a Screen cell: exactly one grapheme,
// exactly two columns wide *by the same table Screen::write_text uses to lay it
// out*. The empty string (no icon) is safe.
//
// ⚠ Why this is a hard check and not a style note. A terminal cell is not a
// code point. write_text measures each grapheme with char_width and, for a
// width-2 glyph, emits the glyph plus a "\0" continuation cell that the
// renderer skips because it assumes the physical cursor advanced two columns.
// If termforge measures 1 where the terminal draws 2, every cell to the right
// of the icon on that row is off by one — permanently, for the rest of the run,
// with the diffing renderer cheerfully repainting garbage there.
//
// This is not hypothetical, and it is invisible by inspection. From the real
// games.json:
//
//   🐍 🏭 👾 💣 🧪   single code point in U+1F300–1FAFF   measured 2, drawn 2 ✓
//   ⚒️ ⚔️ ⚙️          U+2692/2694/2699 + U+FE0F (VS16)     measured 1, drawn 2 ✗
//
// The second row looks identical to the first in an editor. The base characters
// sit outside termforge's wide table and the variation selector is zero-width,
// so they measure one column while essentially every terminal draws two.
//
// Pick an icon from the emoji planes (U+1F300+) or leave it empty. Do not
// "relax" this predicate to admit the ⚒️ family — the predicate is right and the
// icon is wrong.
//
// Only termforge's sanctioned public width helpers are used, so this stays
// correct if its tables are refreshed.
[[nodiscard]] constexpr auto icon_is_safe(std::string_view icon) noexcept
    -> bool {
  if (icon.empty()) return true;
  // Two columns total, AND not splittable at one column. The second clause is
  // what rejects two one-column glyphs ("ab"), which also measure 2 but are two
  // graphemes and would break the selector's single-cell icon slot.
  return termforge::detail::display_width(icon) == kIconCols &&
         termforge::detail::truncate_to_width(icon, 1).empty();
}

}  // namespace termgame
