#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>

#include <glyphcade/arcade/context.hpp>
#include <glyphcade/arcade/scores.hpp>
#include <glyphcade/games/solitaire/solitaire.hpp>

namespace {

using glyphcade::GameContext;
using glyphcade::Solitaire;
using namespace glyphcade::solitaire;

[[nodiscard]] auto key(termforge::Key value) -> termforge::Event {
  return termforge::Event{termforge::KeyEvent{.key = value}};
}

[[nodiscard]] auto ch(char32_t value) -> termforge::Event {
  return termforge::Event{
      termforge::KeyEvent{.key = termforge::Key::Char, .ch = value}};
}

[[nodiscard]] auto mouse(int x, int y, bool pressed, bool motion = false)
    -> termforge::Event {
  return termforge::Event{termforge::MouseEvent{
      .x = x, .y = y, .button = 0, .pressed = pressed, .motion = motion}};
}

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

auto start(Solitaire& game, GameContext& context) -> void {
  game.start(context);
  REQUIRE(game.options_open());
  REQUIRE(game.on_event(key(termforge::Key::Enter)));
  REQUIRE_FALSE(game.options_open());
}

[[nodiscard]] auto all_seven_bit(const termforge::Screen& screen) -> bool {
  for (int y = 0; y < screen.rows(); ++y) {
    for (int x = 0; x < screen.cols(); ++x) {
      for (char byte : screen.text_at(x, y)) {
        if (static_cast<unsigned char>(byte) >= 0x80) return false;
      }
    }
  }
  return true;
}

[[nodiscard]] auto row_text(const termforge::Screen& screen, int y)
    -> std::string {
  std::string out;
  for (int x = 0; x < screen.cols(); ++x) out += screen.text_at(x, y);
  return out;
}

struct Reports {
  std::vector<termforge::ErrorEvent> events;

  static auto receive(void* owner, const termforge::ErrorEvent& event) -> void {
    static_cast<Reports*>(owner)->events.push_back(event);
  }
};

} // namespace

TEST_CASE("the options defaults start fresh draw-three Standard play",
          "[solitaire][ui][options]") {
  GameContext context;
  Solitaire game;
  start(game, context);
  CHECK(game.board().draw_mode() == DrawMode::Three);
  CHECK(game.board().scoring_mode() == ScoringMode::Standard);
  CHECK(game.board().position().stock.size() == 24);

  REQUIRE(game.on_event(key(termforge::Key::Enter)));
  CHECK(game.board().position().stock.size() == 21);
  CHECK(game.board().position().waste.size() == 3);
}

TEST_CASE("the bottom tier is an information-complete ASCII game",
          "[solitaire][ui][ascii]") {
  GameContext context;
  Reports reports;
  context.set_reporter(&reports, &Reports::receive);
  Solitaire game;
  start(game, context);

  termforge::Screen screen{80, 24};
  game.draw(screen);
  CHECK(all_seven_bit(screen));
  CHECK(screen.text_at(game.layout().top_pile_x(0), game.layout().top_y) ==
        "[");
  REQUIRE(reports.events.size() == 1);
  CHECK(reports.events[0].message.find("ASCII text cards") !=
        std::string::npos);
  CHECK(game.pixel_regions().empty());
}

TEST_CASE("truecolour exposes atlas regions without hiding selection cues",
          "[solitaire][ui][pixels]") {
  GameContext context;
  termforge::Capabilities caps;
  caps.truecolor = true;
  context.set_capabilities(caps);
  context.set_border_style(termforge::BorderStyle::Rounded);
  Reports reports;
  context.set_reporter(&reports, &Reports::receive);
  Solitaire game;
  start(game, context);
  Position p;
  p.tableau[0] = {card(Suit::Clubs, Rank::Ace, false),
                  card(Suit::Spades, Rank::Eight),
                  card(Suit::Hearts, Rank::Seven)};
  game.board().load(p);

  termforge::Screen screen{80, 24};
  game.draw(screen);
  const auto regions = game.pixel_regions();
  REQUIRE_FALSE(regions.empty());
  const termforge::Rect selected_stock{game.layout().top_pile_x(0),
                                       game.layout().top_y,
                                       game.layout().card_cols,
                                       game.layout().card_rows};
  CHECK(std::ranges::find(regions, selected_stock) == regions.end());
  const int tableau_x = game.layout().tableau_pile_x(0);
  CHECK(screen.text_at(tableau_x + 1, game.layout().tableau_y) == "0");
  CHECK(screen.text_at(tableau_x + 2, game.layout().tableau_y) == "1");
  CHECK(std::ranges::find(
            regions,
            termforge::Rect{tableau_x, game.layout().tableau_y,
                            game.layout().card_cols, 1}) == regions.end());
  const auto full = std::ranges::find_if(regions, [&](termforge::Rect region) {
    return region.h == game.layout().card_rows;
  });
  REQUIRE(full != regions.end());
  REQUIRE(game.draw_pixels(*full, {50, 60}) != nullptr);
  CHECK(game.draw_pixels(*full, {50, 60})->width() == 50);
  CHECK(game.draw_pixels(*full, {50, 60})->height() == 60);
  CHECK(game.pixel_placement(*full).layer ==
        termforge::ImageLayer::above_text());
  bool found_crop = false;
  for (const auto region : regions) {
    if (region.h == 1) {
      const auto placement = game.pixel_placement(region);
      CHECK_FALSE(placement.source.has_value());
      REQUIRE(game.draw_pixels(region, {50, 10}) != nullptr);
      CHECK(game.draw_pixels(region, {50, 10})->width() == 50);
      CHECK(game.draw_pixels(region, {50, 10})->height() == 10);
      found_crop = true;
      break;
    }
  }
  CHECK(found_crop);
  REQUIRE(reports.events.size() == 1);
  CHECK(reports.events[0].message.find("raster cards") != std::string::npos);
}

TEST_CASE("Kitty native art does not report a degradation",
          "[solitaire][ui][pixels]") {
  GameContext context;
  termforge::Capabilities caps;
  caps.kitty_graphics = true;
  caps.truecolor = true;
  context.set_capabilities(caps);
  Reports reports;
  context.set_reporter(&reports, &Reports::receive);
  Solitaire game;
  start(game, context);
  termforge::Screen screen{80, 24};
  game.draw(screen);
  const auto regions = game.pixel_regions();
  REQUIRE_FALSE(regions.empty());
  const termforge::Rect selected_stock{game.layout().top_pile_x(0),
                                       game.layout().top_y,
                                       game.layout().card_cols,
                                       game.layout().card_rows};
  CHECK(std::ranges::find(regions, selected_stock) != regions.end());
  const auto* image = game.draw_pixels(regions.front(), {40, 48});
  REQUIRE(image != nullptr);
  CHECK(image->width() == 40);
  CHECK(image->height() == 48);
  CHECK(image->at(0, 0).r == 0x40);
  CHECK(image->at(0, 0).g == 0x80);
  CHECK(image->at(0, 0).b == 0xFF);
  CHECK(game.pixel_placement(regions.front()).layer ==
        termforge::ImageLayer::below_text());
  CHECK(reports.events.empty());
}

TEST_CASE("mouse dragging highlights and commits through the model",
          "[solitaire][ui][mouse]") {
  GameContext context;
  Solitaire game;
  start(game, context);
  Position p;
  p.tableau[0] = {card(Suit::Hearts, Rank::Seven)};
  p.tableau[1] = {card(Suit::Spades, Rank::Eight)};
  game.board().load(p);

  termforge::Screen screen{80, 24};
  game.draw(screen);
  const auto& layout = game.layout();
  const int source_x = layout.tableau_pile_x(0) + 2;
  const int target_x = layout.tableau_pile_x(1) + 2;
  const int y = layout.tableau_y + 1;

  REQUIRE(game.on_event(mouse(source_x, y, true)));
  game.draw(screen);
  CHECK(screen.text_at(layout.tableau_pile_x(1), layout.tableau_y) == "<");
  REQUIRE(game.on_event(mouse(target_x, y, true, true)));
  REQUIRE(game.on_event(mouse(target_x, y, false)));
  CHECK(game.board().position().tableau[0].empty());
  REQUIRE(game.board().position().tableau[1].size() == 2);
  CHECK(game.board().position().tableau[1].back().rank == Rank::Seven);
  CHECK_FALSE(game.selected_source().has_value());
}

TEST_CASE("a legal tableau run moves as one group without motion reports",
          "[solitaire][ui][mouse][regression]") {
  GameContext context;
  Solitaire game{1234};
  start(game, context);
  Position p;
  p.tableau[0] = {card(Suit::Hearts, Rank::Seven),
                  card(Suit::Clubs, Rank::Six),
                  card(Suit::Diamonds, Rank::Five)};
  p.tableau[1] = {card(Suit::Spades, Rank::Eight)};
  game.board().load(p);

  termforge::Screen screen{80, 24};
  game.draw(screen);
  const auto& layout = game.layout();
  const int source_x = layout.tableau_pile_x(0) + 2;
  const int target_x = layout.tableau_pile_x(1) + 2;
  const int y = layout.tableau_y;

  REQUIRE(game.on_event(mouse(source_x, y, true)));
  REQUIRE(game.on_event(mouse(target_x, y, false)));
  CHECK(game.board().position().tableau[0].empty());
  REQUIRE(game.board().position().tableau[1].size() == 4);
  CHECK(game.board().position().tableau[1][1].rank == Rank::Seven);
  CHECK(game.board().position().tableau[1][2].rank == Rank::Six);
  CHECK(game.board().position().tableau[1][3].rank == Rank::Five);
}

TEST_CASE("recycling the stock restores its cards and visible count",
          "[solitaire][ui][stock][regression]") {
  GameContext context;
  termforge::Capabilities caps;
  caps.kitty_graphics = true;
  caps.truecolor = true;
  context.set_capabilities(caps);
  Solitaire game{1234};
  start(game, context);
  Position p;
  p.waste = {card(Suit::Clubs, Rank::Ace),
             card(Suit::Diamonds, Rank::Two),
             card(Suit::Hearts, Rank::Three)};
  game.board().load(p, DrawMode::Three);

  termforge::Screen screen{80, 24};
  game.draw(screen);
  const auto& layout = game.layout();
  REQUIRE(game.on_event(mouse(layout.top_pile_x(0) + 1, layout.top_y + 1,
                              true)));
  CHECK(game.board().position().stock.size() == 3);
  CHECK(game.board().position().waste.empty());
  game.draw(screen);
  CHECK(row_text(screen, layout.status_y).find("deck 3+0") !=
        std::string::npos);
  const termforge::Rect stock{layout.top_pile_x(0), layout.top_y,
                              layout.card_cols, layout.card_rows};
  const auto regions = game.pixel_regions();
  CHECK(std::ranges::find(regions, stock) != regions.end());
}

TEST_CASE("new deals advance a deterministic seeded sequence",
          "[solitaire][ui][deal][regression]") {
  GameContext first_context;
  GameContext second_context;
  Solitaire first{987654321};
  Solitaire second{987654321};
  start(first, first_context);
  start(second, second_context);
  const Position first_deal = first.board().position();
  CHECK(first_deal == second.board().position());

  REQUIRE(first.on_event(ch(U'n')));
  REQUIRE(second.on_event(ch(U'n')));
  CHECK(first.board().position() == second.board().position());
  CHECK_FALSE(first.board().position() == first_deal);
}

TEST_CASE("fixed ticks advance the timer only while the deal is active",
          "[solitaire][ui][tick]") {
  GameContext context;
  Solitaire game;
  start(game, context);
  game.tick(std::chrono::duration<double>{0.25});
  CHECK(game.elapsed() == std::chrono::duration<double>{0.25});

  Position p;
  for (int suit = 0; suit < 4; ++suit) {
    for (int rank = 1; rank <= 13; ++rank) {
      p.foundations[static_cast<std::size_t>(suit)].push_back(
          card(static_cast<Suit>(suit), static_cast<Rank>(rank)));
    }
  }
  game.board().load(p);
  game.tick(std::chrono::duration<double>{1.0});
  CHECK(game.elapsed() == std::chrono::duration<double>{0.25});
}

TEST_CASE("a win records score moves and elapsed time by rules mode",
          "[solitaire][ui][scores]") {
  glyphcade::scores::Store scores;
  GameContext context;
  context.set_scores(&scores);
  Solitaire game;
  start(game, context);
  game.tick(std::chrono::duration<double>{1.25});

  Position p;
  for (int suit = 0; suit < 4; ++suit) {
    p.foundations[static_cast<std::size_t>(suit)] =
        foundation_through(static_cast<Suit>(suit), Rank::Queen);
    p.tableau[static_cast<std::size_t>(suit)] = {
        card(static_cast<Suit>(suit), Rank::King)};
  }
  game.board().load(p);
  REQUIRE(game.on_event(ch(U'a')));
  REQUIRE(game.board().won());

  const std::string prefix{game.score_key()};
  CHECK(scores.get("solitaire", prefix + ":score") == 40);
  CHECK(scores.get("solitaire", prefix + ":moves") == 4);
  CHECK(scores.get("solitaire", prefix + ":time_ms") == 1250);
  CHECK(scores.dirty());
}

TEST_CASE("the exact geometry floor owns play rendering",
          "[solitaire][ui][geometry]") {
  GameContext context;
  Solitaire game;
  start(game, context);
  termforge::Screen small{42, 24};
  game.draw(small);
  CHECK_FALSE(game.layout().fits);
  CHECK(small.text_at(0, 0) == "S");

  termforge::Screen exact{43, 24};
  game.draw(exact);
  CHECK(game.layout().fits);
  CHECK(exact.text_at(game.layout().frame_x, game.layout().frame_y) == "+");
}

TEST_CASE("wide terminals enlarge cards up to the table ceiling",
          "[solitaire][ui][geometry]") {
  GameContext context;
  termforge::Capabilities caps;
  caps.truecolor = true;
  context.set_capabilities(caps);
  Solitaire game;
  start(game, context);

  termforge::Screen standard{80, 30};
  game.draw(standard);
  CHECK(game.layout().card_cols == 8);
  CHECK(game.layout().card_rows == 6);
  CHECK(game.layout().frame_w == 70);
  CHECK(standard.text_at(game.layout().top_pile_x(0) + 7,
                         game.layout().top_y) == "]");

  termforge::Screen wide{160, 40};
  game.draw(wide);
  CHECK(game.layout().card_cols == kMaxCardCols);
  CHECK(game.layout().card_rows == kMaxCardRows);
  CHECK(game.layout().pile_gap_cols == kMaxPileGapCols);
  CHECK(game.layout().frame_w == kMaxNeedCols);
  CHECK(game.layout().frame_x == (160 - kMaxNeedCols) / 2);
  REQUIRE_FALSE(game.pixel_regions().empty());
  for (const termforge::Rect region : game.pixel_regions()) {
    CHECK(region.w == kMaxCardCols);
  }
  const int state_x = game.layout().frame_x + game.layout().frame_w - 1 - 7;
  CHECK(wide.text_at(state_x, game.layout().status_y) == "P");
  CHECK(wide.text_at(153, game.layout().status_y).empty());
}
