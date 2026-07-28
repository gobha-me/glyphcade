// The registry: every game compiled into this binary is in the menu, and each
// entry describes the game it actually constructs.
//
// Nothing here needs a Screen, a driver or a Shell. That is the point of
// holding a copy of the metadata in GameEntry: the roster is inspectable
// without instantiating anything.

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>
#include <string>

#include <termforge/widgets/detail/width.hpp>

#include <termgame/arcade/game_meta.hpp>
#include <termgame/arcade/registry.hpp>

TEST_CASE("the registry is not empty", "[registry]") {
  // An empty menu is a bootstrap failure, and it is the exact symptom a
  // self-registering static produces once the linker drops its object file. If
  // this ever goes red, read the warning at the top of all_games.cpp before
  // debugging anything else.
  REQUIRE_FALSE(termgame::all_games().empty());
}

TEST_CASE("every entry has a slug, a title and a factory", "[registry]") {
  for (const auto& entry : termgame::all_games()) {
    INFO("slug: " << entry.meta.slug);
    REQUIRE_FALSE(entry.meta.slug.empty());
    REQUIRE_FALSE(entry.meta.title.empty());
    REQUIRE(entry.make != nullptr);
  }
}

TEST_CASE("slugs are unique", "[registry]") {
  // Belt and braces over the static_assert in all_games.cpp. The assert can be
  // deleted while "simplifying"; this cannot be deleted without going red.
  std::set<std::string> seen;
  for (const auto& entry : termgame::all_games()) {
    INFO("duplicate slug: " << entry.meta.slug);
    REQUIRE(seen.insert(std::string(entry.meta.slug)).second);
  }
}

TEST_CASE("every icon is terminal-safe", "[registry]") {
  for (const auto& entry : termgame::all_games()) {
    INFO("slug: " << entry.meta.slug);
    REQUIRE(termgame::icon_is_safe(entry.meta.icon));
    if (!entry.meta.icon.empty()) {
      // The sharper form: the selector reserves exactly kIconCols for the icon,
      // so the icon and the gutter arithmetic must agree or every row shifts.
      REQUIRE(termforge::detail::display_width(entry.meta.icon) ==
              termgame::kIconCols);
    }
  }
}

TEST_CASE("the factory and the metadata agree", "[registry]") {
  // ⚠ The one mistake no compiler and no static_assert can see: copy-paste in
  // all_games.cpp binding game A's metadata to game B's factory. It ships as
  // "the menu says Snake and Tetris starts".
  for (const auto& entry : termgame::all_games()) {
    const std::unique_ptr<termgame::Game> game = entry.make();
    REQUIRE(game != nullptr);
    INFO("table says " << entry.meta.slug << ", factory builds "
                       << game->meta().slug);
    REQUIRE(game->meta().slug == entry.meta.slug);
    REQUIRE(game->meta().title == entry.meta.title);
  }
}

TEST_CASE("the stub game is registered", "[registry]") {
  // ⚠ Delete this case together with the stub game itself, when Epic 3
  // (Minesweeper) lands. See include/termgame/games/stub/stub_game.hpp.
  bool found = false;
  for (const auto& entry : termgame::all_games()) {
    if (entry.meta.slug == "stub") found = true;
  }
  REQUIRE(found);
}
