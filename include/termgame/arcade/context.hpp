#pragma once

// term-game — GameContext: a game's ONLY channel to shared services.
//
// Games never see the App, the Terminal, the Screen's owner, or each other
// (AGENTS.md, "One App, many Games"). Everything shared arrives through here,
// which is what keeps a game from reaching sideways into the Shell and what
// makes a game testable by handing it a context and nothing else.
//
// What ships in Epic 1 is deliberately small: the probed capability tier, the
// border family the Shell picked from it, and quit_to_menu(). Two services
// DESIGN.md names are seams, not omissions, and are left unfilled on purpose:
//
//   * AUDIO — Epic 2 (gitea #3). The engine does not exist yet. Inventing its
//     handle here would pin an API before anything has consumed it, and the one
//     thing worse than a missing accessor is a wrong one that six games already
//     call.
//   * HIGH SCORES — deferred. No game produces a score yet, and a persistence
//     format chosen before there is anything to persist is a format that gets
//     migrated. Tracked separately on gitea.
//
// Both are additive: a new accessor on this class breaks no existing game.

#include <termforge/core/types.hpp>
#include <termforge/widgets/glyphs.hpp>

namespace termgame {

class GameContext {
 public:
  GameContext() = default;

  // The probed terminal capabilities. Zero-initialised — i.e. the floor — until
  // the Shell's first frame, because the driver that answers this question does
  // not exist before then. A game only ever sees this after start().
  [[nodiscard]] auto capabilities() const noexcept
      -> const termforge::Capabilities& {
    return m_caps;
  }

  // The border family every game should draw its chrome with.
  //
  // There is no Capabilities bit for "can render box drawing" (termforge #16),
  // so somebody has to decide. The Shell decides once and every game agrees
  // with it through this accessor, rather than each game guessing separately
  // and the suite looking like six different programs on the same terminal.
  //
  // Defaults to Ascii: the floor is the right assumption to hold until the
  // driver has actually answered.
  [[nodiscard]] auto border_style() const noexcept -> termforge::BorderStyle {
    return m_border;
  }

  // Ask the Shell to return to the selector.
  //
  // ⚠ DEFERRED, never immediate, and that deferral is the whole point.
  // Returning to the menu destroys the Game. If this called back into the Shell
  // synchronously, a game calling it from inside its own tick() or on_event()
  // would be destroyed while its stack frame was still live — a use-after-free
  // that reproduces only on the exact path a game author is most likely to
  // write. The Shell polls this once per frame, at one place, after every game
  // entry point has unwound.
  //
  // Same hazard termforge's non-owning push_overlay() exists to avoid.
  auto quit_to_menu() noexcept -> void { m_quit_to_menu = true; }

  // ── Shell-side plumbing below. Not for games. ────────────────────────────
  [[nodiscard]] auto quit_to_menu_requested() const noexcept -> bool {
    return m_quit_to_menu;
  }
  auto clear_quit_to_menu() noexcept -> void { m_quit_to_menu = false; }
  auto set_capabilities(const termforge::Capabilities& caps) -> void {
    m_caps = caps;
  }
  auto set_border_style(termforge::BorderStyle style) noexcept -> void {
    m_border = style;
  }

 private:
  termforge::Capabilities m_caps{};
  termforge::BorderStyle m_border{termforge::BorderStyle::Ascii};
  bool m_quit_to_menu{false};
};

}  // namespace termgame
