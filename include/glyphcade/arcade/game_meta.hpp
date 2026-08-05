#pragma once

// glyphcade — GameMeta: everything the selector knows about a game without
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

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <termforge/core/types.hpp>
#include <termforge/widgets/detail/width.hpp>

namespace glyphcade {

// One player-facing setting, chosen on the pre-start screen before the game's
// first frame. See arcade/options_screen.hpp for who draws it, and the
// `options` field below for why the Shell also reads it.
//
// ⚠ THE STORAGE RULE ABOVE APPLIES HERE TOO, and `choices` has one more way to
// go wrong than a plain string_view field does. It must point at an array with
// STATIC storage duration — an `inline constexpr std::string_view kFoo[]` at
// namespace scope, or a `static constexpr` member. Writing the array inline in
// the kMeta initialiser compiles and dangles: the array is a temporary whose
// lifetime ends at the end of that full-expression, and the span outlives it by
// the whole program. -Wdangling does not see through a span, so nothing here
// diagnoses it.
struct OptionSpec {
  std::string_view label;                     // "Level", "Walls" — ASCII
  std::span<const std::string_view> choices;  // {"Easy", "Medium", "Hard"}
  int default_index{0};                       // index into `choices`
};

// The most options one game may declare. OptionsScreen holds a fixed array of
// this size so it allocates nothing on the render path, and all_games.cpp
// static_asserts that no registered game exceeds it.
inline constexpr std::size_t kMaxGameOptions = 4;

// Past this many choices, an option is too wide to be a one-row `< value >`
// cycler and renders as a windowed vertical list instead — twenty presses to
// reach Sokoban's level 20 is not a chooser. Two consequences, both enforced
// elsewhere: such an option consumes every row, so all_games.cpp forbids it
// sharing a screen with another; and the selector's detail pane prints the
// COUNT rather than the joined list, because twenty names is five wrapped rows
// of a pane that only has 48 columns to begin with.
inline constexpr std::size_t kInlineChoiceMax = 6;

// What kind of fact a game's size floor is. term-game#15 + term-game#42.
//
// ⚠ THIS ENUM IS THE WHOLE REASON term-game#15 TOOK SIX DEFERRALS. The obvious shape —
// a bare `min_cols` / `min_rows` pair — was refused for a reason that is worth
// keeping in front of whoever edits this next, and it is written out at
// games/sokoban/layout.hpp:23: Minesweeper needs 21 columns because nine cells
// at two columns each plus three of chrome is 21, and the terminal either has
// them or the board cannot be drawn. Sokoban's 34 is nothing of the kind. It
// has a CAMERA, so no level is ever undrawable at any size; 34x12 is a
// judgement that below sixteen tiles across you cannot see enough of a room to
// plan a push. A single field would sit those two numbers side by side and
// invite the selector to treat them as the same fact.
//
// So the kind travels with the number.
//
// ⚠ WHAT THE KIND IS NOT: A PROMISE ABOUT BEHAVIOUR. Both kinds REFUSE below
// their floor — Sokoban's compute_layout sets `fits = cols >= kNeedCols && rows
// >= kNeedRows` and its draw() falls through to draw_too_small() exactly as the
// other four do. An earlier draft of this enum had the selector print
// "recommended" for Playable and "minimum" for Drawable, which read as "you may
// go below this one"; a player then pressed Enter and got a hard refusal
// screen. That is the same binary disagreeing with itself one keystroke apart —
// the defect term-game#42 was filed about — reintroduced by the change meant to
// fix it. Three independent reviews caught it and it reproduced on a 30x10 pty.
//
// What the kind actually records is whether the NUMBER is derivable or chosen,
// which is a fact about how it may be revised: 21x13 falls out of the board and
// cannot move, 34x12 is an argument about seeing enough of a room and could be
// argued down. The selector says "needed" for both and adds "to play well" for
// a Playable one — the *reason* differs, the force does not.
enum class SizeFloor : std::uint8_t {
  // Below this the game cannot be drawn. Arithmetic, derived from the board.
  Drawable,
  // Below this it could be drawn but is not worth playing, so the game refuses
  // anyway. A judgement, and only a game that can always draw itself — i.e.
  // one with a camera — is entitled to one.
  Playable,
};

// The smallest terminal a game asks for, and what kind of ask it is.
//
// ⚠ THE CONTRACT IS "PLAYABLE AT ALL", NOT "PLAYABLE AT EVERY SETTING", and
// Minesweeper is the case that makes the difference. Its Hard preset needs
// 63x20; its Easy preset needs 21x13; and on a 40x15 terminal you can play
// Minesweeper perfectly well, you just cannot play it on Hard. So it declares
// 21x13. The in-game screen that names what the CHOSEN board needs and keeps
// the level keys live (minesweeper.cpp's draw_too_small) is the right
// behaviour, it stays, and this field is not a replacement for it — #15 is
// about finding out one screen earlier, never about refusing.
//
// ⚠ NEVER TYPE THESE NUMBERS. Every registered game initialises this from the
// constants in its own games/<name>/layout.hpp, which are the same constants
// its compute_layout compares against. A hand-copied literal is a second
// source of truth that drifts silently the first time a board changes size,
// and test/34geometry exists to assert the number is the actual boundary
// rather than merely present.
struct GameGeometry {
  int cols{0};  // 0x0 == no floor beyond the Shell's own kMinCols x kMinRows
  int rows{0};
  SizeFloor floor{SizeFloor::Drawable};
};

// Does a terminal of this size clear the game's floor? An undeclared floor
// ({0,0}) clears everything, which is what makes the field optional.
//
// ⚠ BOTH COMPARISONS ARE >=, and the boundary is inclusive: a game declaring
// 58x20 is asking to be given 58x20, not 59x21. test/34geometry pins that by
// asserting the declared size is exactly where the game's own compute_layout
// flips, so an off-by-one here disagrees with the game rather than merely
// looking different.
[[nodiscard]] constexpr auto meets_floor(const GameGeometry& g, int cols,
                                         int rows) noexcept -> bool {
  return cols >= g.cols && rows >= g.rows;
}

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

  // The settings this game asks about before its first frame, or empty for
  // none. term-game#38.
  //
  // ⚠ THE SHELL ONLY READS THIS. It advertises the labels and choices in the
  // selector's detail pane (Shell::refresh_detail) and does nothing else with
  // them: there is no new Shell::State, and arcade/game.hpp is unchanged. The
  // screen itself is drawn by the GAME, in its own draw(), in exactly the arm
  // where it already draws draw_too_small(). Two consumers, one schema — which
  // is the point, because it is what stops the menu advertising an option the
  // game does not have.
  //
  // ⚠ EMPTY IS THE CHEAP CASE ON PURPOSE. 2048 has no settings: it declares
  // nothing, gets no screen, holds no OptionsScreen member, and its translation
  // units are byte-identical to what they were before this field existed. A
  // mechanism that taxed the game with nothing to ask would be the wrong
  // mechanism.
  //
  std::span<const OptionSpec> options{};

  // The smallest terminal this game asks for, and what kind of ask it is. See
  // GameGeometry above for why the kind is part of it. term-game#15 + term-game#42.
  //
  // ⚠ THE SHELL ONLY READS THIS TOO, and it never refuses. The detail pane
  // names the size, and the selector's footer says so when the current
  // terminal is below it — which is one screen earlier than the player used to
  // find out, and that is the whole of the fix. Nothing here greys a row,
  // disables Enter, or stops a game starting: a player who wants to launch
  // Tetris on a 30-row terminal and read its own message about it is not doing
  // anything wrong.
  //
  // ⚠ DECLARED LAST, and that is not arbitrary. Designated initialisers must
  // follow declaration order, so a field inserted ABOVE this one forces an edit
  // to every kMeta that sets anything below it. As of this field the blast
  // radius is the whole roster — all five games set `.geometry`, four set
  // `.options`, two set `.keyboard` — plus the eleven locally-declared metas in
  // test/33options and the six in test/34geometry. Add the next one HERE.
  GameGeometry geometry{};
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

[[nodiscard]] constexpr auto option_text_is_ascii(const OptionSpec& o) noexcept
    -> bool {
  if (!text_is_seven_bit(o.label)) return false;
  for (const std::string_view choice : o.choices) {
    if (!text_is_seven_bit(choice)) return false;
  }
  return true;
}

[[nodiscard]] constexpr auto meta_text_is_ascii(const GameMeta& m) noexcept
    -> bool {
  // ⚠ Option text is the newest place an em dash can reach a bare terminal, and
  // it is a worse place than the description was. The description is prose you
  // skim once; an option label is on screen at the exact moment the player is
  // choosing, AND the same bytes are printed a second time by the detail pane
  // on the selector. Both surfaces, one check.
  //
  // ⚠ No registered game has non-ASCII option text, so nothing that runs can
  // witness this loop — delete it and every test stays green. Its only witness
  // is the compile-time NEGATIVE case in test/33options, which is therefore not
  // optional coverage.
  for (const OptionSpec& o : m.options) {
    if (!option_text_is_ascii(o)) return false;
  }
  return text_is_seven_bit(m.slug) && text_is_seven_bit(m.title) &&
         text_is_seven_bit(m.description) && text_is_seven_bit(m.tag);
}

// Five ways an options schema can be wrong, all decidable at compile time and
// none visible at the call site. Four produce a bad frame; the default_index
// one is an out-of-range read on the FIRST frame the game draws.
//
// ⚠ It lives HERE, next to icon_is_safe() and meta_text_is_ascii(), rather than
// in all_games.cpp beside the static_assert that calls it — and that placement
// is the point. Every clause below is unfalsifiable by the games in the
// registry: correct schemas cannot witness a check for incorrect ones, so a
// predicate reachable only from the registry is a predicate no test can ever
// prove still works. Public and per-meta, test/33options static_asserts a
// NEGATIVE for each clause. If you add a clause, add its negative there too.
[[nodiscard]] constexpr auto options_are_well_formed(const GameMeta& m) noexcept
    -> bool {
  if (m.options.size() > kMaxGameOptions) return false;
  bool has_list = false;
  for (const OptionSpec& o : m.options) {
    if (o.label.empty()) return false;    // an unlabelled row
    if (o.choices.empty()) return false;  // a row with nothing to pick
    if (o.default_index < 0 ||
        o.default_index >= static_cast<int>(o.choices.size())) {
      return false;  // reads past the end before the player touches anything
    }
    for (const std::string_view choice : o.choices) {
      if (choice.empty()) return false;  // a blank the cursor can land on
    }
    if (o.choices.size() > kInlineChoiceMax) has_list = true;
  }
  // A list-rendered option needs every row on the screen, so it cannot share
  // one. See kInlineChoiceMax above.
  return !has_list || m.options.size() == 1;
}

// Three ways a size floor can be wrong, none of them visible at the call site.
//
// ⚠ SAME PLACEMENT ARGUMENT AS options_are_well_formed ABOVE, and it is why
// this is public and per-meta rather than a loop inside all_games.cpp: every
// registered game declares a correct geometry, so a correct registry can never
// witness a check for an incorrect one. The negatives live in test/34geometry.
// If you add a clause, add its negative there too.
//
// ⚠ WHAT IS DELIBERATELY NOT HERE: any comparison against the Shell's own
// kMinCols / kMinRows. GameMeta is in glyphcade_core, which sits BELOW the
// Shell in the link chain precisely so a game cannot reach it (AGENTS.md, "a
// game links glyphcade_core and nothing else"), and reaching for shell.hpp
// here to tighten one assert would invert that. test/34geometry makes the
// comparison instead, where both headers are legal to include.
[[nodiscard]] constexpr auto geometry_is_well_formed(
    const GameMeta& m) noexcept -> bool {
  const GameGeometry& g = m.geometry;
  // Neither may be negative, and a floor is declared in both axes or in
  // neither. A half-declared floor reads as "no floor" on one axis and is
  // almost certainly a forgotten edit rather than an intention.
  if (g.cols < 0 || g.rows < 0) return false;
  if ((g.cols == 0) != (g.rows == 0)) return false;
  // Playable is a judgement ABOUT a number. Without one there is nothing to
  // judge, and the selector would have a kind to print and no size to print
  // beside it.
  return !(g.cols == 0 && g.floor == SizeFloor::Playable);
}

}  // namespace glyphcade
