// The geometry block: what a game says about the smallest terminal it wants,
// and whether that number is true. gitea #15 + #42.
//
// ⚠ THE ONE ASSERTION THIS FILE EXISTS FOR is "the declared floor is the
// ACTUAL boundary". Every game initialises GameMeta::geometry from the same
// constants its own compute_layout compares against, so
//
//     CHECK(Snake::kMeta.geometry.cols == snake::kNeedCols);
//
// is a tautology dressed as a test — it asserts that a line of code says what
// it says. What is worth asserting is that the number the SELECTOR prints and
// the size at which the GAME actually starts drawing are the same size: at the
// declared floor compute_layout reports fits, and one column narrower or one
// row shorter it does not. That claim can go false without either declaration
// changing, which is exactly what a test is for.
//
// ⚠ AND IT NEEDS BOTH DIRECTIONS. "It fits at the declared size" alone passes
// for any floor at or above the truth, so a game declaring 200x200 would sail
// through. The narrower-does-not-fit half is what pins it from the other side.
//
// ⚠ WHY THE SHELL COMPARISON LIVES HERE AND NOT IN game_meta.hpp. GameMeta is
// in term-game_core, which sits below the Shell in the link chain precisely so
// a game cannot reach the Shell (AGENTS.md). A test is above both and may
// include both, so "no game asks for less than the Shell itself needs" is
// checkable here and nowhere lower.
//
// What does NOT belong here: anything the selector DRAWS. Whether the detail
// pane prints "minimum" or "recommended", and whether the footer warns, is
// test/11selector's question, because it needs a Shell.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include <termgame/arcade/game_meta.hpp>
#include <termgame/arcade/registry.hpp>
#include <termgame/arcade/shell.hpp>
#include <termgame/games/minesweeper/board.hpp>
#include <termgame/games/minesweeper/layout.hpp>
#include <termgame/games/snake/layout.hpp>
#include <termgame/games/sokoban/layout.hpp>
#include <termgame/games/tetris/layout.hpp>
#include <termgame/games/twenty48/layout.hpp>

using termgame::all_games;
using termgame::GameGeometry;
using termgame::GameMeta;
using termgame::geometry_is_well_formed;
using termgame::meets_floor;
using termgame::Shell;
using termgame::SizeFloor;

namespace {

// ── The negatives ────────────────────────────────────────────────────────────
//
// ⚠ Same argument test/33options makes at length, and it applies verbatim:
// all_games.cpp static_asserts geometry_is_well_formed() over five CORRECT
// declarations, so every clause in it passes no matter what its body says.
// Delete the both-or-neither clause, invert the Playable one, drop the
// negative-extent guard — the registry assert stays green and the whole suite
// stays green. A predicate whose only caller supplies correct input is not
// being checked. Each clause therefore gets a meta that is wrong in exactly one
// way, static_asserted to be REJECTED.
//
// These are static_asserts and not TEST_CASEs on purpose: this file failing is
// a compile error, and a malformed geometry is cheapest to catch there.

constexpr GameMeta kGood{.slug = "g",
                         .title = "G",
                         .description = "d",
                         .tag = "t",
                         .icon = "",
                         .geometry = {.cols = 40,
                                      .rows = 12,
                                      .floor = SizeFloor::Drawable}};
static_assert(geometry_is_well_formed(kGood));

// No floor at all is legal, and is what a game that never declared one gets.
constexpr GameMeta kUndeclared{
    .slug = "g", .title = "G", .description = "d", .tag = "t", .icon = ""};
static_assert(geometry_is_well_formed(kUndeclared));
static_assert(kUndeclared.geometry.cols == 0 && kUndeclared.geometry.rows == 0);

constexpr GameMeta kNegative{
    .slug = "g",
    .title = "G",
    .description = "d",
    .tag = "t",
    .icon = "",
    .geometry = {.cols = -1, .rows = 12, .floor = SizeFloor::Drawable}};
static_assert(!geometry_is_well_formed(kNegative), "a negative extent");

// Half a floor: a forgotten edit reads as "no floor" on one axis, and the game
// would be advertised as needing 40x0.
constexpr GameMeta kHalfDeclared{
    .slug = "g",
    .title = "G",
    .description = "d",
    .tag = "t",
    .icon = "",
    .geometry = {.cols = 40, .rows = 0, .floor = SizeFloor::Drawable}};
static_assert(!geometry_is_well_formed(kHalfDeclared),
              "a floor declared in one axis only");

// ⚠ And the mirror of it, because one arm of a != cannot witness the other:
// with only the cols-set case above, rewriting the clause as `g.rows == 0`
// still rejects it and this file stays green.
constexpr GameMeta kHalfDeclaredRows{
    .slug = "g",
    .title = "G",
    .description = "d",
    .tag = "t",
    .icon = "",
    .geometry = {.cols = 0, .rows = 12, .floor = SizeFloor::Drawable}};
static_assert(!geometry_is_well_formed(kHalfDeclaredRows),
              "a floor declared in the other axis only");

// Playable is a judgement about a number. Without one there is nothing to
// judge, and the selector has a word to print and no size to print beside it.
constexpr GameMeta kPlayableNowhere{
    .slug = "g",
    .title = "G",
    .description = "d",
    .tag = "t",
    .icon = "",
    .geometry = {.cols = 0, .rows = 0, .floor = SizeFloor::Playable}};
static_assert(!geometry_is_well_formed(kPlayableNowhere),
              "a Playable kind with no size");

// ── The five games' own fits predicates, behind one signature ────────────────
//
// compute_layout does not have one shape across the roster — minesweeper takes
// the board size, sokoban takes the level size, the other three take only the
// screen — so the boundary sweep needs each one wrapped. Nothing here is
// allowed to reimplement the arithmetic: every lambda calls the game's own
// compute_layout and reads its own `fits`, which is the whole point.

struct GameFits {
  std::string_view slug;
  bool (*fits)(int cols, int rows);
};

constexpr GameFits kFits[]{
    {"minesweeper",
     [](int c, int r) {
       const auto easy =
           termgame::minesweeper::preset(termgame::minesweeper::Level::Easy);
       return termgame::minesweeper::compute_layout(c, r, easy.rows, easy.cols)
           .fits;
     }},
    {"2048", [](int c, int r) { return termgame::twenty48::compute_layout(c, r).fits; }},
    {"snake", [](int c, int r) { return termgame::snake::compute_layout(c, r).fits; }},
    {"tetris", [](int c, int r) { return termgame::tetris::compute_layout(c, r).fits; }},
    // ⚠ The level size is deliberately arbitrary (level 1 is 8x5 tiles).
    // Sokoban's `fits` is a screen-only comparison — a level larger than the
    // window scrolls rather than being refused — so no choice here can change
    // the answer, and that independence IS the reason its floor is Playable.
    {"sokoban",
     [](int c, int r) { return termgame::sokoban::compute_layout(c, r, 8, 5).fits; }},
};

[[nodiscard]] auto fits_for(std::string_view slug) -> bool (*)(int, int) {
  for (const GameFits& f : kFits) {
    if (f.slug == slug) return f.fits;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("every registered game declares a size floor", "[geometry]") {
  // Not a style preference. A game with no floor is a game the selector cannot
  // warn about, which is the defect gitea #15 is; the field is optional in the
  // SCHEMA so that a future game may abstain deliberately, and this case is
  // where abstaining has to be argued rather than forgotten.
  for (const auto& entry : all_games()) {
    INFO("game: " << entry.meta.slug);
    CHECK(entry.meta.geometry.cols > 0);
    CHECK(entry.meta.geometry.rows > 0);
  }
}

TEST_CASE("no game asks for less than the Shell itself needs", "[geometry]") {
  // Below kMinCols x kMinRows the selector does not draw at all, so a floor
  // beneath it could never be reported to anybody — the player would be
  // looking at draw_too_small instead.
  for (const auto& entry : all_games()) {
    INFO("game: " << entry.meta.slug);
    CHECK(entry.meta.geometry.cols >= Shell::kMinCols);
    CHECK(entry.meta.geometry.rows >= Shell::kMinRows);
  }
}

TEST_CASE("a declared floor is the size the game actually starts drawing at",
          "[geometry]") {
  for (const auto& entry : all_games()) {
    const GameGeometry& g = entry.meta.geometry;
    INFO("game: " << entry.meta.slug << " floor " << g.cols << "x" << g.rows);

    auto* fits = fits_for(entry.meta.slug);
    REQUIRE(fits != nullptr);  // a new game needs a row in kFits

    // At the declared size, exactly.
    CHECK(fits(g.cols, g.rows));

    // ⚠ Both halves. "It fits at the declared size" alone is satisfied by any
    // floor at or above the truth; these two are what stop a game overstating
    // what it needs and having the selector warn about terminals that would
    // have worked.
    CHECK_FALSE(fits(g.cols - 1, g.rows));
    CHECK_FALSE(fits(g.cols, g.rows - 1));
  }
}

TEST_CASE("meets_floor is inclusive at the boundary", "[geometry]") {
  constexpr GameGeometry g{.cols = 58, .rows = 20, .floor = SizeFloor::Drawable};

  CHECK(meets_floor(g, 58, 20));  // the declared size is enough
  CHECK(meets_floor(g, 80, 24));
  CHECK_FALSE(meets_floor(g, 57, 20));
  CHECK_FALSE(meets_floor(g, 58, 19));

  // An undeclared floor clears every size, which is what makes the field
  // optional rather than a tax on a game with nothing to say.
  constexpr GameGeometry none{};
  CHECK(meets_floor(none, Shell::kMinCols, Shell::kMinRows));
}

TEST_CASE("both SizeFloor kinds are in use on the roster", "[geometry]") {
  // ⚠ ONE CASE PER VALUE, and not for symmetry. Sokoban is the only Playable
  // on the roster, so a test that only checked "the other four are Drawable"
  // would still pass if SizeFloor collapsed to a single value — and with it
  // would go the distinction the enum exists for (see GameGeometry in
  // arcade/game_meta.hpp). Same trap test/11selector records for
  // kInlineChoiceMax: with one game either side of a line, only asserting both
  // sides discriminates it.
  int drawable = 0;
  int playable = 0;
  for (const auto& entry : all_games()) {
    INFO("game: " << entry.meta.slug);
    switch (entry.meta.geometry.floor) {
      case SizeFloor::Drawable:
        ++drawable;
        break;
      case SizeFloor::Playable:
        ++playable;
        break;
    }
    // Sokoban is Playable because it has a camera: no level is ever
    // undrawable, only unpleasant. Every other game's board either has room or
    // cannot be shown, which is arithmetic.
    const bool expect_playable = entry.meta.slug == "sokoban";
    CHECK((entry.meta.geometry.floor == SizeFloor::Playable) ==
          expect_playable);
  }
  CHECK(drawable > 0);
  CHECK(playable > 0);
}
