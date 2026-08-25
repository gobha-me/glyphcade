#include <glyphcade/games/solitaire/board.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <utility>

namespace glyphcade::solitaire {
namespace {

constexpr std::size_t kUndoLimit = 50;

[[nodiscard]] auto valid_tableau_run(std::span<const Card> cards) noexcept
    -> bool {
  if (cards.empty() || !cards.front().face_up) return false;
  for (std::size_t i = 1; i < cards.size(); ++i) {
    if (!cards[i].face_up ||
        rank_value(cards[i - 1].rank) != rank_value(cards[i].rank) + 1 ||
        is_red(cards[i - 1].suit) == is_red(cards[i].suit)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] auto signature(const Position& p) -> std::string {
  std::string out;
  const auto append = [&](const Pile& pile) {
    out.push_back(static_cast<char>(pile.size()));
    for (const Card card : pile) {
      out.push_back(static_cast<char>(card_id(card) + 1));
      out.push_back(static_cast<char>(card.face_up ? 1 : 0));
    }
  };
  append(p.stock);
  append(p.waste);
  for (const auto& pile : p.foundations)
    append(pile);
  for (const auto& pile : p.tableau)
    append(pile);
  return out;
}

} // namespace

Board::Board(std::uint64_t seed, DrawMode draw, ScoringMode scoring) {
  reset(seed, draw, scoring);
}

auto Board::reset(std::uint64_t seed, DrawMode draw, ScoringMode scoring)
    -> void {
  m_draw = draw;
  m_scoring = scoring;
  m_position = Position{};
  m_position.score = scoring == ScoringMode::Vegas ? -52 : 0;
  m_undo.clear();

  Pile deck;
  deck.reserve(52);
  for (int suit = 0; suit < 4; ++suit) {
    for (int rank = 1; rank <= 13; ++rank) {
      deck.push_back(Card{.suit = static_cast<Suit>(suit),
                          .rank = static_cast<Rank>(rank),
                          .face_up = false});
    }
  }

  Rng rng{seed};
  for (std::size_t i = deck.size(); i > 1; --i) {
    const auto j = static_cast<std::size_t>(rng.below(i));
    std::swap(deck[i - 1], deck[j]);
  }

  for (int row = 0; row < 7; ++row) {
    for (int pile = row; pile < 7; ++pile) {
      Card card = deck.back();
      deck.pop_back();
      card.face_up = pile == row;
      m_position.tableau[static_cast<std::size_t>(pile)].push_back(card);
    }
  }
  m_position.stock = std::move(deck);
}

auto Board::foundation_count() const noexcept -> int {
  int total = 0;
  for (const auto& pile : m_position.foundations) {
    total += static_cast<int>(pile.size());
  }
  return total;
}

auto Board::source_cards(PileRef from) const noexcept -> std::span<const Card> {
  switch (from.kind) {
    case PileKind::Waste:
      if (m_position.waste.empty()) return {};
      return {&m_position.waste.back(), 1};
    case PileKind::Foundation:
      if (from.pile < 0 || from.pile >= 4 ||
          m_position.foundations[static_cast<std::size_t>(from.pile)].empty()) {
        return {};
      }
      return {
          &m_position.foundations[static_cast<std::size_t>(from.pile)].back(),
          1};
    case PileKind::Tableau:
      if (from.pile < 0 || from.pile >= 7) return {};
      const auto& pile =
          m_position.tableau[static_cast<std::size_t>(from.pile)];
      if (from.card < 0 || static_cast<std::size_t>(from.card) >= pile.size()) {
        return {};
      }
      return std::span<const Card>{pile}.subspan(
          static_cast<std::size_t>(from.card));
  }
  return {};
}

auto Board::target_accepts(PileRef to,
                           std::span<const Card> cards) const noexcept -> bool {
  if (cards.empty()) return false;
  if (to.kind == PileKind::Tableau) {
    if (to.pile < 0 || to.pile >= 7 || !valid_tableau_run(cards)) return false;
    const auto& pile = m_position.tableau[static_cast<std::size_t>(to.pile)];
    if (pile.empty()) return cards.front().rank == Rank::King;
    const Card top = pile.back();
    return top.face_up &&
           rank_value(top.rank) == rank_value(cards.front().rank) + 1 &&
           is_red(top.suit) != is_red(cards.front().suit);
  }
  if (to.kind == PileKind::Foundation) {
    if (to.pile < 0 || to.pile >= 4 || cards.size() != 1) return false;
    const Card card = cards.front();
    if (suit_index(card.suit) != to.pile) return false;
    const auto& pile =
        m_position.foundations[static_cast<std::size_t>(to.pile)];
    return rank_value(card.rank) == static_cast<int>(pile.size()) + 1;
  }
  return false;
}

auto Board::can_move(PileRef from, PileRef to) const noexcept -> bool {
  if (won() || from == to) return false;
  if (from.kind == PileKind::Tableau && to.kind == PileKind::Tableau &&
      from.pile == to.pile) {
    return false;
  }
  return target_accepts(to, source_cards(from));
}

auto Board::remember() -> void {
  if (m_undo.size() == kUndoLimit) m_undo.erase(m_undo.begin());
  m_undo.push_back(m_position);
}

auto Board::update_win() -> bool {
  const bool now = foundation_count() == 52;
  if (now) m_position.state = State::Won;
  return now;
}

auto Board::move_impl(PileRef from, PileRef to, bool remember_state)
    -> ActionResult {
  if (!can_move(from, to)) return {};
  if (remember_state) remember();

  const int score_before = m_position.score;
  Pile moved;
  if (from.kind == PileKind::Waste) {
    moved.push_back(m_position.waste.back());
    m_position.waste.pop_back();
  } else if (from.kind == PileKind::Foundation) {
    auto& pile = m_position.foundations[static_cast<std::size_t>(from.pile)];
    moved.push_back(pile.back());
    pile.pop_back();
  } else {
    auto& pile = m_position.tableau[static_cast<std::size_t>(from.pile)];
    const auto first = pile.begin() + from.card;
    moved.assign(first, pile.end());
    pile.erase(first, pile.end());
  }

  if (to.kind == PileKind::Foundation) {
    auto& target = m_position.foundations[static_cast<std::size_t>(to.pile)];
    target.push_back(moved.front());
    m_position.score += m_scoring == ScoringMode::Standard ? 10 : 5;
  } else {
    auto& target = m_position.tableau[static_cast<std::size_t>(to.pile)];
    target.insert(target.end(), moved.begin(), moved.end());
    if (m_scoring == ScoringMode::Standard && from.kind == PileKind::Waste) {
      m_position.score += 5;
    } else if (m_scoring == ScoringMode::Standard &&
               from.kind == PileKind::Foundation) {
      m_position.score -= 15;
    }
  }

  if (from.kind == PileKind::Tableau) {
    auto& source = m_position.tableau[static_cast<std::size_t>(from.pile)];
    if (!source.empty() && !source.back().face_up) {
      source.back().face_up = true;
      if (m_scoring == ScoringMode::Standard) m_position.score += 5;
    }
  }

  ++m_position.moves;
  const bool now_won = update_win();
  return {.changed = true,
          .kind = to.kind == PileKind::Foundation ? ActionKind::Foundation
                                                  : ActionKind::Place,
          .cards = static_cast<int>(moved.size()),
          .score_delta = m_position.score - score_before,
          .won = now_won};
}

auto Board::move(PileRef from, PileRef to) -> ActionResult {
  return move_impl(from, to, true);
}

auto Board::stock_impl(bool remember_state) -> ActionResult {
  if (won()) return {};
  if (!m_position.stock.empty()) {
    if (remember_state) remember();
    const int count = std::min(static_cast<int>(m_draw),
                               static_cast<int>(m_position.stock.size()));
    for (int i = 0; i < count; ++i) {
      Card card = m_position.stock.back();
      m_position.stock.pop_back();
      card.face_up = true;
      m_position.waste.push_back(card);
    }
    ++m_position.moves;
    return {.changed = true, .kind = ActionKind::Deal, .cards = count};
  }
  if (m_position.waste.empty()) return {};

  if (remember_state) remember();
  const int score_before = m_position.score;
  const int count = static_cast<int>(m_position.waste.size());
  while (!m_position.waste.empty()) {
    Card card = m_position.waste.back();
    m_position.waste.pop_back();
    card.face_up = false;
    m_position.stock.push_back(card);
  }
  if (m_scoring == ScoringMode::Vegas) m_position.score -= 52;
  ++m_position.moves;
  return {.changed = true,
          .kind = ActionKind::Recycle,
          .cards = count,
          .score_delta = m_position.score - score_before};
}

auto Board::act_stock() -> ActionResult {
  return stock_impl(true);
}

auto Board::flip_impl(int pile_index, bool remember_state) -> ActionResult {
  if (won() || pile_index < 0 || pile_index >= 7) return {};
  auto& pile = m_position.tableau[static_cast<std::size_t>(pile_index)];
  if (pile.empty() || pile.back().face_up) return {};
  if (remember_state) remember();
  const int score_before = m_position.score;
  pile.back().face_up = true;
  if (m_scoring == ScoringMode::Standard) m_position.score += 5;
  ++m_position.moves;
  return {.changed = true,
          .kind = ActionKind::Flip,
          .cards = 1,
          .score_delta = m_position.score - score_before};
}

auto Board::flip_tableau(int pile) -> ActionResult {
  return flip_impl(pile, true);
}

auto Board::undo() -> ActionResult {
  if (!can_undo()) return {};
  m_position = std::move(m_undo.back());
  m_undo.pop_back();
  return {.changed = true, .kind = ActionKind::Undo};
}

auto Board::auto_plan() const -> std::optional<std::vector<AutoStep>> {
  if (won()) return std::nullopt;
  for (const auto& pile : m_position.tableau) {
    if (std::ranges::any_of(pile, [](Card card) { return !card.face_up; })) {
      return std::nullopt;
    }
  }

  Board copy = *this;
  copy.m_undo.clear();
  std::vector<AutoStep> steps;
  std::set<std::string> seen;

  for (int guard = 0; guard < 1000 && !copy.won(); ++guard) {
    bool placed = false;
    for (int pile = 0; pile < 7 && !placed; ++pile) {
      const auto& source =
          copy.m_position.tableau[static_cast<std::size_t>(pile)];
      if (source.empty()) continue;
      const PileRef from{PileKind::Tableau, pile,
                         static_cast<int>(source.size()) - 1};
      const PileRef to{PileKind::Foundation, suit_index(source.back().suit),
                       -1};
      if (copy.move_impl(from, to, false).changed) {
        steps.push_back({.stock = false, .from = from, .to = to});
        placed = true;
      }
    }
    if (placed) continue;

    if (!copy.m_position.waste.empty()) {
      const Card top = copy.m_position.waste.back();
      const PileRef from{PileKind::Waste, 0, -1};
      const PileRef to{PileKind::Foundation, suit_index(top.suit), -1};
      if (copy.move_impl(from, to, false).changed) {
        steps.push_back({.stock = false, .from = from, .to = to});
        continue;
      }
    }

    const std::string state = signature(copy.m_position);
    if (!seen.insert(state).second) return std::nullopt;
    if (!copy.stock_impl(false).changed) return std::nullopt;
    steps.push_back({.stock = true});
  }

  if (!copy.won()) return std::nullopt;
  return steps;
}

auto Board::can_auto_complete() const -> bool {
  return auto_plan().has_value();
}

auto Board::auto_complete() -> ActionResult {
  const auto plan = auto_plan();
  if (!plan) return {};
  remember();
  const int score_before = m_position.score;
  int cards = 0;
  for (const AutoStep& step : *plan) {
    const ActionResult result =
        step.stock ? stock_impl(false) : move_impl(step.from, step.to, false);
    if (!result.changed) {
      m_position = std::move(m_undo.back());
      m_undo.pop_back();
      return {};
    }
    if (!step.stock) cards += result.cards;
  }
  return {.changed = true,
          .kind = ActionKind::AutoComplete,
          .cards = cards,
          .score_delta = m_position.score - score_before,
          .won = won()};
}

auto Board::load(Position position, DrawMode draw, ScoringMode scoring)
    -> void {
  m_position = std::move(position);
  m_draw = draw;
  m_scoring = scoring;
  m_undo.clear();
  update_win();
}

} // namespace glyphcade::solitaire
