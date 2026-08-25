// Failure-focused testing against glyphcade's own level parser.
//
// The parser returns std::expected so malformed external data is a value the
// caller must handle, not an exception or a partially-constructed Level. Keep
// every failure arm here, followed by the exact size boundaries and only then
// the ordinary successful parse. This is also the project's compile-time
// canary for the C++23 std::expected floor.

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glyphcade/games/sokoban/level.hpp>

namespace {

using namespace glyphcade::sokoban;

auto expect_error(std::span<const std::string_view> rows, ParseError expected,
                  std::string_view message) -> void {
  const auto result = parse(rows, "failure fixture", 0);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == expected);
  CHECK(describe(result.error()) == message);
}

auto expect_error(std::initializer_list<std::string_view> rows,
                  ParseError expected, std::string_view message) -> void {
  const std::vector<std::string_view> owned{rows};
  expect_error(owned, expected, message);
}

} // namespace

TEST_CASE("Sokoban parse failures are explicit and complete",
          "[failure][sokoban][parse]") {
  SECTION("empty input") {
    expect_error({}, ParseError::Empty, "level is empty");
    expect_error({"", ""}, ParseError::Empty, "level is empty");
  }

  SECTION("either dimension beyond the ceiling") {
    const std::string too_wide(static_cast<std::size_t>(kMaxCols + 1), '#');
    const std::vector<std::string_view> wide{too_wide};
    expect_error(wide, ParseError::TooLarge, "level is too large");

    const std::vector<std::string_view> tall(
        static_cast<std::size_t>(kMaxRows + 1), "#");
    expect_error(tall, ParseError::TooLarge, "level is too large");
  }

  SECTION("unknown character") {
    expect_error({"#####", "#@X.#", "#$  #", "#####"}, ParseError::BadChar,
                 "level has an unknown character");
  }

  SECTION("missing player") {
    expect_error({"#####", "# $.#", "#####"}, ParseError::NoPlayer,
                 "level has no player");
  }

  SECTION("multiple players") {
    // The browser reference scans for the player and silently keeps the last
    // match. The parser refuses to choose between malformed starting states.
    expect_error({"#####", "#@+*#", "#####"}, ParseError::ManyPlayers,
                 "level has more than one player");
  }

  SECTION("no goals") {
    expect_error({"#####", "#@$ #", "#####"}, ParseError::NoGoals,
                 "level has no goals");
  }

  SECTION("box and goal counts differ") {
    // The reference's win check looks only for remaining boxes. Unequal counts
    // can therefore produce either an early win or an unwinnable level.
    expect_error({"#######", "#@$ ..#", "#######"}, ParseError::CountMismatch,
                 "level has boxes and goals unequal");
    expect_error({"#######", "#@$$ .#", "#######"}, ParseError::CountMismatch,
                 "level has boxes and goals unequal");
  }
}

TEST_CASE("Sokoban parse size ceilings are inclusive",
          "[failure][sokoban][parse][boundary]") {
  SECTION("maximum width") {
    std::string row(static_cast<std::size_t>(kMaxCols), ' ');
    row[0] = '@';
    row[1] = '$';
    row[2] = '.';
    const std::vector<std::string_view> rows{row};

    const auto result = parse(rows, "max width", 1);
    REQUIRE(result.has_value());
    CHECK(result->w == kMaxCols);
    CHECK(result->h == 1);
  }

  SECTION("maximum height") {
    std::vector<std::string_view> rows(static_cast<std::size_t>(kMaxRows), " ");
    rows[0] = "@$ .";

    const auto result = parse(rows, "max height", 1);
    REQUIRE(result.has_value());
    CHECK(result->w == 4);
    CHECK(result->h == kMaxRows);
  }
}

TEST_CASE("Sokoban parse ordinary input succeeds last",
          "[failure][sokoban][parse][happy]") {
  const std::vector<std::string_view> rows{
      "#####",
      "#@$.#",
      "#####",
  };

  const auto result = parse(rows, "ordinary", 7);
  REQUIRE(result.has_value());
  CHECK(result->w == 5);
  CHECK(result->h == 3);
  CHECK(result->name == "ordinary");
  CHECK(result->par == 7);
  CHECK(result->player == Pos{1, 1});
  REQUIRE(result->boxes.size() == 1);
  CHECK(result->boxes[0] == Pos{2, 1});
  CHECK(result->is_goal(3, 1));
}
