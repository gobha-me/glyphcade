// glyphcade — THE registry. Adding a game is one line here and nothing
// anywhere else.
//
// ⚠ Never replace this with self-registering statics. The tempting shape is
//
//     static const auto reg = register_game<Minesweeper>();   // DON'T
//
// in each game's .cpp. It works in an executable and silently fails in a static
// library: the linker drops object files nothing references, taking the
// registrar with them, and the game just never appears in the menu — no
// warning at compile time, no error at link time, no diagnostic at run time,
// only a player who cannot find Minesweeper. The workarounds (--whole-archive,
// $<LINK_LIBRARY:WHOLE_ARCHIVE,...>, OBJECT libraries) are each platform- or
// generator-sensitive.
//
// This table has no mechanism to fail. The only way into it is typing a type's
// name, which requires including its header, which requires the type to be
// complete — so a typo, a renamed class or a deleted game is a compile error.

#include <glyphcade/arcade/registry.hpp>

#include <cstddef>
#include <iterator>

#include <glyphcade/games/minesweeper/minesweeper.hpp>
#include <glyphcade/games/snake/snake.hpp>
#include <glyphcade/games/sokoban/sokoban.hpp>
#include <glyphcade/games/tetris/tetris.hpp>
#include <glyphcade/games/twenty48/twenty48.hpp>

namespace glyphcade {
namespace {

// Menu order is this order.
//
// ⚠ Each entry pairs a game's metadata with its factory, and no compiler can
// check that the two belong together — bind Minesweeper's meta to 2048's factory
// and the menu says "Minesweeper" while 2048 starts. test/12registry builds every
// entry and compares the constructed game's meta against the table, which is the
// only place that mistake is catchable.
constexpr GameEntry kGames[] = {
    {Minesweeper::kMeta, &make_game<Minesweeper>},
    {Twenty48::kMeta, &make_game<Twenty48>},
    {Snake::kMeta, &make_game<Snake>},
    {Tetris::kMeta, &make_game<Tetris>},
    {Sokoban::kMeta, &make_game<Sokoban>},
};

// ── The table checks itself ──────────────────────────────────────────────────
// Both of the mistakes below are invisible until a player hits them, and both
// are decidable at compile time, so they are decided at compile time.

// A duplicate slug makes save data, high scores and any future deep-link
// ambiguous, and the menu shows two rows that are not obviously different.
constexpr auto slugs_are_unique() -> bool {
  for (std::size_t i = 0; i < std::size(kGames); ++i) {
    for (std::size_t j = i + 1; j < std::size(kGames); ++j) {
      if (kGames[i].meta.slug == kGames[j].meta.slug) return false;
    }
  }
  return true;
}
static_assert(slugs_are_unique(), "two registered games share a slug");

// An icon whose measured width disagrees with its rendered width shifts every
// cell to its right for the rest of the run. See icon_is_safe() in
// arcade/game_meta.hpp for why this is not paranoia.
constexpr auto icons_are_safe() -> bool {
  for (const auto& entry : kGames) {
    if (!icon_is_safe(entry.meta.icon)) return false;
  }
  return true;
}

// The icon is allowed to be non-ASCII and is checked above; every other text
// field is not. See meta_text_is_ascii() in arcade/game_meta.hpp for the em dash
// that got onto a bare terminal and why asserting on rendered output missed it.
constexpr auto metadata_is_ascii() -> bool {
  for (const auto& entry : kGames) {
    if (!meta_text_is_ascii(entry.meta)) return false;
  }
  return true;
}
static_assert(icons_are_safe(),
              "a registered game's icon is not exactly one two-column "
              "grapheme — see icon_is_safe() in arcade/game_meta.hpp");

// The per-meta predicate is options_are_well_formed() in arcade/game_meta.hpp,
// deliberately not inlined here — see the note at its definition for why a
// check the registry cannot falsify must live somewhere a test can reach.
constexpr auto all_options_are_well_formed() -> bool {
  for (const auto& entry : kGames) {
    if (!options_are_well_formed(entry.meta)) return false;
  }
  return true;
}

static_assert(metadata_is_ascii(),
              "a registered game's slug, title, description or tag contains a "
              "non-ASCII byte — the selector prints all four on the no-colour "
              "tier, which by definition cannot render them. An em dash in a "
              "description is the usual culprit; see meta_text_is_ascii() in "
              "arcade/game_meta.hpp.");

static_assert(all_options_are_well_formed(),
              "a registered game's options schema is malformed: an empty label "
              "or choice, a default_index outside its own choices, more than "
              "kMaxGameOptions options, or a long-list option sharing a screen "
              "with another. See OptionSpec in arcade/game_meta.hpp.");

// The per-meta predicate is geometry_is_well_formed() in arcade/game_meta.hpp,
// for the same reason options_are_well_formed() is: a registry of correct
// declarations cannot witness a check for an incorrect one.
constexpr auto all_geometry_is_well_formed() -> bool {
  for (const auto& entry : kGames) {
    if (!geometry_is_well_formed(entry.meta)) return false;
  }
  return true;
}

static_assert(all_geometry_is_well_formed(),
              "a registered game's size floor is malformed: a negative extent, "
              "a floor declared in one axis but not the other, or a Playable "
              "kind with no size to be a judgement about. See GameGeometry in "
              "arcade/game_meta.hpp.");

// ⚠ WELL-FORMED IS NOT THE SAME AS DECLARED, and this is the second assert
// because {0,0} is deliberately LEGAL in the schema — a GameMeta with no floor
// is well-formed, so the predicate above cannot catch a game that simply forgot
// one. That is exactly the failure term-game#15 exists to prevent: a sixth game
// omits `.geometry`, compiles clean under -Werror in all four configurations,
// keeps every static_assert green, and ships with meets_floor() true at every
// size — so the selector silently never warns about it. Registry POLICY is
// stricter than the schema, and AGENTS.md's own claim about the neighbouring
// GameMeta rules ("that is a static_assert, not a convention") is only true of
// this one if it is written here.
constexpr auto all_geometry_is_declared() -> bool {
  for (const auto& entry : kGames) {
    if (entry.meta.geometry.cols == 0) return false;
  }
  return true;
}

static_assert(all_geometry_is_declared(),
              "a registered game declares no size floor. GameMeta::geometry is "
              "optional in the schema so a future game may abstain "
              "deliberately — but abstaining has to be argued here, not "
              "forgotten in a kMeta. See GameGeometry in arcade/game_meta.hpp.");

}  // namespace

auto all_games() noexcept -> std::span<const GameEntry> { return kGames; }

}  // namespace glyphcade
