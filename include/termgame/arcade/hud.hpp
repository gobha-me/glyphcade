#pragma once

// term-game — the two pieces of row arithmetic every game re-derived.
//
// ⚠ WHY THIS IS IN CORE. src/lib/CMakeLists.txt's rule is "if a game may call
// it, it belongs in core". This names Screen and Rgb but no Shell type, so it
// sits below the games in the link chain and changes nothing about it.
//
// ⚠ WHY IT EXISTS AT ALL. gitea #38 asked for a shared pre-start screen and
// noted that four games' worth of duplicated width-budgeted row drawing already
// existed — so a helper that hand-rolled a fifth copy would have made the
// problem worse while claiming to solve it. These are that pattern, extracted
// once, and OptionsScreen is their first consumer.

#include <span>
#include <string>
#include <string_view>

#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>

namespace termgame::hud {

// One width tier: the narrowest screen this text may appear on.
struct Tier {
  int min_cols;
  std::string_view text;
};

// The first tier whose min_cols the screen meets, scanning in order. Tables are
// written WIDEST FIRST, and the last entry must have min_cols == 0 so there is
// always an answer.
//
// ⚠ Why a cascade rather than truncation — stated here once instead of in the
// five places that each said it. write_text clips at the screen edge, so a
// single long string does not corrupt anything; it just ends mid-word, which
// reads as a rendering bug rather than as a narrow terminal. Every tier is a
// complete sentence.
//
// constexpr so a tier table can be static_asserted (ordering, and a zero floor)
// at compile time rather than discovered on a 30-column terminal.
[[nodiscard]] constexpr auto pick_for_width(int cols,
                                            std::span<const Tier> tiers) noexcept
    -> std::string_view {
  for (const Tier& t : tiers) {
    if (cols >= t.min_cols) return t.text;
  }
  return {};
}

// The same cascade for text that is only known at runtime: the first candidate
// that FITS `cols`, scanning a widest-first list.
//
// ⚠ WHY pick_for_width CANNOT DO THIS. A Tier holds a string_view into a
// literal and its floor is a number written by hand, which is what makes a
// table static_assertable. A sentence built from std::to_string has neither: it
// owns its bytes and its width is not known until it exists. Measuring
// `size()` here instead of trusting a declared floor is the whole difference.
//
// ⚠ AND IT MEASURES BYTES, WHICH IS ONLY CORRECT BECAUSE EVERY CALLER IS
// 7-BIT. Every GameMeta text field except the icon is static_asserted ASCII and
// the rest of any such sentence is digits and punctuation, so bytes and columns
// agree. A caller that ever passes a multi-byte glyph needs
// termforge::detail::display_width instead, and would be wrong here in a way
// that only shows on a terminal narrow enough to cut.
//
// Returns the LAST candidate when none fits, rather than empty: the narrowest
// form is the one the caller wrote for the narrowest terminal, and clipping it
// is still better than saying nothing. Callers should make that last entry
// short enough to fit Shell::kMinCols.
[[nodiscard]] inline auto pick_that_fits(std::span<const std::string> widest_first,
                                         int cols) -> std::string {
  if (widest_first.empty()) return {};
  for (const std::string& s : widest_first) {
    if (static_cast<int>(s.size()) <= cols) return s;
  }
  return widest_first.back();
}

// True iff `tiers` is ordered widest-first and ends at a zero floor — i.e. iff
// pick_for_width can never return empty. Meant for a static_assert next to the
// table it describes.
[[nodiscard]] constexpr auto tiers_are_total(std::span<const Tier> tiers) noexcept
    -> bool {
  if (tiers.empty()) return false;
  for (std::size_t i = 1; i < tiers.size(); ++i) {
    if (tiers[i].min_cols >= tiers[i - 1].min_cols) return false;
  }
  return tiers.back().min_cols == 0;
}

// The status row: `word` right-aligned, `fields` appended left-to-right only
// while each still fits the columns `word` did not take.
//
// ⚠ THE BUDGET IS THE LOAD-BEARING PART, and this is the one piece of
// arithmetic in the repo that mutation testing has failed to kill TWICE.
// Screen::write_text clips at the screen edge but NOT against text already on
// the row, so an unbounded left-hand string produced `movesPLAYING` at 40
// columns — observed in a headless render, not theorised. Fields are appended
// only while they fit, so the row degrades by dropping WHOLE fields rather than
// truncating a number: a missing field reads as a narrow terminal, a
// half-written one reads as a wrong score.
//
// ⚠ WHY IT SURVIVED TWICE, and what a test has to do about it. Deleting the
// budget changes nothing at any width the GAME ITSELF fits on — the loop stops
// appending long before the left text can reach the word. The case that kills
// it must sweep widths NARROWER than each game's own kNeedCols, because the
// status row is drawn whether or not the playfield fits. That sweep now exists
// once, in test/33options, against this function, instead of needing to be
// written four times.
//
// ⚠ The word is drawn FIRST and that is a chosen failure mode, not a second
// guard. The budget already guarantees the two cannot overlap; the order only
// decides which text loses if the arithmetic is ever wrong, and the word must
// win. At the no-colour tier the outcome word is the ONLY carrier of win and
// loss — FallbackDriver discards colour, so a red "game over" is an invisible
// game over — whereas a clipped counter is merely ugly.
//
// ⚠ CALLER CONTRACT: no label in `fields` may be a substring of another. The
// whole-fields assertions in test/15, /23, /26 and /28 key off find(label), so
// "line" alongside "lines" would match the wrong field and pass while the row
// was broken. This cannot be checked here — the fields arrive already formatted.
//
// ⚠ `fields` is priority-ordered. The loop stops at the first field that does
// not fit and does NOT try later, shorter ones: a row whose contents reorder
// themselves as the terminal narrows is harder to read than one that gets
// shorter. Put the least urgent field last.
auto draw_status_row(termforge::Screen& screen, int y,
                     std::span<const std::string> fields, std::string_view word,
                     termforge::Rgb field_fg, termforge::Rgb word_fg,
                     termforge::Rgb bg, std::string_view sep = "   ") -> void;

}  // namespace termgame::hud
