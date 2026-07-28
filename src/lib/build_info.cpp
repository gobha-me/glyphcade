#include <termgame/build_info.hpp>

#include <string>

#include <version.hpp>

namespace termgame {

auto version_string() noexcept -> const char* {
  // Function-local static: built once on first call, and its lifetime runs to
  // the end of the program, so handing out .c_str() is safe. version.hpp is
  // generated into the source tree at configure time (cpp-template CT-11) and
  // is gitignored — if it is missing, re-run `cmake -B build`.
  static const std::string kVersion = std::to_string(VERSION_MAJOR) + "." +
                                      std::to_string(VERSION_MINOR) + "." +
                                      std::to_string(VERSION_PATCH);
  return kVersion.c_str();
}

auto build_has_audio() noexcept -> bool {
  // The one place in the project that reads this macro. It is PRIVATE to the
  // library target (see src/lib/CMakeLists.txt), so this translation unit is
  // the only one that can see it — which is the point: the answer crosses into
  // headers, tests and the binary as a value, never as a conditional.
#ifdef TERMGAME_WITH_AUDIO
  return true;
#else
  return false;
#endif
}

}  // namespace termgame
