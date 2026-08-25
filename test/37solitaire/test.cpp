#include <catch2/catch_test_macros.hpp>

#include <array>
#include <set>

#include <glyphcade/games/solitaire/board.hpp>

namespace {

using namespace glyphcade::solitaire;

auto card(Suit suit, Rank rank, bool face_up = true) -> Card {
  return {.suit = suit, .rank = rank, .face_up = face_up};
}

auto foundation_through(Suit suit, Rank rank) -> Pile {
  Pile pile;
  for (int value = 1; value <= rank_value(rank); ++value) {
    pile.push_back(card(suit, static_cast<Rank>(value)));
  }
  return pile;
}

} // namespace

TEST_CASE("a seed produces one complete deterministic Klondike deal",
          "[solitaire][deal]") {
  Board first{20260825};
  Board again{20260825};
  Board other{20260826};
  CHECK(first.position() == again.position());
  CHECK_FALSE(first.position() == other.position());

  const auto& p = first.position();
  CHECK(p.stock.size() == 24);
  std::set<int> ids;
  for (const Card c : p.stock) {
    CHECK_FALSE(c.face_up);
    ids.insert(card_id(c));
  }
  for (int pile = 0; pile < 7; ++pile) {
    const auto& cards = p.tableau[static_cast<std::size_t>(pile)];
    REQUIRE(cards.size() == static_cast<std::size_t>(pile + 1));
    for (std::size_t i = 0; i < cards.size(); ++i) {
      CHECK(cards[i].face_up == (i + 1 == cards.size()));
      ids.insert(card_id(cards[i]));
    }
  }
  CHECK(ids.size() == 52);
}

TEST_CASE("draw-one and draw-three preserve stock order across recycling",
          "[solitaire][stock]") {
  Position p;
  p.stock = {card(Suit::Clubs, Rank::Ace, false),
             card(Suit::Clubs, Rank::Two, false),
             card(Suit::Clubs, Rank::Three, false),
             card(Suit::Clubs, Rank::Four, false)};

  Board draw_three{0};
  draw_three.load(p, DrawMode::Three);
  const auto first = draw_three.act_stock();
  CHECK(first.cards == 3);
  CHECK(draw_three.position().waste.back().rank == Rank::Two);
  CHECK(draw_three.act_stock().cards == 1);
  CHECK(draw_three.position().waste.back().rank == Rank::Ace);
  CHECK(draw_three.act_stock().kind == ActionKind::Recycle);
  CHECK(draw_three.position().stock.back().rank == Rank::Four);

  Board draw_one{0};
  draw_one.load(p, DrawMode::One);
  CHECK(draw_one.act_stock().cards == 1);
  CHECK(draw_one.position().waste.back().rank == Rank::Four);
}

TEST_CASE("a complete draw-three cycle never loses a card",
          "[solitaire][stock][regression]") {
  Board board{20260825, DrawMode::Three};
  const Position initial = board.position();

  while (!board.position().stock.empty()) REQUIRE(board.act_stock().changed);
  REQUIRE(board.position().waste.size() == 24);
  REQUIRE(board.act_stock().kind == ActionKind::Recycle);
  CHECK(board.position().stock == initial.stock);
  CHECK(board.position().waste.empty());

  std::set<int> ids;
  const auto collect = [&](const Pile& pile) {
    for (const Card value : pile) ids.insert(card_id(value));
  };
  collect(board.position().stock);
  collect(board.position().waste);
  for (const auto& pile : board.position().foundations) collect(pile);
  for (const auto& pile : board.position().tableau) collect(pile);
  CHECK(ids.size() == 52);
}

TEST_CASE("tableau moves require an alternating descending run",
          "[solitaire][move]") {
  Position p;
  p.tableau[0] = {card(Suit::Hearts, Rank::Seven),
                  card(Suit::Clubs, Rank::Six)};
  p.tableau[1] = {card(Suit::Spades, Rank::Eight)};
  p.tableau[2] = {card(Suit::Hearts, Rank::Eight)};
  p.tableau[3] = {card(Suit::Diamonds, Rank::King)};
  Board board{0};
  board.load(p);

  const PileRef run{PileKind::Tableau, 0, 0};
  CHECK(board.can_move(run, {PileKind::Tableau, 1, -1}));
  CHECK_FALSE(board.can_move(run, {PileKind::Tableau, 2, -1}));
  CHECK_FALSE(board.can_move(run, {PileKind::Tableau, 4, -1}));
  CHECK(board.can_move({PileKind::Tableau, 3, 0}, {PileKind::Tableau, 4, -1}));

  const auto moved = board.move(run, {PileKind::Tableau, 1, -1});
  REQUIRE(moved.changed);
  CHECK(moved.cards == 2);
  CHECK(board.position().tableau[0].empty());
  CHECK(board.position().tableau[1].size() == 3);
}

TEST_CASE("foundation moves are same-suit ascending and single-card",
          "[solitaire][move]") {
  Position p;
  p.waste = {card(Suit::Hearts, Rank::Ace)};
  p.tableau[0] = {card(Suit::Clubs, Rank::Two), card(Suit::Hearts, Rank::Two)};
  Board board{0};
  board.load(p);

  CHECK(board.move({PileKind::Waste, 0, -1}, {PileKind::Foundation, 2, -1})
            .changed);
  CHECK_FALSE(
      board.can_move({PileKind::Tableau, 0, 0}, {PileKind::Foundation, 2, -1}));
  CHECK(board.move({PileKind::Tableau, 0, 1}, {PileKind::Foundation, 2, -1})
            .changed);
  CHECK(board.position().foundations[2].size() == 2);
}

TEST_CASE("standard and Vegas scoring apply at the model boundary",
          "[solitaire][score]") {
  Position standard;
  standard.waste = {card(Suit::Hearts, Rank::Seven)};
  standard.tableau[0] = {card(Suit::Spades, Rank::Eight)};
  Board board{0};
  board.load(standard, DrawMode::Three, ScoringMode::Standard);
  CHECK(board.move({PileKind::Waste, 0, -1}, {PileKind::Tableau, 0, -1})
            .score_delta == 5);

  Position flip;
  flip.tableau[0] = {card(Suit::Clubs, Rank::Ace, false),
                     card(Suit::Hearts, Rank::Seven)};
  flip.tableau[1] = {card(Suit::Spades, Rank::Eight)};
  board.load(flip, DrawMode::Three, ScoringMode::Standard);
  CHECK(board.move({PileKind::Tableau, 0, 1}, {PileKind::Tableau, 1, -1})
            .score_delta == 5);
  CHECK(board.position().tableau[0].back().face_up);

  Position vegas;
  vegas.score = -52;
  vegas.waste = {card(Suit::Diamonds, Rank::Ace)};
  board.load(vegas, DrawMode::Three, ScoringMode::Vegas);
  CHECK(board.move({PileKind::Waste, 0, -1}, {PileKind::Foundation, 1, -1})
            .score_delta == 5);
  board.load(Position{.waste = {card(Suit::Clubs, Rank::King)}, .score = -52},
             DrawMode::Three, ScoringMode::Vegas);
  CHECK(board.act_stock().score_delta == -52);
}

TEST_CASE("undo restores the complete position and ignores illegal actions",
          "[solitaire][undo]") {
  Position p;
  p.waste = {card(Suit::Hearts, Rank::Seven)};
  p.tableau[0] = {card(Suit::Spades, Rank::Eight)};
  Board board{0};
  board.load(p);
  const Position before = board.position();
  CHECK_FALSE(
      board.move({PileKind::Waste, 0, -1}, {PileKind::Foundation, 2, -1})
          .changed);
  CHECK_FALSE(board.can_undo());
  REQUIRE(
      board.move({PileKind::Waste, 0, -1}, {PileKind::Tableau, 0, -1}).changed);
  CHECK(board.can_undo());
  REQUIRE(board.undo().changed);
  CHECK(board.position() == before);
}

TEST_CASE("undo retains only the most recent fifty changed positions",
          "[solitaire][undo]") {
  Position p;
  p.stock = {card(Suit::Clubs, Rank::Ace, false)};
  Board board{0};
  board.load(p, DrawMode::One);

  for (int action = 0; action < 52; ++action) {
    REQUIRE(board.act_stock().changed);
  }
  for (int undo = 0; undo < 50; ++undo) {
    REQUIRE(board.undo().changed);
  }
  CHECK_FALSE(board.can_undo());
  CHECK_FALSE(board.undo().changed);
}

TEST_CASE("auto-complete proves the position before changing it",
          "[solitaire][auto]") {
  Position p;
  for (int suit = 0; suit < 4; ++suit) {
    p.foundations[static_cast<std::size_t>(suit)] =
        foundation_through(static_cast<Suit>(suit), Rank::Queen);
    p.tableau[static_cast<std::size_t>(suit)] = {
        card(static_cast<Suit>(suit), Rank::King)};
  }
  Board board{0};
  board.load(p);
  CHECK(board.can_auto_complete());
  const auto result = board.auto_complete();
  CHECK(result.changed);
  CHECK(result.cards == 4);
  CHECK(result.won);
  CHECK(board.foundation_count() == 52);
  CHECK_FALSE(board.can_undo());

  p = Position{};
  p.tableau[0] = {card(Suit::Clubs, Rank::Ace, false)};
  board.load(p);
  const Position blocked = board.position();
  CHECK_FALSE(board.can_auto_complete());
  CHECK_FALSE(board.auto_complete().changed);
  CHECK(board.position() == blocked);
}

TEST_CASE("auto-complete includes stock and waste accessibility",
          "[solitaire][auto][stock]") {
  Position p;
  p.foundations[0] = foundation_through(Suit::Clubs, Rank::Queen);
  p.foundations[1] = foundation_through(Suit::Diamonds, Rank::King);
  p.foundations[2] = foundation_through(Suit::Hearts, Rank::King);
  p.foundations[3] = foundation_through(Suit::Spades, Rank::King);
  p.stock = {card(Suit::Clubs, Rank::King, false)};
  Board board{0};
  board.load(p, DrawMode::Three);
  CHECK(board.can_auto_complete());
  CHECK(board.auto_complete().won);
  CHECK(board.foundation_count() == 52);
}
