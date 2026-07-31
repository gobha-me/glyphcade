// The options schema: what a game may declare about its own settings, and the
// two compile-time predicates that decide whether it declared it correctly.
//
// ⚠ THIS FILE EXISTS BECAUSE OF WHAT THE REGISTRY CANNOT PROVE. all_games.cpp
// static_asserts options_are_well_formed() and meta_text_is_ascii() over the
// five registered games — and every one of those games is CORRECT, so both
// predicates pass no matter what their bodies say. Delete the option loop out
// of meta_text_is_ascii, invert the default_index comparison, drop the
// empty-choice check: the registry static_assert stays green, the whole test
// suite stays green, and the guard is gone.
//
// A predicate whose only caller supplies correct input is not being tested. So
// every clause gets a NEGATIVE here — a locally-declared meta that is wrong in
// exactly one way, static_asserted to be REJECTED. That is the only construct
// in this repo that fails when a check stops checking.
//
// ⚠ These are static_asserts, not TEST_CASEs, and the difference matters: this
// file failing is a COMPILE error, not a red test. That is deliberate — a
// malformed schema is an out-of-range read on a game's first frame, and the
// build is the last place it can still be cheap.
//
// ⚠ NO TERMFORGE SCREEN HERE. game_meta.hpp names termforge::KeyboardMode and
// the width helpers but nothing that renders, so a case in this file cannot
// construct a Screen even by accident. The pre-start screen's own rendering
// belongs in test/33options once OptionsScreen exists, and its per-game
// behaviour belongs in the five *-ui suites.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <string_view>

#include <termgame/arcade/game_meta.hpp>
#include <termgame/arcade/registry.hpp>

namespace {

using termgame::GameMeta;
using termgame::kInlineChoiceMax;
using termgame::kMaxGameOptions;
using termgame::meta_text_is_ascii;
using termgame::OptionSpec;
using termgame::options_are_well_formed;

// ⚠ Namespace scope, `inline constexpr`, not written inline in the metas below.
// That is the storage-duration rule from OptionSpec's own comment, and this
// file is the one place in the repo that would notice if it were violated —
// a span into a dead temporary is UB that the sanitizers see and -Wdangling
// does not.
inline constexpr std::string_view kTwo[]{"Easy", "Hard"};
inline constexpr std::string_view kBlankChoice[]{"Easy", ""};
inline constexpr std::string_view kEmDashChoice[]{"Easy", "Hard \xe2\x80\x94 really"};

// Seven choices: one past kInlineChoiceMax, so this is a LIST option.
inline constexpr std::string_view kSeven[]{"1", "2", "3", "4", "5", "6", "7"};

// A meta that is correct in every respect, so each negative below differs from
// it in exactly one place and nothing else can be the reason it is rejected.
inline constexpr OptionSpec kGoodOptions[]{
    {.label = "Level", .choices = kTwo, .default_index = 1},
};
inline constexpr GameMeta kGood{
    .slug = "good",
    .title = "Good",
    .description = "A game whose schema is well formed.",
    .tag = "Test",
    .icon = "",
    .options = kGoodOptions,
};

static_assert(options_are_well_formed(kGood), "the control must pass");
static_assert(meta_text_is_ascii(kGood), "the control must pass");

// ── Negative: an unlabelled row ─────────────────────────────────────────────
inline constexpr OptionSpec kUnlabelled[]{
    {.label = "", .choices = kTwo, .default_index = 0}};
inline constexpr GameMeta kMetaUnlabelled{
    .slug = "x", .title = "X", .description = "d", .tag = "t",
    .icon = "", .options = kUnlabelled};
static_assert(!options_are_well_formed(kMetaUnlabelled),
              "an option with no label must be rejected: it renders as a row "
              "of choices with nothing saying what they choose");

// ── Negative: a row with nothing to pick ────────────────────────────────────
// ⚠ `.choices = {}`, not a zero-size array — `std::string_view kNone[]{}` is
// ill-formed in C++ (a zero-length array is a GNU extension -Werror rejects).
// A default-constructed span is the real way a game says "nothing to pick",
// and therefore the exact thing the predicate has to catch.
inline constexpr OptionSpec kNoChoices[]{
    {.label = "Level", .choices = {}, .default_index = 0}};
inline constexpr GameMeta kMetaNoChoices{
    .slug = "x", .title = "X", .description = "d", .tag = "t",
    .icon = "", .options = kNoChoices};
static_assert(!options_are_well_formed(kMetaNoChoices),
              "an option with no choices must be rejected: the cursor can land "
              "on it and there is nothing to move to");

// ── Negative: a blank choice ────────────────────────────────────────────────
inline constexpr OptionSpec kBlank[]{
    {.label = "Level", .choices = kBlankChoice, .default_index = 0}};
inline constexpr GameMeta kMetaBlank{
    .slug = "x", .title = "X", .description = "d", .tag = "t",
    .icon = "", .options = kBlank};
static_assert(!options_are_well_formed(kMetaBlank),
              "a blank choice must be rejected: it draws as an empty cycler "
              "the player cannot tell from a broken frame");

// ── Negative: default_index past the end ────────────────────────────────────
// ⚠ THE ONE THAT IS AN OUT-OF-RANGE READ rather than a bad-looking row, and it
// happens on the first frame, before the player has touched anything.
inline constexpr OptionSpec kDefaultTooHigh[]{
    {.label = "Level", .choices = kTwo, .default_index = 2}};  // size() is 2
inline constexpr GameMeta kMetaDefaultTooHigh{
    .slug = "x", .title = "X", .description = "d", .tag = "t",
    .icon = "", .options = kDefaultTooHigh};
static_assert(!options_are_well_formed(kMetaDefaultTooHigh),
              "a default_index at choices.size() must be rejected — off by one "
              "is the way this gets written, and it reads past the end on the "
              "game's first frame");

inline constexpr OptionSpec kDefaultNegative[]{
    {.label = "Level", .choices = kTwo, .default_index = -1}};
inline constexpr GameMeta kMetaDefaultNegative{
    .slug = "x", .title = "X", .description = "d", .tag = "t",
    .icon = "", .options = kDefaultNegative};
static_assert(!options_are_well_formed(kMetaDefaultNegative),
              "a negative default_index must be rejected");

// ⚠ The boundary the two above straddle, and it needs its own assertion in the
// OTHER direction. kGood's default_index is 1, the LAST valid index into a
// two-element array — so tightening the comparison to `>= size() - 1` (or to
// `> 0`) rejects a legal schema, and the two negatives above would not notice.
static_assert(kGoodOptions[0].default_index ==
                  static_cast<int>(kGoodOptions[0].choices.size()) - 1,
              "kGood must sit exactly ON the boundary or it does not defend it");
static_assert(options_are_well_formed(kGood),
              "the last valid default_index must be ACCEPTED");

// ── Negative: too many options ──────────────────────────────────────────────
inline constexpr OptionSpec kFive[]{
    {.label = "a", .choices = kTwo, .default_index = 0},
    {.label = "b", .choices = kTwo, .default_index = 0},
    {.label = "c", .choices = kTwo, .default_index = 0},
    {.label = "d", .choices = kTwo, .default_index = 0},
    {.label = "e", .choices = kTwo, .default_index = 0},
};
static_assert(std::size(kFive) == kMaxGameOptions + 1, "keep this one past");
inline constexpr GameMeta kMetaFive{
    .slug = "x", .title = "X", .description = "d", .tag = "t",
    .icon = "", .options = kFive};
static_assert(!options_are_well_formed(kMetaFive),
              "more than kMaxGameOptions must be rejected: OptionsScreen's "
              "m_choice is a fixed array of exactly that size");

// ── Negative: a list option sharing a screen ────────────────────────────────
inline constexpr OptionSpec kListPlusOne[]{
    {.label = "Level", .choices = kSeven, .default_index = 0},
    {.label = "Walls", .choices = kTwo, .default_index = 0},
};
inline constexpr GameMeta kMetaListPlusOne{
    .slug = "x", .title = "X", .description = "d", .tag = "t",
    .icon = "", .options = kListPlusOne};
static_assert(!options_are_well_formed(kMetaListPlusOne),
              "a >kInlineChoiceMax option must not share a screen: it renders "
              "as a windowed list that consumes every row");

// ...but alone it is exactly what Sokoban needs, so it must PASS.
inline constexpr OptionSpec kListAlone[]{
    {.label = "Level", .choices = kSeven, .default_index = 0}};
inline constexpr GameMeta kMetaListAlone{
    .slug = "x", .title = "X", .description = "d", .tag = "t",
    .icon = "", .options = kListAlone};
static_assert(options_are_well_formed(kMetaListAlone),
              "a lone list option is Sokoban's level picker and must pass — "
              "rejecting it would make the rule 'no long lists' instead of "
              "'a long list cannot share'");

// ── Negatives: non-ASCII option text ────────────────────────────────────────
// ⚠ The loop these two witness is the single easiest thing in this change to
// delete without consequence. No registered game has non-ASCII option text, so
// nothing that RUNS can tell the difference. See the note at meta_text_is_ascii.
inline constexpr OptionSpec kEmDashLabel[]{
    {.label = "Level \xe2\x80\x94 pick one", .choices = kTwo,
     .default_index = 0}};
inline constexpr GameMeta kMetaEmDashLabel{
    .slug = "x", .title = "X", .description = "d", .tag = "t",
    .icon = "", .options = kEmDashLabel};
static_assert(!meta_text_is_ascii(kMetaEmDashLabel),
              "an em dash in an option LABEL must be rejected — it is printed "
              "twice, once by the options screen and once by the selector's "
              "detail pane, on a tier that cannot render it");

inline constexpr OptionSpec kEmDashInChoice[]{
    {.label = "Level", .choices = kEmDashChoice, .default_index = 0}};
inline constexpr GameMeta kMetaEmDashChoice{
    .slug = "x", .title = "X", .description = "d", .tag = "t",
    .icon = "", .options = kEmDashInChoice};
static_assert(!meta_text_is_ascii(kMetaEmDashChoice),
              "an em dash in an option CHOICE must be rejected: the choices "
              "are swept as well as the labels, and only this asserts it");

// The two predicates are independent, and each negative must fail for its OWN
// reason. A schema can be well formed and non-ASCII, or malformed and ASCII;
// conflating them would let one predicate mask a hole in the other.
static_assert(options_are_well_formed(kMetaEmDashLabel),
              "the em-dash metas are structurally FINE — if this fails, an "
              "ASCII negative is being rejected by the wrong predicate");
static_assert(meta_text_is_ascii(kMetaDefaultTooHigh),
              "the malformed metas are ASCII-clean — same reason, other way");

}  // namespace

// The registry's real schemas, checked at run time as well as by the
// static_assert in all_games.cpp — so a reader who wants to know what the
// predicate accepts can see it pass on the actual games.
TEST_CASE("every registered game's options schema is well formed") {
  // Deliberately duplicated from all_games.cpp's static_assert rather than
  // shared: this file is about the predicate, and a shared helper would let
  // both sites be wrong together.
  for (const auto& entry : termgame::all_games()) {
    INFO("game: " << entry.meta.slug);
    REQUIRE(options_are_well_formed(entry.meta));
    REQUIRE(meta_text_is_ascii(entry.meta));
  }
}

TEST_CASE("a game with no options is the cheap case") {
  // 2048 declares nothing and must stay that way — the empty span is what the
  // detail pane short-circuits on and what lets the game hold no member at all.
  bool found_empty = false;
  for (const auto& entry : termgame::all_games()) {
    if (entry.meta.slug == "2048") {
      REQUIRE(entry.meta.options.empty());
      found_empty = true;
    }
  }
  REQUIRE(found_empty);
}
