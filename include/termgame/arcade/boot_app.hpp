#pragma once

// term-game — BootApp: the Epic 0 placeholder.
//
// ⚠ This is NOT the Shell. The Shell (Epic 1) is the real termforge::App — it
// owns the terminal, the event loop, the audio engine, the game registry and
// the selector UI, per DESIGN.md. BootApp is named apart from it deliberately,
// so the two cannot collide and so nobody reads this file as a half-built
// version of that one. It proves the scaffold: that termforge is linked, that
// the alternate screen opens and closes, and that the build's audio decision
// reached the compiler. Nothing more.

#include <termforge/core/app.hpp>
#include <termforge/widgets/label.hpp>

namespace termgame {

// Not `final`: test/10render subclasses it to re-expose App's protected
// screen(), which is how the placeholder is asserted without a terminal.
class BootApp : public termforge::App {
 public:
  BootApp();

  auto on_render(termforge::Screen& screen) -> void override;

 private:
  // A Label rather than a bare Screen::write_text, on purpose: it pulls in a
  // second termforge translation unit (widgets/label.cpp beside core/screen.cpp
  // and core/app.cpp), which turns every build into a real link test of the
  // export. A broken include path fails to compile; a half-exported archive
  // fails to link. Same trick termforge's own tools/consume fixture uses.
  termforge::Label m_title;
};

}  // namespace termgame
