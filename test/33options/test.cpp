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
// ⚠ THIS FILE DOES CONSTRUCT A Screen, unlike test/14, /22, /25, /27 and /31.
// It is not breaking their rule; it is on the other side of it. Those suites
// test game RULES, which name no termforge type, so a Screen there would be an
// accident. hud and OptionsScreen are rendering code — a Screen is the thing
// under test, and termforge::Screen is directly constructible, so none of this
// needs a Shell, a Probe or test_run_frames.
//
// What belongs here: hud's arithmetic, and OptionsScreen in isolation.
// What does not: any particular game's settings. Whether Snake starts on the
// level you picked is test/26snake-ui's question, because it needs a Snake.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>
#include <termforge/widgets/theme.hpp>

#include <termgame/arcade/game_meta.hpp>
#include <termgame/arcade/hud.hpp>
#include <termgame/arcade/options_screen.hpp>
#include <termgame/arcade/registry.hpp>

namespace {

namespace hud = termgame::hud;

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

// ═══════════════════════════════════════════════════════════════════════════
// hud::pick_for_width
// ═══════════════════════════════════════════════════════════════════════════

namespace {

constexpr hud::Tier kProbeTiers[]{
    {70, "wide"},
    {40, "medium"},
    {0, "narrow"},
};
static_assert(hud::tiers_are_total(kProbeTiers), "");

// The tier-table shape rule is itself a guard nothing else witnesses, so it
// gets its own negatives. Both of these are how a real table gets written
// wrong: appended in the order someone thought of them, or missing the floor.
constexpr hud::Tier kMisordered[]{{0, "narrow"}, {40, "medium"}};
constexpr hud::Tier kNoFloor[]{{70, "wide"}, {40, "medium"}};
static_assert(!hud::tiers_are_total(kMisordered),
              "narrowest-first must be rejected: pick_for_width returns the "
              "FIRST match, so the floor would swallow every wider tier");
static_assert(!hud::tiers_are_total(kNoFloor),
              "a table with no zero floor must be rejected: below 40 columns "
              "pick_for_width returns empty and the row silently vanishes");

}  // namespace

TEST_CASE("pick_for_width returns the first tier that fits") {
  CHECK(hud::pick_for_width(200, kProbeTiers) == "wide");
  CHECK(hud::pick_for_width(70, kProbeTiers) == "wide");
  // ⚠ Both sides of each boundary. `>=` mutated to `>` moves every tier by one
  // column, which no interior width can see.
  CHECK(hud::pick_for_width(69, kProbeTiers) == "medium");
  CHECK(hud::pick_for_width(40, kProbeTiers) == "medium");
  CHECK(hud::pick_for_width(39, kProbeTiers) == "narrow");
  CHECK(hud::pick_for_width(0, kProbeTiers) == "narrow");
}

TEST_CASE("pick_for_width never returns empty for a total table") {
  // The property tiers_are_total() promises, swept rather than argued.
  for (int cols = 0; cols <= 120; ++cols) {
    INFO("cols: " << cols);
    REQUIRE_FALSE(hud::pick_for_width(cols, kProbeTiers).empty());
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// hud::draw_status_row — the mutation that has gone green in two epics
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Read row `y` back as a plain string, trailing blanks trimmed.
auto row_text(const termforge::Screen& screen, int y) -> std::string {
  std::string out;
  for (int x = 0; x < screen.cols(); ++x) {
    const std::string& t = screen.at(x, y).text;
    out += t.empty() ? " " : t;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

}  // namespace

TEST_CASE("draw_status_row: the word survives every width") {
  // ⚠ THE SWEEP THAT KILLS IT, and the reason it goes down to 1 column rather
  // than to any game's kNeedCols. Deleting the budget changes nothing at any
  // width the GAME fits on — the loop stops appending long before the left text
  // can reach the word. The status row is drawn whether or not the playfield
  // fits, so the interesting widths are the ones no game can be played at.
  //
  // Until this existed, killing this mutation would have meant writing this
  // sweep four times, once per game. That is why it lives against the helper.
  const std::vector<std::string> fields{"score 1234", "lines 56", "level 7",
                                        "record 890123"};
  const std::string_view word = "TOPPED OUT";

  for (int cols = 1; cols <= 100; ++cols) {
    INFO("cols: " << cols);
    termforge::Screen screen(cols, 3);
    hud::draw_status_row(screen, 0, fields, word, termforge::theme::kFg,
                         termforge::theme::kFg, termforge::theme::kBg);
    const std::string row = row_text(screen, 0);

    // 1. The row never exceeds the screen. write_text clips, so this is really
    //    asserting that nothing wrapped onto row 1.
    REQUIRE(static_cast<int>(row.size()) <= cols);
    REQUIRE(row_text(screen, 1).empty());

    // 2. The word is intact and right-aligned, whenever it fits at all.
    if (cols >= static_cast<int>(word.size())) {
      INFO("row: [" << row << "]");
      REQUIRE(row.find(word) != std::string::npos);
      REQUIRE(row.rfind(word) ==
              static_cast<std::size_t>(cols) - word.size());
    }

    // 3. ⚠ THE ACTUAL ASSERTION. Every field is present WHOLE or absent
    //    ENTIRELY — never a prefix. A half-written "score 12" is a wrong score,
    //    and that is the failure the budget exists to prevent. Checking only
    //    "the fields fit" would pass against the mutant, because at these
    //    widths the mutant writes over the word rather than off the edge.
    for (const std::string& f : fields) {
      const std::size_t at = row.find(f);
      if (at == std::string::npos) {
        // Absent is fine — but then its own PREFIX must not be there either,
        // which is what distinguishes "dropped" from "truncated".
        const std::string prefix = f.substr(0, f.size() - 1);
        INFO("field: [" << f << "] row: [" << row << "]");
        REQUIRE(row.find(prefix) == std::string::npos);
      } else {
        // Present — and it must not have landed on top of the word.
        REQUIRE(at + f.size() <= static_cast<std::size_t>(cols));
      }
    }
  }
}

TEST_CASE("draw_status_row: fields are dropped in priority order") {
  const std::vector<std::string> fields{"aaa", "bbb", "ccc"};
  // 20 columns, a 4-char word: word_x is 16, budget 14. "aaa   bbb   ccc" is
  // 15, one over — so the last field goes and nothing else does.
  termforge::Screen screen(20, 2);
  hud::draw_status_row(screen, 0, fields, "WORD", termforge::theme::kFg,
                       termforge::theme::kFg, termforge::theme::kBg);
  const std::string row = row_text(screen, 0);
  INFO("row: [" << row << "]");
  CHECK(row.find("aaa") != std::string::npos);
  CHECK(row.find("bbb") != std::string::npos);
  // ⚠ The LAST field is the one that goes, not the one that happens to be
  // longest. A loop that skipped a field and kept trying later ones would
  // reorder the row as the terminal narrowed.
  CHECK(row.find("ccc") == std::string::npos);
}

TEST_CASE("draw_status_row: a screen narrower than the word keeps the word") {
  // budget goes negative here, and that is correct rather than a bug to guard:
  // nothing fits, so the word — the only carrier of win and loss at the
  // no-colour tier — keeps the row to itself.
  const std::vector<std::string> fields{"score 1"};
  termforge::Screen screen(4, 2);
  hud::draw_status_row(screen, 0, fields, "PLAYING", termforge::theme::kFg,
                       termforge::theme::kFg, termforge::theme::kBg);
  const std::string row = row_text(screen, 0);
  CHECK(row.find("score") == std::string::npos);
  CHECK_FALSE(row.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// OptionsScreen
// ═══════════════════════════════════════════════════════════════════════════

namespace {

using termgame::OptionsScreen;
using Reply = OptionsScreen::Reply;

inline constexpr std::string_view kLevels[]{"Easy", "Normal", "Hard"};
inline constexpr std::string_view kWalls[]{"Solid", "Wrap"};
inline constexpr OptionSpec kTwoOptions[]{
    {.label = "Level", .choices = kLevels, .default_index = 1},
    {.label = "Walls", .choices = kWalls, .default_index = 0},
};

// Twenty choices: list mode, the Sokoban shape.
inline constexpr std::string_view kTwenty[]{
    "01 Hello",  "02 Corner",  "03 Alley",   "04 Pen",    "05 Split",
    "06 Ring",   "07 Shuttle", "08 Comb",    "09 Vault",  "10 Spiral",
    "11 Twin",   "12 Ladder",  "13 Cross",   "14 Maze",   "15 Knot",
    "16 Cradle", "17 Fork",    "18 Cascade", "19 Lock",   "20 Archive",
};
inline constexpr OptionSpec kListOption[]{
    {.label = "Level", .choices = kTwenty, .default_index = 0}};

auto key(termforge::Key k) -> termforge::Event {
  return termforge::KeyEvent{.key = k};
}
auto ch(char32_t c) -> termforge::Event {
  return termforge::KeyEvent{.key = termforge::Key::Char, .ch = c};
}

// ⚠ on_event is [[nodiscard]] on purpose — a caller that drops the Reply
// silently stops forwarding Escape to the Shell. Cases that are only setting up
// state say so explicitly here rather than the attribute being weakened.
auto send(OptionsScreen& s, const termforge::Event& ev) -> void {
  static_cast<void>(s.on_event(ev));
}

auto opened(std::span<const OptionSpec> opts) -> OptionsScreen {
  OptionsScreen s;
  s.open("Snake", opts, nullptr);
  return s;
}

// The whole screen as one string, for "does this text appear anywhere" checks.
auto screen_text(const termforge::Screen& screen) -> std::string {
  std::string out;
  for (int y = 0; y < screen.rows(); ++y) {
    out += row_text(screen, y);
    out += "\n";
  }
  return out;
}

auto all_seven_bit(const termforge::Screen& screen) -> bool {
  for (int y = 0; y < screen.rows(); ++y) {
    for (int x = 0; x < screen.cols(); ++x) {
      for (const unsigned char b : screen.at(x, y).text) {
        if (b >= 0x80) return false;
      }
    }
  }
  return true;
}

}  // namespace

TEST_CASE("an empty schema opens nothing") {
  // ⚠ 2048's case. open() must be safe to call unconditionally, and must leave
  // the screen closed — otherwise the cheap case is not cheap, it is a blank
  // screen the player has to dismiss.
  OptionsScreen s;
  s.open("2048", {}, nullptr);
  CHECK_FALSE(s.is_open());
  CHECK(s.on_event(key(termforge::Key::Enter)) == Reply::Ignored);
}

TEST_CASE("open() seeds each choice from its own default_index") {
  auto s = opened(kTwoOptions);
  REQUIRE(s.is_open());
  // ⚠ Not both zero. A mutant that memsets the array passes if every default
  // is 0, which is why the fixture's first default is 1.
  CHECK(s.selected(0) == 1);
  CHECK(s.selected(1) == 0);
  CHECK(s.cursor() == 0);
}

TEST_CASE("Enter dismisses and the choices survive the close") {
  auto s = opened(kTwoOptions);
  CHECK(s.on_event(key(termforge::Key::Right)) == Reply::Consumed);
  CHECK(s.on_event(key(termforge::Key::Enter)) == Reply::Dismissed);
  CHECK_FALSE(s.is_open());
  // ⚠ The game reads selected() DURING dismissal, so the values must outlive
  // the close. Clearing them on close would be invisible until a game read 0.
  CHECK(s.selected(0) == 2);
}

TEST_CASE("dismissing without touching anything yields the defaults") {
  // ⚠ Paired with the case above on purpose. A game that ignores selected()
  // and always applies default_index passes THIS alone; a game that applies
  // selected() correctly passes both. Neither case is sufficient by itself.
  auto s = opened(kTwoOptions);
  CHECK(s.on_event(key(termforge::Key::Enter)) == Reply::Dismissed);
  CHECK(s.selected(0) == 1);
  CHECK(s.selected(1) == 0);
}

TEST_CASE("values clamp rather than wrap") {
  auto s = opened(kTwoOptions);
  for (int i = 0; i < 10; ++i) send(s, key(termforge::Key::Right));
  CHECK(s.selected(0) == 2);  // Hard, not back round to Easy
  for (int i = 0; i < 10; ++i) send(s, key(termforge::Key::Left));
  CHECK(s.selected(0) == 0);
}

TEST_CASE("the cursor moves between options and clamps at both ends") {
  auto s = opened(kTwoOptions);
  CHECK(s.cursor() == 0);
  send(s, key(termforge::Key::Down));
  CHECK(s.cursor() == 1);
  send(s, key(termforge::Key::Down));
  CHECK(s.cursor() == 1);  // clamped
  // Left/Right now act on the SECOND option, not the first.
  send(s, key(termforge::Key::Right));
  CHECK(s.selected(1) == 1);
  CHECK(s.selected(0) == 1);  // untouched
  send(s, key(termforge::Key::Up));
  CHECK(s.cursor() == 0);
}

TEST_CASE("hjkl mirror the arrows") {
  auto s = opened(kTwoOptions);
  send(s, ch(U'j'));
  CHECK(s.cursor() == 1);
  send(s, ch(U'l'));
  CHECK(s.selected(1) == 1);
  send(s, ch(U'h'));
  CHECK(s.selected(1) == 0);
  send(s, ch(U'k'));
  CHECK(s.cursor() == 0);
  CHECK(s.on_event(ch(U' ')) == Reply::Dismissed);
}

TEST_CASE("Escape and p are NOT consumed") {
  // ⚠ Both belong to the Shell. Consuming Escape would strand the player on a
  // screen whose only exit is starting a game they did not want; consuming 'p'
  // would make pause dead here and nowhere else. Ignored is what makes the
  // caller return false, which is what lets the Shell see them.
  auto s = opened(kTwoOptions);
  CHECK(s.on_event(key(termforge::Key::Escape)) == Reply::Ignored);
  CHECK(s.is_open());  // and it did not dismiss on the way past
  CHECK(s.on_event(ch(U'p')) == Reply::Ignored);
  CHECK(s.is_open());
}

TEST_CASE("a key Release is ignored") {
  // ⚠ THE TETRIS/KITTY TRAP, and this synthesised event is its ONLY coverage.
  // Tetris declares KeyboardMode::Enhanced; the Shell sets the tier inside
  // enter_selected_game, BEFORE the Enter that entered the game comes back up.
  // On a terminal that granted the kitty protocol, the release of that very
  // keystroke arrives here — and without the guard it dismisses this screen
  // before one frame is drawn. The player sees the options flash and vanish.
  //
  // ⚠ Nothing else in this repo can reach it. test_run_frames installs a
  // FallbackDriver with all-false capabilities, and script(1) is not a kitty
  // terminal, so kitty_keyboard is never true in this container. A pty run
  // cannot substitute for this case; do not delete it as redundant.
  auto s = opened(kTwoOptions);
  const termforge::Event release = termforge::KeyEvent{
      .key = termforge::Key::Enter, .action = termforge::KeyAction::Release};
  CHECK(s.on_event(release) == Reply::Ignored);
  CHECK(s.is_open());

  // Repeat, by contrast, IS acted on — holding Down must keep moving.
  auto s2 = opened(kTwoOptions);
  const termforge::Event repeat = termforge::KeyEvent{
      .key = termforge::Key::Down, .action = termforge::KeyAction::Repeat};
  CHECK(s2.on_event(repeat) == Reply::Consumed);
  CHECK(s2.cursor() == 1);
}

TEST_CASE("preselect overrides the schema default and clamps") {
  auto s = opened(kListOption);
  s.preselect(0, 7);
  CHECK(s.selected(0) == 7);
  // ⚠ Clamps rather than rejects: Sokoban's resume index comes from a score
  // file that may have been written when the pack was a different size.
  s.preselect(0, 999);
  CHECK(s.selected(0) == 19);
  s.preselect(0, -5);
  CHECK(s.selected(0) == 0);
  // An out-of-range OPTION is a no-op, not a write past the array.
  s.preselect(9, 3);
  CHECK(s.selected(0) == 0);
}

TEST_CASE("list mode: Up/Down move the choice, because the rows ARE choices") {
  // ⚠ In cycler mode Up/Down move between options; here there is only one
  // option and its choices are the rows, so Up/Down must move the VALUE or the
  // twenty-level picker cannot pick anything.
  auto s = opened(kListOption);
  CHECK(s.selected(0) == 0);
  send(s, key(termforge::Key::Down));
  CHECK(s.selected(0) == 1);
  send(s, key(termforge::Key::Down));
  CHECK(s.selected(0) == 2);
  send(s, key(termforge::Key::Up));
  CHECK(s.selected(0) == 1);
  CHECK(s.cursor() == 0);  // there is only one row to be on
}

TEST_CASE("list mode: the cursor stays inside the window while scrolling") {
  auto s = opened(kListOption);
  termforge::Screen screen(40, 12);
  for (int i = 0; i < 19; ++i) send(s, key(termforge::Key::Down));
  REQUIRE(s.selected(0) == 19);
  s.draw(screen);
  // The last level must be visible, not scrolled past.
  INFO(screen_text(screen));
  CHECK(screen_text(screen).find("20 Archive") != std::string::npos);
  // And the first must NOT be, or nothing scrolled at all.
  CHECK(screen_text(screen).find("01 Hello") == std::string::npos);
}

TEST_CASE("draw: the title, the options and the hint all appear") {
  auto s = opened(kTwoOptions);
  termforge::Screen screen(80, 12);
  s.draw(screen);
  const std::string text = screen_text(screen);
  INFO(text);
  CHECK(text.find("Snake") != std::string::npos);
  CHECK(text.find("Level") != std::string::npos);
  CHECK(text.find("Normal") != std::string::npos);  // the default, not "Easy"
  CHECK(text.find("Walls") != std::string::npos);
  CHECK(text.find("Enter start") != std::string::npos);
}

TEST_CASE("draw: a closed screen draws nothing") {
  auto s = opened(kTwoOptions);
  send(s, key(termforge::Key::Enter));
  termforge::Screen screen(80, 12);
  s.draw(screen);
  CHECK(screen_text(screen).find("Level") == std::string::npos);
}

TEST_CASE("draw survives every size from the Shell floor up") {
  // ⚠ 20x8 is Shell::kMinCols x kMinRows — the smallest screen Game::draw is
  // ever called with, and SMALLER than every game's own playfield floor. A
  // screen that only survives the game's minimum has not been tested.
  //
  // The sweep goes below the floor as well, because a resize race can hand a
  // game a smaller Screen than the Shell's gate nominally allows.
  for (int cols = 4; cols <= 90; cols += 3) {
    for (int rows = 2; rows <= 26; rows += 2) {
      INFO("size: " << cols << "x" << rows);
      auto s = opened(kTwoOptions);
      termforge::Screen screen(cols, rows);
      REQUIRE_NOTHROW(s.draw(screen));
      REQUIRE(all_seven_bit(screen));  // no ctx => Ascii tier

      auto list = opened(kListOption);
      termforge::Screen screen2(cols, rows);
      REQUIRE_NOTHROW(list.draw(screen2));
      REQUIRE(all_seven_bit(screen2));
    }
  }
}

TEST_CASE("draw: the hint row is the last row and always says how to start") {
  // ⚠ The one thing that must survive any width, because it is the only text
  // telling the player how to leave. At 20 columns the wide tiers do not fit;
  // "Enter start" is 11 and does.
  for (int cols : {20, 24, 34, 40, 56, 80}) {
    INFO("cols: " << cols);
    auto s = opened(kTwoOptions);
    termforge::Screen screen(cols, 8);
    s.draw(screen);
    const std::string last = row_text(screen, 7);
    INFO("hint row: [" << last << "]");
    REQUIRE(last.find("Enter start") != std::string::npos);
    REQUIRE(static_cast<int>(last.size()) <= cols);
  }
}

TEST_CASE("draw: options never overwrite the hint row") {
  // A short screen must drop OPTIONS, not the hint. Two options plus a
  // two-row header needs five rows; at four the second option has nowhere to
  // go and the hint must still be there.
  auto s = opened(kTwoOptions);
  termforge::Screen screen(60, 4);
  s.draw(screen);
  const std::string last = row_text(screen, 3);
  INFO(screen_text(screen));
  CHECK(last.find("Enter start") != std::string::npos);
  CHECK(last.find("Walls") == std::string::npos);
}

TEST_CASE("draw_status_row: a field that EXACTLY fills the budget is kept") {
  // ⚠ Found by mutation, not by reasoning: `> budget` changed to `>= budget`
  // survived every case above. It costs exactly one column — a field that fits
  // with nothing to spare gets dropped — and no width in the sweep lands on
  // that boundary by chance, because the sweep varies the SCREEN while the
  // fields stay the same length.
  //
  // Constructed so the arithmetic is exact: word "WORD" is 4, so at 20 columns
  // word_x is 16 and budget is 14. The single field is 14.
  const std::vector<std::string> fields{"abcdefghijklmn"};
  REQUIRE(fields[0].size() == 14);
  termforge::Screen screen(20, 2);
  hud::draw_status_row(screen, 0, fields, "WORD", termforge::theme::kFg,
                       termforge::theme::kFg, termforge::theme::kBg);
  const std::string row = row_text(screen, 0);
  INFO("row: [" << row << "]");
  CHECK(row.find(fields[0]) != std::string::npos);

  // And one column longer must be dropped, or the assertion above is just
  // "fields are kept" and pins nothing.
  const std::vector<std::string> tooLong{"abcdefghijklmno"};
  termforge::Screen screen2(20, 2);
  hud::draw_status_row(screen2, 0, tooLong, "WORD", termforge::theme::kFg,
                       termforge::theme::kFg, termforge::theme::kBg);
  CHECK(row_text(screen2, 0).find(tooLong[0]) == std::string::npos);
}

TEST_CASE("draw_status_row: a too-narrow screen keeps the START of the word") {
  // ⚠ Also found by mutation: dropping the max(0, ...) clamp on word_x survived
  // everything, because a negative x is merely "clipped" by write_text rather
  // than rejected — so the word still appeared, just with its FRONT eaten. At 4
  // columns "PLAYING" became "YING", which reads as a different word rather
  // than as a truncated one.
  //
  // The clamp is what makes the visible text a PREFIX of the word. That is the
  // property worth having: "PLAY" is recognisably "PLAYING" cut short.
  const std::vector<std::string> none{};
  for (int cols = 1; cols <= 7; ++cols) {
    INFO("cols: " << cols);
    termforge::Screen screen(cols, 2);
    hud::draw_status_row(screen, 0, none, "PLAYING", termforge::theme::kFg,
                         termforge::theme::kFg, termforge::theme::kBg);
    const std::string row = row_text(screen, 0);
    INFO("row: [" << row << "]");
    REQUIRE_FALSE(row.empty());
    REQUIRE(std::string_view("PLAYING").substr(0, row.size()) == row);
  }
}

TEST_CASE("selected() and preselect() are total over any index") {
  // ⚠ Found by mutation: dropping selected()'s range guard survived everything,
  // because no case had ever ASKED for an option that does not exist. It is
  // reachable — m_choice is a fixed kMaxGameOptions array, so an unguarded
  // selected(9) reads past the end of a screen with two options, and a game
  // that loops `for (i = 0; i < 4; ++i)` over its own settings would do it.
  auto s = opened(kTwoOptions);
  REQUIRE(s.option_count() == 2);
  CHECK(s.selected(2) == 0);
  CHECK(s.selected(kMaxGameOptions) == 0);
  CHECK(s.selected(9999) == 0);
  // And on a screen that was never opened at all.
  OptionsScreen closed;
  CHECK(closed.selected(0) == 0);
  closed.preselect(0, 3);  // must not write into an empty span's array
  CHECK(closed.selected(0) == 0);
}
