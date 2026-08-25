// Solitaire's layout contract: the nineteen-card worst case, the 43x24
// bottom-tier floor and the one coordinate mapping the renderer and mouse path
// share. No Screen and no TTY.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <glyphcade/games/solitaire/layout.hpp>

namespace {

using namespace glyphcade::solitaire;

}  // namespace

TEST_CASE("the maximum tableau depth is derived, not typed",
          "[solitaire][layout]") {
  CHECK(kMaxHiddenCards == kTableauPiles - 1);
  CHECK(kMaxFaceUpCards == kRanks);
  CHECK(kMaxTableauCards == kMaxHiddenCards + kMaxFaceUpCards);
  CHECK(kMaxTableauCards == 19);
}

TEST_CASE("the bottom-tier card and seven piles set the width",
          "[solitaire][layout]") {
  // +---+ / |10H| / +---+: three interior columns are the smallest shape that
  // can show the widest rank and an ASCII suit without colour.
  CHECK(kCardCols - 2 == 3);
  CHECK(kCardRows == 3);
  CHECK(kTableauCols == (7 * 5) + 6);
  CHECK(kStockWasteCols == 11);
  CHECK(kFoundationCols == 23);
  CHECK(kTopGroupGapCols == 7);
  CHECK(kTopCols == kTableauCols);
  CHECK(kNeedCols == 43);
}

TEST_CASE("a counted hidden prefix bounds the worst pile at sixteen rows",
          "[solitaire][layout]") {
  CHECK(pile_rows(0, 0) == kCardRows);  // empty destination outline
  CHECK(pile_rows(6, 0) == kCardRows);  // exposed counted card back
  CHECK(pile_rows(0, 1) == kCardRows);
  CHECK(pile_rows(6, 1) == kHiddenSummaryRows + kCardRows);
  CHECK(pile_rows(0, 13) == 12 + kCardRows);
  CHECK(pile_rows(6, 13) == 1 + 12 + kCardRows);
  CHECK(kMaxTableauRows == 16);

  int observed_max = 0;
  for (int hidden = 0; hidden <= kMaxHiddenCards; ++hidden) {
    for (int face_up = 0; face_up <= kMaxFaceUpCards; ++face_up) {
      observed_max = std::max(observed_max, pile_rows(hidden, face_up));
    }
  }
  CHECK(observed_max == kMaxTableauRows);
}

TEST_CASE("43x24 is the exact floor and presentation has a width ceiling",
          "[solitaire][layout]") {
  const Layout exact = compute_layout(kNeedCols, kNeedRows);
  REQUIRE(exact.fits);
  CHECK(exact.frame_x == 0);
  CHECK(exact.frame_y == 0);
  CHECK(exact.frame_w == 43);
  CHECK(exact.frame_h == 24);
  CHECK(exact.card_cols == 5);
  CHECK(exact.card_rows == 3);
  CHECK(exact.pile_gap_cols == 1);
  CHECK(exact.tableau_rows == kMaxTableauRows);
  CHECK(exact.tableau_y + exact.tableau_rows == exact.status_y);
  CHECK(exact.status_y + 1 == exact.hint_y);
  CHECK(exact.hint_y + 1 == exact.frame_h - 1);

  CHECK_FALSE(compute_layout(kNeedCols - 1, kNeedRows).fits);
  CHECK_FALSE(compute_layout(kNeedCols, kNeedRows - 1).fits);

  // Width alone does not crush portrait cards into three terminal rows.
  const Layout short_wide = compute_layout(120, 24);
  REQUIRE(short_wide.fits);
  CHECK(short_wide.card_cols == kCardCols);
  CHECK(short_wide.card_rows == kCardRows);

  const Layout roomy = compute_layout(80, 30);
  REQUIRE(roomy.fits);
  CHECK(roomy.card_cols == 8);
  CHECK(roomy.card_rows == 6);
  CHECK(std::abs((3 * roomy.card_cols) - (4 * roomy.card_rows)) <= 1);
  CHECK(roomy.pile_gap_cols == 2);
  CHECK(roomy.frame_x == 5);
  CHECK(roomy.frame_w == 70);
  CHECK(roomy.frame_h == 30);
  CHECK(roomy.tableau_rows == 19);
  CHECK(roomy.tableau_rows > exact.tableau_rows);

  const Layout wide = compute_layout(120, 32);
  REQUIRE(wide.fits);
  CHECK(wide.card_cols == 9);
  CHECK(wide.card_rows == 7);
  CHECK(std::abs((3 * wide.card_cols) - (4 * wide.card_rows)) <= 1);
  CHECK(wide.pile_gap_cols == kMaxPileGapCols);
  CHECK(wide.frame_w == 77);
  CHECK(wide.frame_x == (120 - wide.frame_w) / 2);

  const Layout wider = compute_layout(200, 40);
  CHECK(wider.card_cols == kMaxCardCols);
  CHECK(wider.card_rows == kMaxCardRows);
  CHECK(std::abs((3 * wider.card_cols) - (4 * wider.card_rows)) <= 1);
  CHECK(wider.frame_w == kMaxNeedCols);
  CHECK(wider.frame_x == (200 - kMaxNeedCols) / 2);
}

TEST_CASE("pile origins share the same gap arithmetic", "[solitaire][layout]") {
  const Layout l = compute_layout(80, 24);
  REQUIRE(l.fits);

  CHECK(l.tableau_pile_x(0) == l.tableau_x);
  CHECK(l.tableau_pile_x(6) + l.card_cols == l.tableau_x + l.tableau_cols);
  CHECK(l.top_pile_x(0) == l.top_x);
  const int stock_waste_cols = (2 * l.card_cols) + l.pile_gap_cols;
  const int foundation_cols = (4 * l.card_cols) + (3 * l.pile_gap_cols);
  const int group_gap = l.tableau_cols - stock_waste_cols - foundation_cols;
  CHECK(l.top_pile_x(1) + l.card_cols == l.top_x + stock_waste_cols);
  CHECK(l.top_pile_x(2) == l.top_x + stock_waste_cols + group_gap);
  CHECK(l.top_pile_x(5) + l.card_cols == l.top_x + l.tableau_cols);
  CHECK(l.top_x == l.tableau_x);
}

TEST_CASE("an exposed hidden card is the only hidden card hit",
          "[solitaire][layout][hit]") {
  for (int row = 0; row < kCardRows; ++row) {
    CHECK(tableau_card_at(row, 6, 0) == 5);
  }
  CHECK_FALSE(tableau_card_at(-1, 6, 0));
  CHECK_FALSE(tableau_card_at(kCardRows, 6, 0));
  CHECK_FALSE(tableau_card_at(0, 0, 0));
}

TEST_CASE("the covered hidden summary is visible but never selectable",
          "[solitaire][layout][hit]") {
  // Row zero says how many hidden cards exist. Card six, the first face-up
  // card, starts on row one and remains individually addressable.
  CHECK_FALSE(tableau_card_at(0, 6, 3));
  CHECK(tableau_card_at(1, 6, 3) == 6);
  CHECK(tableau_card_at(2, 6, 3) == 7);

  // The final face-up card owns its full three-row rectangle.
  CHECK(tableau_card_at(3, 6, 3) == 8);
  CHECK(tableau_card_at(4, 6, 3) == 8);
  CHECK(tableau_card_at(5, 6, 3) == 8);
  CHECK_FALSE(tableau_card_at(6, 6, 3));
}

TEST_CASE("every visible face-up strip maps to its card and run",
          "[solitaire][layout][hit]") {
  for (int hidden = 0; hidden <= kMaxHiddenCards; ++hidden) {
    for (int face_up = 1; face_up <= kMaxFaceUpCards; ++face_up) {
      const int hidden_rows = hidden > 0 ? kHiddenSummaryRows : 0;
      const int rows = pile_rows(hidden, face_up);

      if (hidden > 0) CHECK_FALSE(tableau_card_at(0, hidden, face_up));

      for (int face = 0; face < face_up; ++face) {
        const int row = hidden_rows + face;
        INFO("hidden " << hidden << ", face-up " << face_up << ", row " << row);
        CHECK(tableau_card_at(row, hidden, face_up) == hidden + face);
      }

      // The bottom two rows belong to the same final card, not to imaginary
      // cards beyond the run.
      CHECK(tableau_card_at(rows - 2, hidden, face_up) == hidden + face_up - 1);
      CHECK(tableau_card_at(rows - 1, hidden, face_up) == hidden + face_up - 1);
      CHECK_FALSE(tableau_card_at(rows, hidden, face_up));
    }
  }
}

TEST_CASE("invalid counts never produce a hit", "[solitaire][layout][hit]") {
  CHECK_FALSE(tableau_card_at(0, -1, 1));
  CHECK_FALSE(tableau_card_at(0, 1, -1));
}
