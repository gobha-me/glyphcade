#include <termgame/arcade/boot_app.hpp>

#include <string>

#include <termgame/build_info.hpp>

namespace termgame {

namespace {

constexpr termforge::Rgb kAccent{0x00, 0xFF, 0x80};
constexpr termforge::Rgb kHint{0x80, 0x80, 0x80};

}  // namespace

BootApp::BootApp() : m_title("term-game " + std::string(version_string())) {
  m_title.set_align(termforge::Label::Align::Center);
  m_title.set_colors(kAccent, termforge::theme::kBg);
}

auto BootApp::on_render(termforge::Screen& screen) -> void {
  screen.clear();

  const int w = screen.cols();
  const int h = screen.rows();

  // The Label spans the full width and centers itself within that rect, so it
  // stays centered across a resize without recomputing an x here.
  m_title.set_geometry(termforge::Rect{0, h / 2, w, 1});
  m_title.draw(screen);

  // The audio decision, made visible. build_has_audio() reports how CMake
  // configured this build, not whether a device exists — see build_info.hpp.
  const std::string audio =
      std::string("audio: ") + (build_has_audio() ? "on" : "off");
  screen.write_text(0, h - 2, audio, kHint, termforge::theme::kBg);

  // ESC and Ctrl+C are already served by App's default on_event, which is why
  // BootApp does not override it. Do not add an override just to quit.
  screen.write_text(0, h - 1, "Press ESC to quit", kHint,
                    termforge::theme::kBg);
}

}  // namespace termgame
