#pragma once

// Klondike's rules model. This header deliberately names no TermForge type:
// deals, moves, scoring, undo and auto-complete are all testable without a
// Screen or a terminal.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glyphcade/arcade/rng.hpp>

namespace glyphcade::solitaire {

enum class Suit : std::uint8_t { Clubs, Diamonds, Hearts, Spades };
enum class Rank : std::uint8_t {
  Ace = 1,
  Two,
  Three,
  Four,
  Five,
  Six,
  Seven,
  Eight,
  Nine,
  Ten,
  Jack,
  Queen,
  King,
};

enum class DrawMode : std::uint8_t { One = 1, Three = 3 };
enum class ScoringMode : std::uint8_t { Standard, Vegas };
enum class State : std::uint8_t { Playing, Won };

struct Card {
  Suit suit{Suit::Clubs};
  Rank rank{Rank::Ace};
  bool face_up{false};

  auto operator==(const Card&) const -> bool = default;
};

using Pile = std::vector<Card>;

struct Position {
  Pile stock{};
  Pile waste{};
  std::array<Pile, 4> foundations{};
  std::array<Pile, 7> tableau{};
  int score{0};
  int moves{0};
  State state{State::Playing};

  auto operator==(const Position&) const -> bool = default;
};

enum class PileKind : std::uint8_t { Waste, Foundation, Tableau };

struct PileRef {
  PileKind kind{PileKind::Waste};
  int pile{0};
  // Tableau only. -1 means the accessible top card for waste/foundation.
  int card{-1};

  auto operator==(const PileRef&) const -> bool = default;
};

enum class ActionKind : std::uint8_t {
  None,
  Deal,
  Recycle,
  Flip,
  Place,
  Foundation,
  Undo,
  AutoComplete,
};

struct ActionResult {
  bool changed{false};
  ActionKind kind{ActionKind::None};
  int cards{0};
  int score_delta{0};
  bool won{false};
};

[[nodiscard]] constexpr auto suit_index(Suit suit) noexcept -> int {
  return static_cast<int>(suit);
}

[[nodiscard]] constexpr auto rank_value(Rank rank) noexcept -> int {
  return static_cast<int>(rank);
}

[[nodiscard]] constexpr auto card_id(Card card) noexcept -> int {
  return suit_index(card.suit) * 13 + rank_value(card.rank) - 1;
}

[[nodiscard]] constexpr auto is_red(Suit suit) noexcept -> bool {
  return suit == Suit::Diamonds || suit == Suit::Hearts;
}

class Board {
 public:
  Board(std::uint64_t seed, DrawMode draw = DrawMode::Three,
        ScoringMode scoring = ScoringMode::Standard);

  auto reset(std::uint64_t seed, DrawMode draw, ScoringMode scoring) -> void;

  [[nodiscard]] auto position() const noexcept -> const Position& {
    return m_position;
  }
  [[nodiscard]] auto draw_mode() const noexcept -> DrawMode { return m_draw; }
  [[nodiscard]] auto scoring_mode() const noexcept -> ScoringMode {
    return m_scoring;
  }
  [[nodiscard]] auto state() const noexcept -> State {
    return m_position.state;
  }
  [[nodiscard]] auto won() const noexcept -> bool {
    return m_position.state == State::Won;
  }
  [[nodiscard]] auto score() const noexcept -> int { return m_position.score; }
  [[nodiscard]] auto moves() const noexcept -> int { return m_position.moves; }
  [[nodiscard]] auto foundation_count() const noexcept -> int;

  [[nodiscard]] auto can_move(PileRef from, PileRef to) const noexcept -> bool;
  auto move(PileRef from, PileRef to) -> ActionResult;
  auto act_stock() -> ActionResult;
  auto flip_tableau(int pile) -> ActionResult;

  [[nodiscard]] auto can_undo() const noexcept -> bool {
    return !won() && !m_undo.empty();
  }
  auto undo() -> ActionResult;

  // Trivially solvable means a dry-run can clear the position using foundation
  // moves and stock operations only. No tableau rearrangement is guessed.
  [[nodiscard]] auto can_auto_complete() const -> bool;
  auto auto_complete() -> ActionResult;

  // Exact fixture seam for headless rule tests.
  auto load(Position position, DrawMode draw = DrawMode::Three,
            ScoringMode scoring = ScoringMode::Standard) -> void;

 private:
  struct AutoStep {
    bool stock{false};
    PileRef from{};
    PileRef to{};
  };

  [[nodiscard]] auto source_cards(PileRef from) const noexcept
      -> std::span<const Card>;
  [[nodiscard]] auto target_accepts(PileRef to,
                                    std::span<const Card> cards) const noexcept
      -> bool;
  auto move_impl(PileRef from, PileRef to, bool remember) -> ActionResult;
  auto stock_impl(bool remember) -> ActionResult;
  auto flip_impl(int pile, bool remember) -> ActionResult;
  auto remember() -> void;
  auto update_win() -> bool;
  [[nodiscard]] auto auto_plan() const -> std::optional<std::vector<AutoStep>>;

  Position m_position{};
  DrawMode m_draw{DrawMode::Three};
  ScoringMode m_scoring{ScoringMode::Standard};
  std::vector<Position> m_undo{};
};

} // namespace glyphcade::solitaire
