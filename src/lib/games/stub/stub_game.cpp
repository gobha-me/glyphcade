#include <termgame/games/stub/stub_game.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#include <termforge/widgets/theme.hpp>

namespace termgame {
namespace {

constexpr termforge::Rgb kMarker{0x00, 0xFF, 0x80};

// Two decimals, without <format> or a stringstream — this file is on the render
// path and the repo has no formatting dependency.
auto fixed2(double v) -> std::string {
  const long scaled = std::lround(v * 100.0);
  std::string s = std::to_string(scaled / 100);
  s += '.';
  const long frac = std::labs(scaled % 100);
  if (frac < 10) s += '0';
  s += std::to_string(frac);
  return s;
}

}  // namespace

auto StubGame::start(GameContext& ctx) -> void {
  // Storing the address is safe: the Shell owns exactly one GameContext for its
  // whole life and never recreates it per game (see arcade/context.hpp).
  //
  // Nothing is reset here, deliberately. This object was constructed moments
  // ago by the registry factory; freshness is structural, not a routine.
  m_ctx = &ctx;
  m_frame.set_style(ctx.border_style());
}

auto StubGame::tick(std::chrono::duration<double> dt) -> void {
  ++m_ticks;
  m_elapsed += dt;
  m_min_dt = std::min(m_min_dt, dt);
  m_x += m_dir * kSpeedCellsPerSec * dt.count();
  // Bounds are applied in draw(), where the screen width is known. tick() must
  // not need a Screen — that is what makes it drivable headlessly.
}

auto StubGame::on_event(const termforge::Event& ev) -> bool {
  const auto* k = std::get_if<termforge::KeyEvent>(&ev);
  if (k == nullptr || k->key != termforge::Key::Char) return false;

  if (k->ch == U'd' || k->ch == U'D') {
    m_done = true;  // the done() exit path
    return true;
  }
  if (k->ch == U'm' || k->ch == U'M') {
    // The quit_to_menu() exit path, called from INSIDE an event handler. That
    // placement is the point: it is the exact shape that a synchronous
    // callback design would turn into a use-after-free, so CI's ASan/UBSan
    // arms execute it every run.
    if (m_ctx != nullptr) m_ctx->quit_to_menu();
    return true;
  }
  // Everything else declines — notably Escape and 'p', so the Shell's
  // quit-to-menu and pause are exercised rather than shadowed.
  return false;
}

auto StubGame::draw(termforge::Screen& screen) -> void {
  const int w = screen.cols();
  const int h = screen.rows();
  const auto fg = termforge::theme::kFg;
  const auto bg = termforge::theme::kBg;

  m_frame.set_style(m_ctx != nullptr ? m_ctx->border_style()
                                     : termforge::BorderStyle::Ascii);
  m_frame.set_geometry({0, 0, w, h});
  m_frame.draw(screen);

  const termforge::Rect in = m_frame.content_rect();
  if (in.w <= 0 || in.h <= 0) return;

  screen.write_text(in.x, in.y,
                    "ticks: " + std::to_string(m_ticks) +
                        "   elapsed: " + fixed2(m_elapsed.count()) + "s",
                    fg, bg);

  // Bounce inside the content rect. Done here rather than in tick() so tick()
  // stays Screen-free; the cost is that the turnaround point follows a resize,
  // which for a diagnostic is a feature.
  if (in.w > 1) {
    const auto span = static_cast<double>(in.w - 1);
    if (m_x < 0.0) {
      m_x = 0.0;
      m_dir = 1.0;
    } else if (m_x > span) {
      m_x = span;
      m_dir = -1.0;
    }
    const int row = in.y + in.h / 2;
    screen.write_text(in.x + static_cast<int>(std::lround(m_x)), row, "#",
                      kMarker, bg);
  }

  if (in.h >= 3) {
    screen.write_text(in.x, in.y + in.h - 1,
                      "Esc menu  P pause  D finish  M quit_to_menu",
                      termforge::theme::kDim, bg);
  }
}

}  // namespace termgame
