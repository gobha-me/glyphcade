#pragma once

// glyphcade — Solitaire: the bottom-tier table geometry. Integers only.
//
// It names no termforge type and owns no rules state. The renderer and mouse
// path consume the same Layout and tableau_card_at() answer so their
// coordinates cannot drift apart.
//
// ── Why a nineteen-card pile still fits twenty-four rows ────────────────────
//
// The longest legal tableau has six face-down cards from the initial deal plus
// a complete thirteen-rank face-up build. Drawing every card at even one row of
// fan would need 21 rows for a three-row card, before the top row or chrome.
//
// Face-down identities are not information the player may inspect. Their COUNT
// is, so a non-empty hidden prefix is one counted card-back strip. Face-up
// cards remain one row apart and therefore individually readable and
// hit-testable; the last card is drawn at its full three-row height. The worst
// pile is then
//
//   1 hidden strip + 12 covered face-up rows + 3 final-card rows = 16.
//
// This is not a cap: all nineteen cards are still represented, and every
// face-up card keeps its own row. It needs no scrolling mode, adaptive option
// or hidden playable information.

#include <algorithm>
#include <optional>

namespace glyphcade::solitaire {

inline constexpr int kTableauPiles = 7;
inline constexpr int kRanks = 13;

// Pile seven begins with six hidden cards, the largest hidden prefix in the
// deal. A legal face-up tableau run is descending by rank and therefore cannot
// contain more than the deck's thirteen ranks.
inline constexpr int kMaxHiddenCards = kTableauPiles - 1;
inline constexpr int kMaxFaceUpCards = kRanks;
inline constexpr int kMaxTableauCards = kMaxHiddenCards + kMaxFaceUpCards;

static_assert(kMaxHiddenCards == 6);
static_assert(kMaxTableauCards == 19);

// Bottom-tier card. The three interior columns hold the widest label, "10H".
// Presentation may grow on a larger screen, but width and height grow together
// at the atlas' 2:3 aspect after accounting for cells that are nominally twice
// as tall as they are wide. A short-but-wide terminal therefore keeps compact
// cards instead of crushing portrait art into a three-row landscape slot.
// Width has a deliberate ceiling: seven 11-column cards with two-column gaps
// need 91 columns including the frame. Past that prose measure the table
// centres instead of stretching.
inline constexpr int kCardCols = 5;
inline constexpr int kCardRows = 3;
inline constexpr int kPileGapCols = 1;
inline constexpr int kMaxCardCols = 11;
inline constexpr int kMaxCardRows = 8;
inline constexpr int kMaxPileGapCols = 2;
inline constexpr int kFaceUpFanRows = 1;
inline constexpr int kHiddenSummaryRows = 1;

static_assert(kFaceUpFanRows == 1,
              "each face-up strip owns exactly one hit-test row");

inline constexpr int kTableauCols =
    (kTableauPiles * kCardCols) + ((kTableauPiles - 1) * kPileGapCols);

// The top row is two groups: stock/waste on the left, four foundations on the
// right. Let the groups consume the tableau's full measure so the remaining
// seven columns become a deliberate centre gap rather than three columns of
// margin around six evenly-spaced cards.
inline constexpr int kStockWastePiles = 2;
inline constexpr int kFoundationPiles = 4;
inline constexpr int kTopPiles = kStockWastePiles + kFoundationPiles;
inline constexpr int kStockWasteCols =
    (kStockWastePiles * kCardCols) +
    ((kStockWastePiles - 1) * kPileGapCols);
inline constexpr int kFoundationCols =
    (kFoundationPiles * kCardCols) +
    ((kFoundationPiles - 1) * kPileGapCols);
inline constexpr int kTopGroupGapCols =
    kTableauCols - kStockWasteCols - kFoundationCols;
inline constexpr int kTopCols =
    kStockWasteCols + kTopGroupGapCols + kFoundationCols;

// Frame border, the top card row, one separating row, the tableau, then status
// and hint. At the floor, surplus rows enlarge tableau capacity. Once enough
// rows exist to preserve the card-art aspect they also enlarge the exposed
// card, with the same larger height accounted for in the worst tableau.
inline constexpr int kFrameCols = 2;
inline constexpr int kFrameRows = 2;
inline constexpr int kSectionGapRows = 1;
inline constexpr int kStatusRows = 1;
inline constexpr int kHintRows = 1;
inline constexpr int kFixedRows =
    kFrameRows + kCardRows + kSectionGapRows + kStatusRows + kHintRows;

// The rows needed to show one pile. An empty pile and an exposed face-down card
// still occupy one full card outline. Once a face-up run exists, a hidden
// prefix contributes one counted strip and every face-up card except the last
// contributes one fan row.
[[nodiscard]] constexpr auto pile_rows(int hidden_count,
                                       int face_up_count) noexcept -> int {
  if (face_up_count <= 0) return kCardRows;
  const int hidden_rows = hidden_count > 0 ? kHiddenSummaryRows : 0;
  return hidden_rows + ((face_up_count - 1) * kFaceUpFanRows) + kCardRows;
}

inline constexpr int kMaxTableauRows =
    pile_rows(kMaxHiddenCards, kMaxFaceUpCards);
inline constexpr int kNeedCols = kTableauCols + kFrameCols;
inline constexpr int kNeedRows = kFixedRows + kMaxTableauRows;
inline constexpr int kMaxNeedCols = (kTableauPiles * kMaxCardCols) +
                                    ((kTableauPiles - 1) * kMaxPileGapCols) +
                                    kFrameCols;

static_assert(kTopPiles == 6);
static_assert(kTopGroupGapCols == 7);
static_assert(kTopCols == kTableauCols);
static_assert(kMaxTableauRows == 16);
static_assert((kMaxTableauCards - 1) + kCardRows == 21,
              "an uncompressed nineteen-card fan does not fit");
static_assert(kNeedCols == 43);
static_assert(kNeedRows == 24);
static_assert(kMaxNeedCols == 91);

struct Layout {
  bool fits{false};
  int frame_x{0};
  int frame_y{0};
  int frame_w{0};
  int frame_h{0};
  int card_cols{kCardCols};
  int card_rows{kCardRows};
  int pile_gap_cols{kPileGapCols};
  int tableau_cols{kTableauCols};
  int top_x{0};
  int top_y{0};
  int tableau_x{0};
  int tableau_y{0};
  int tableau_rows{0};
  int status_y{0};
  int hint_y{0};

  [[nodiscard]] constexpr auto tableau_pile_x(int pile) const noexcept -> int {
    return tableau_x + (pile * (card_cols + pile_gap_cols));
  }

  [[nodiscard]] constexpr auto top_pile_x(int pile) const noexcept -> int {
    if (pile < kStockWastePiles) {
      return top_x + (pile * (card_cols + pile_gap_cols));
    }
    const int stock_waste_cols = (kStockWastePiles * card_cols) + pile_gap_cols;
    const int foundation_cols = (kFoundationPiles * card_cols) +
                                ((kFoundationPiles - 1) * pile_gap_cols);
    const int group_gap_cols =
        tableau_cols - stock_waste_cols - foundation_cols;
    return top_x + stock_waste_cols + group_gap_cols +
           ((pile - kStockWastePiles) * (card_cols + pile_gap_cols));
  }
};

// A running game owns the whole Screen. Cards and gaps grow with available
// geometry up to kMaxNeedCols, then the table centres. Neither dimension
// changes the seven-pile rules extent.
[[nodiscard]] constexpr auto compute_layout(int screen_cols,
                                            int screen_rows) noexcept
    -> Layout {
  Layout out;
  out.status_y = screen_rows >= 3 ? screen_rows - 3 : 0;
  out.hint_y = screen_rows >= 2 ? screen_rows - 2 : 0;

  out.fits = screen_cols >= kNeedCols && screen_rows >= kNeedRows;
  if (!out.fits) return out;

  const int available_cols = std::min(screen_cols, kMaxNeedCols) - kFrameCols;
  const int width_card_cols = std::clamp(
      (available_cols - ((kTableauPiles - 1) * kPileGapCols)) / kTableauPiles,
      kCardCols, kMaxCardCols);
  // The worst tableau and top row each contain one full-height card. Solving
  //   frame/chrome + top + (hidden + 12 fans + final card) <= screen rows
  // leaves this much height for each of those two cards.
  const int height_card_rows =
      std::clamp((screen_rows - 18) / 2, kCardRows, kMaxCardRows);
  const int aspect_card_cols =
      std::max(kCardCols, (height_card_rows * 4 + 1) / 3);
  out.card_cols = std::min(width_card_cols, aspect_card_cols);
  out.card_rows = std::clamp((out.card_cols * 3 + 2) / 4, kCardRows,
                             height_card_rows);
  out.pile_gap_cols = std::clamp(
      (available_cols - (kTableauPiles * out.card_cols)) / (kTableauPiles - 1),
      kPileGapCols, kMaxPileGapCols);
  out.tableau_cols = (kTableauPiles * out.card_cols) +
                     ((kTableauPiles - 1) * out.pile_gap_cols);
  out.frame_w = out.tableau_cols + kFrameCols;
  out.frame_x = (screen_cols - out.frame_w) / 2;
  out.frame_y = 0;
  out.frame_h = screen_rows;

  const int inner_x = out.frame_x + 1;
  out.top_x = inner_x;
  out.top_y = out.frame_y + 1;
  out.tableau_x = inner_x;
  out.tableau_y = out.top_y + out.card_rows + kSectionGapRows;
  out.tableau_rows = out.status_y - out.tableau_y;
  return out;
}

// Map one row within a tableau pile to its absolute card index (bottom/front of
// the stored pile is index zero). A covered hidden summary deliberately has no
// hit: those cards are not legal selections. When no face-up run exists, the
// full card back is the exposed top hidden card and clicking any of its three
// rows identifies the one card that may be flipped.
[[nodiscard]] constexpr auto tableau_card_at(int row, int hidden_count,
                                             int face_up_count,
                                             int card_rows = kCardRows) noexcept
    -> std::optional<int> {
  if (row < 0 || hidden_count < 0 || face_up_count < 0) return std::nullopt;

  if (face_up_count == 0) {
    if (hidden_count == 0 || row >= card_rows) return std::nullopt;
    return hidden_count - 1;
  }

  const int hidden_rows = hidden_count > 0 ? kHiddenSummaryRows : 0;
  if (row < hidden_rows) return std::nullopt;

  const int face_row = row - hidden_rows;
  const int face_rows = ((face_up_count - 1) * kFaceUpFanRows) + card_rows;
  if (face_row >= face_rows) return std::nullopt;

  const int fanned_index = face_row / kFaceUpFanRows;
  const int face_index =
      fanned_index < face_up_count ? fanned_index : face_up_count - 1;
  return hidden_count + face_index;
}

}  // namespace glyphcade::solitaire
