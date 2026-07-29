#pragma once

// term-game — GameContext: a game's ONLY channel to shared services.
//
// Games never see the App, the Terminal, the Screen's owner, or each other
// (AGENTS.md, "One App, many Games"). Everything shared arrives through here,
// which is what keeps a game from reaching sideways into the Shell and what
// makes a game testable by handing it a context and nothing else.
//
// Epic 1 shipped three things: the probed capability tier, the border family
// the Shell picked from it, and quit_to_menu(). Epic 2 (gitea #3) added audio()
// — the seam this file reserved, filled in exactly as promised, additively and
// without disturbing a single existing game.
//
// One service DESIGN.md names is still a seam rather than an omission:
//
//   * HIGH SCORES — deferred. No game produces a score yet, and a persistence
//     format chosen before there is anything to persist is a format that gets
//     migrated. Tracked separately on gitea (#14).

#include <termforge/core/types.hpp>
#include <termforge/widgets/glyphs.hpp>

#include <termgame/audio/engine.hpp>

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

  // Sound effects.
  //
  // ⚠ NEVER null, and that is the whole design. A context with no engine hands
  // back an empty Player whose play() is a no-op, so a game writes
  //
  //     ctx.audio().play(audio::SfxId::Reveal);
  //
  // with no null check, no has_audio(), and no #ifdef anywhere. "This build
  // makes no sound" stays a property of the ENGINE rather than a shape all
  // dozen call sites have to carry — the same choice border_style() above
  // already makes, where the default is the floor rather than an optional.
  //
  // A Player exposes only play(). Opening, closing and pumping the engine
  // belong to the Shell that owns it, and a game must not be able to reach
  // them.
  [[nodiscard]] auto audio() const noexcept -> const audio::Player& {
    return m_audio;
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
  auto set_audio(audio::Engine* engine) noexcept -> void {
    m_audio = audio::Player{engine};
  }

 private:
  termforge::Capabilities m_caps{};
  termforge::BorderStyle m_border{termforge::BorderStyle::Ascii};
  bool m_quit_to_menu{false};
  // Empty until the Shell sets it, which is what makes a bare GameContext in a
  // test silent rather than a crash.
  audio::Player m_audio{};
};

}  // namespace termgame
