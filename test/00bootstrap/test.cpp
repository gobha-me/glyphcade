// The scaffold's own smoke test: the library links, and the build options
// actually reached the compiler.

#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include <termgame/build_info.hpp>
#include <version.hpp>

TEST_CASE("the project library is linked and reachable", "[bootstrap][smoke]") {
  REQUIRE(std::string_view{PROGRAM_NAME} == "term-game");

  const auto* v = termgame::version_string();
  REQUIRE(v != nullptr);
  REQUIRE_FALSE(std::string_view{v}.empty());

  // "0.0.0" is correct and expected on a tree with no reachable tag — see
  // cmake/version.cmake. So this asserts the shape, not the value: three
  // dot-separated components, which is what cmake/version_parse.cmake
  // guarantees or refuses.
  const std::string_view sv{v};
  REQUIRE(sv.find('.') != std::string_view::npos);
  REQUIRE(sv.find('.') != sv.rfind('.'));
}

TEST_CASE("build_has_audio agrees with the CMake option",
          "[bootstrap][audio]") {
  // TERMGAME_EXPECT_AUDIO comes from CMake via this dir's CMakeLists.txt;
  // build_has_audio() comes from the library, which was compiled with (or
  // without) a PRIVATE TERMGAME_WITH_AUDIO. Two independent paths from one
  // option — if the plumbing breaks anywhere between cmake/audio.cmake and the
  // compiler, they disagree here.
  //
  // Verified both ways by building twice: detection-default, and an explicit
  // -DTERMGAME_WITH_AUDIO=OFF. The second is the configuration CI runs.
  REQUIRE(termgame::build_has_audio() == (TERMGAME_EXPECT_AUDIO != 0));
}
