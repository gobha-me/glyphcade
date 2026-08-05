#include <glyphcade/arcade/hud.hpp>

#include <algorithm>

namespace glyphcade::hud {

auto draw_status_row(termforge::Screen& screen, int y,
                     std::span<const std::string> fields, std::string_view word,
                     termforge::Rgb field_fg, termforge::Rgb word_fg,
                     termforge::Rgb bg, std::string_view sep) -> void {
  // ⚠ No y-range guard here, deliberately. Screen::write_text already clips,
  // and the callers disagree about which rows are legal: four games put the
  // status row at 0 and the hints at rows-1, Sokoban puts them at rows-3 and
  // rows-2 with a `fits ? status_y : 0` fallback. A guard here would have to
  // encode one of those opinions and would silently drop the other's row.
  // ⚠ THIS max(0, ...) IS AN UNKILLABLE MUTATION, and it is kept deliberately.
  // Removing it changes no observable behaviour at any width: termforge's
  // write_text does `start_x = x < 0 ? 0 : x` itself (core/screen.cpp), and
  // `budget` is negative in that regime either way, so no field is appended
  // whichever it is. No test can tell the difference, and none should be
  // contorted into pretending to.
  //
  // It stays because that upstream clamp is NOT in write_text's documented
  // contract — the header promises clipping at the RIGHT edge and says nothing
  // about a negative x. A guard whose only defence is an unwritten
  // implementation detail of the version we happen to be pinned to is exactly
  // what a bump removes silently.
  //
  // ⚠ term-game#36 was named here as the event to re-check this against, and it
  // has now happened: the pin crossed six tags to v0.6.0 (not the v0.5.1 this
  // comment used to predict). Re-checked at that bump, and BOTH halves still
  // hold — `screen.cpp` still does `start_x = x < 0 ? 0 : x`, byte-identical to
  // v0.2.2's, and `screen.hpp` still promises only the right edge. So the guard
  // is still unkillable, still undefended by the contract, and still kept. Four
  // minor versions of the dependency is the strongest evidence available that
  // this is de-facto stable and de-jure unpromised, which is precisely the
  // regime a local guard is for.
  const int word_x =
      std::max(0, screen.cols() - static_cast<int>(word.size()));
  screen.write_text(word_x, y, word, word_fg, bg);

  // One blank column between the two halves. `budget` goes negative on a screen
  // narrower than the word itself, which is correct: nothing fits, so nothing
  // is appended, and the word — the only thing that matters at the bottom tier
  // — keeps the row to itself.
  const int budget = word_x - 2;

  std::string left;
  for (const std::string& field : fields) {
    const std::string_view gap = left.empty() ? std::string_view{} : sep;
    if (static_cast<int>(left.size() + gap.size() + field.size()) > budget) {
      break;
    }
    left += gap;
    left += field;
  }
  screen.write_text(0, y, left, field_fg, bg);
}

}  // namespace glyphcade::hud
