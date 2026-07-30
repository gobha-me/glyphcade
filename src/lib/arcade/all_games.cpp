// term-game — THE registry. Adding a game is one line here and nothing
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

#include <termgame/arcade/registry.hpp>

#include <cstddef>
#include <iterator>

#include <termgame/games/minesweeper/minesweeper.hpp>
#include <termgame/games/snake/snake.hpp>
#include <termgame/games/twenty48/twenty48.hpp>

namespace termgame {
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
};

// ── The table checks itself ─────────────────────────────────────────────────
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

static_assert(metadata_is_ascii(),
              "a registered game's slug, title, description or tag contains a "
              "non-ASCII byte — the selector prints all four on the no-colour "
              "tier, which by definition cannot render them. An em dash in a "
              "description is the usual culprit; see meta_text_is_ascii() in "
              "arcade/game_meta.hpp.");

}  // namespace

auto all_games() noexcept -> std::span<const GameEntry> { return kGames; }

}  // namespace termgame
