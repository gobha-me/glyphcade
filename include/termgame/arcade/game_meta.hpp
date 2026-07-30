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

#include <termforge/core/types.hpp>
#include <termforge/widgets/detail/width.hpp>

namespace termgame {

struct GameMeta {
  std::string_view slug;         // stable id, kebab-case: "minesweeper"
  std::string_view title;        // "Minesweeper"
  std::string_view description;  // a sentence or two; the selector wraps it
  std::string_view tag;          // "Puzzle", "Arcade Classic"
  std::string_view icon;         // ONE two-column emoji, or empty. See below.

  // Which keyboard tier this game wants while it is running (termforge #60,
  // shipped v0.2.2). Defaults to Legacy, which is byte-for-byte what every tag
  // before #60 emitted — so a game that says nothing gets exactly what the
  // three games written before this field existed already got.
  //
  // ⚠ WHY THIS IS METADATA AND NOT A Game METHOD. set_keyboard_mode lives on
  // termforge::App, and the Shell is the only App (AGENTS.md, "One App, many
  // Games"), so a game cannot ask for it directly. The Shell needs the answer
  // BEFORE the game's first frame, and it already holds this struct from the
  // registry table. Keeping it here also keeps it constexpr, so which games
  // want which tier is decidable at compile time rather than by running one.
  //
  // ⚠ WHY IT IS PER-GAME AND NOT SET ONCE. Enhanced is not a superset of
  // Legacy, it is a different contract: every key arrives as CSI-u, so Shift+a
  // becomes ch=='A' WITH shift set where a plain byte carried no modifier, and
  // every key gains a Release. Turning it on globally would make Snake turn
  // twice per keypress and double-fire the selector's bindings. A tier that is
  // right for one game is wrong for the others.
  //
  // ⚠ A game never READS this. What a game reads to find out which arm it
  // actually got is ctx.capabilities().kitty_keyboard — App::keyboard_mode()
  // answers "what did we ask for", not "what did the terminal grant". A game
  // that asks for Enhanced must still work when the answer is no; every game is
  // playable at the bottom tier, and that rule has no keyboard exemption.
  termforge::KeyboardMode keyboard{termforge::KeyboardMode::Legacy};
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

// Every text field except the icon must be 7-bit ASCII.
//
// ⚠ Found on a real pty, not reasoned about. 2048's description originally read
// "Reach 2048 — then keep going" with an em dash (U+2014), and the selector's
// detail pane duly printed those three bytes of UTF-8 onto a terminal that had
// just told us it cannot draw a box. The whole repo promises the bottom tier
// works; glyphs.hpp static_asserts it for tiles, and icon_is_safe() covers the one
// field that is deliberately not ASCII — but nothing covered the prose, which is
// the field most likely to be written by someone reaching for a nice dash.
//
// ⚠ Why no test caught it: test/11selector's 7-bit sweep runs at 60x20, where the
// detail pane wraps the description and the offending character fell outside the
// visible rows. A wider terminal showed it immediately. That is the general
// hazard with asserting on rendered output — the assertion only covers what the
// viewport happened to include — and the reason this belongs at compile time
// against the SOURCE string instead.
//
// The icon is excluded because it is intentionally non-ASCII; icon_is_safe() is
// its check. Everything else has no business being anything but ASCII: slug keys
// a score file, title and tag are laid out by column arithmetic, and description
// is wrapped by it.
[[nodiscard]] constexpr auto text_is_seven_bit(std::string_view s) noexcept
    -> bool {
  for (const char c : s) {
    if (static_cast<unsigned char>(c) >= 0x80) return false;
  }
  return true;
}

[[nodiscard]] constexpr auto meta_text_is_ascii(const GameMeta& m) noexcept
    -> bool {
  return text_is_seven_bit(m.slug) && text_is_seven_bit(m.title) &&
         text_is_seven_bit(m.description) && text_is_seven_bit(m.tag);
}

}  // namespace termgame
