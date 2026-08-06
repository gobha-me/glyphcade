#pragma once

// glyphcade — Solitaire: the bottom-tier table geometry. Integers only.
//
// This is the layout decision that precedes Epic 8, not an incomplete Game.
// It names no termforge type and owns no rules state. The eventual renderer and
// mouse path will consume the same Layout and tableau_card_at() answer so their
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

// Bottom-tier card. The three interior columns hold the widest label, "10H";
// higher tiers may replace the suit letter or the whole card with art without
// changing the rules extent.
inline constexpr int kCardCols = 5;
inline constexpr int kCardRows = 3;
inline constexpr int kPileGapCols = 1;
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
// and hint. Surplus ROWS enlarge tableau_rows; surplus COLUMNS do not enlarge
// cards or the table, matching the suite-wide ceiling rule.
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

static_assert(kTopPiles == 6);
static_assert(kTopGroupGapCols == 7);
static_assert(kTopCols == kTableauCols);
static_assert(kMaxTableauRows == 16);
static_assert((kMaxTableauCards - 1) + kCardRows == 21,
              "an uncompressed nineteen-card fan does not fit");
static_assert(kNeedCols == 43);
static_assert(kNeedRows == 24);

struct Layout {
  bool fits{false};
  int frame_x{0};
  int frame_y{0};
  int frame_w{0};
  int frame_h{0};
  int top_x{0};
  int top_y{0};
  int tableau_x{0};
  int tableau_y{0};
  int tableau_rows{0};
  int status_y{0};
  int hint_y{0};

  [[nodiscard]] constexpr auto tableau_pile_x(int pile) const noexcept -> int {
    return tableau_x + (pile * (kCardCols + kPileGapCols));
  }

  [[nodiscard]] constexpr auto top_pile_x(int pile) const noexcept -> int {
    if (pile < kStockWastePiles) {
      return top_x + (pile * (kCardCols + kPileGapCols));
    }
    return top_x + kStockWasteCols + kTopGroupGapCols +
           ((pile - kStockWastePiles) * (kCardCols + kPileGapCols));
  }
};

// A running game owns the whole Screen. The table has a prose measure in
// columns and stays centred at kNeedCols, while extra rows are real capacity
// and extend the tableau viewport.
[[nodiscard]] constexpr auto compute_layout(int screen_cols,
                                            int screen_rows) noexcept
    -> Layout {
  Layout out;
  out.status_y = screen_rows >= 3 ? screen_rows - 3 : 0;
  out.hint_y = screen_rows >= 2 ? screen_rows - 2 : 0;

  out.fits = screen_cols >= kNeedCols && screen_rows >= kNeedRows;
  if (!out.fits) return out;

  out.frame_x = (screen_cols - kNeedCols) / 2;
  out.frame_y = 0;
  out.frame_w = kNeedCols;
  out.frame_h = screen_rows;

  const int inner_x = out.frame_x + 1;
  out.top_x = inner_x;
  out.top_y = out.frame_y + 1;
  out.tableau_x = inner_x;
  out.tableau_y = out.top_y + kCardRows + kSectionGapRows;
  out.tableau_rows = screen_rows - kFixedRows;
  return out;
}

// Map one row within a tableau pile to its absolute card index (bottom/front of
// the stored pile is index zero). A covered hidden summary deliberately has no
// hit: those cards are not legal selections. When no face-up run exists, the
// full card back is the exposed top hidden card and clicking any of its three
// rows identifies the one card that may be flipped.
[[nodiscard]] constexpr auto tableau_card_at(int row, int hidden_count,
                                             int face_up_count) noexcept
    -> std::optional<int> {
  if (row < 0 || hidden_count < 0 || face_up_count < 0) return std::nullopt;

  if (face_up_count == 0) {
    if (hidden_count == 0 || row >= kCardRows) return std::nullopt;
    return hidden_count - 1;
  }

  const int hidden_rows = hidden_count > 0 ? kHiddenSummaryRows : 0;
  if (row < hidden_rows) return std::nullopt;

  const int face_row = row - hidden_rows;
  const int face_rows = ((face_up_count - 1) * kFaceUpFanRows) + kCardRows;
  if (face_row >= face_rows) return std::nullopt;

  const int fanned_index = face_row / kFaceUpFanRows;
  const int face_index =
      fanned_index < face_up_count ? fanned_index : face_up_count - 1;
  return hidden_count + face_index;
}

}  // namespace glyphcade::solitaire
