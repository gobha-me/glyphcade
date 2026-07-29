#pragma once

// term-game — the arcade Shell: the ONE termforge::App in this process.
//
// It owns the terminal, the event loop, the selector UI and the game registry
// (and, from Epic 2, the audio engine). Games are *hosted*, never derived from
// App — see arcade/game.hpp and DESIGN.md, "One App, many Games".
//
// ⚠ THE ESCAPE RULE, which is the most fragile thing in this file.
// Shell::on_event NEVER chains to termforge::App::on_event. The base default
// quits on Escape; inside a game Escape must mean "back to the menu", and there
// is no way to express that while still calling the base — the base would
// already have quit. So on_event is total: every event either has a Shell
// meaning or is dropped, and Ctrl+C is handled explicitly because it is no
// longer being inherited. Every termforge example ends its on_event with
// `App::on_event(ev);`, so someone will eventually "restore" that line;
// test/11selector goes red when they do.

#include <chrono>
#include <memory>
#include <string>

#include <termforge/core/app.hpp>
#include <termforge/widgets/dialogs.hpp>
#include <termforge/widgets/focus_ring.hpp>
#include <termforge/widgets/frame.hpp>
#include <termforge/widgets/label.hpp>
#include <termforge/widgets/list_widget.hpp>
#include <termforge/widgets/text_box.hpp>

#include <termgame/arcade/context.hpp>
#include <termgame/arcade/game.hpp>
#include <termgame/audio/engine.hpp>
#include <termgame/audio/sink.hpp>

namespace termgame {

// Not `final`: tests subclass it to re-expose App's protected screen() and to
// override the private virtual loop seams (now_steady/wait_readable/
// read_available) for a fake clock — the same trick, for the same reason, as
// the Probe in test/10render.
class Shell : public termforge::App {
 public:
  // Silent: a NullSink, which opens successfully and never pulls. Every
  // existing test and probe subclass constructs the Shell this way and is
  // unaffected by Epic 2.
  Shell();

  // ⚠ THE AUDIO INJECTION SEAM, and the reason gitea #13 came out the way it
  // did. The Shell owns an engine but does not choose its sink: src/bin hands
  // it a device, a test hands it a WavFileSink, and the RtAudio backend
  // therefore never has to be reachable from this library at all.
  explicit Shell(std::unique_ptr<audio::AudioSink> sink);

  enum class State { Selector, InGame, Paused };

  // The simulation rate handed to termforge's fixed-timestep accumulator.
  static constexpr int kTickHz = 60;

  // Below this the selector cannot be drawn at all; draw_too_small() takes over
  // rather than emitting negative-width Rects.
  static constexpr int kMinCols = 20;
  static constexpr int kMinRows = 8;

  // Narrower than this and the detail pane is dropped so the list keeps a
  // usable width.
  static constexpr int kDetailPaneMinCols = 48;

  [[nodiscard]] auto state() const noexcept -> State { return m_state; }
  [[nodiscard]] auto current_game() const noexcept -> const Game* {
    return m_game.get();
  }

  // The Epic 1 acceptance criterion "the registry lists every linked game",
  // expressed as something a test can read. ListWidget::item_count() is not
  // reachable through a private member.
  [[nodiscard]] auto selector_item_count() const noexcept -> std::size_t {
    return m_list.item_count();
  }
  [[nodiscard]] auto selector_index() const noexcept -> int {
    return m_list.selected();
  }

  // Read-only, for tests: play_count() records intent, so a binding assertion
  // works identically on a build that can make no sound.
  //
  // ⚠ Deliberately the SHELL's engine and not the Game's. Asserting through
  // here sidesteps the "never touch the Game* after dispatching a key it
  // consumes" trap structurally rather than by discipline — the Shell destroys
  // the game inside dispatch_event(), but the Shell itself is still very much
  // alive. See STATUS.md and test/15minesweeper-ui.
  [[nodiscard]] auto audio() const noexcept -> const audio::Engine& {
    return m_audio;
  }

  auto on_event(const termforge::Event& ev) -> void override;
  auto on_tick(std::chrono::duration<double> dt) -> void override;
  auto on_render(termforge::Screen& screen) -> void override;

 private:
  auto handle_selector_key(const termforge::Event& ev,
                           const termforge::KeyEvent& key) -> void;
  auto handle_in_game_key(const termforge::Event& ev,
                          const termforge::KeyEvent& key) -> void;
  auto enter_selected_game() -> void;
  auto request_to_menu() -> void;
  auto open_pause() -> void;
  auto apply_transitions() -> void;
  auto sync_capabilities() -> void;
  auto quit_app() -> void;

  auto rebuild_list() -> void;
  auto refresh_detail() -> void;
  auto draw_selector(termforge::Screen& screen) -> void;
  auto draw_too_small(termforge::Screen& screen) -> void;

  State m_state{State::Selector};
  audio::Engine m_audio;
  // Stashed at construction, emitted on the first frame. See sync_capabilities.
  std::string m_audio_notice;
  GameContext m_ctx;
  std::unique_ptr<Game> m_game;  // null unless InGame or Paused
  bool m_release_game{false};    // deferred destruction; see apply_transitions
  bool m_caps_synced{false};
  int m_detail_index{-1};  // the index the detail pane was last built for
  std::string m_notice;    // most recent ErrorEvent, shown in the footer

  // Selector widgets, direct members — laid out and drawn together every frame
  // in on_render, the shape termforge's examples/widgets.cpp uses.
  termforge::Label m_title;
  termforge::Frame m_list_frame;
  termforge::ListWidget m_list;
  termforge::Frame m_detail_frame;
  termforge::TextBox m_detail;
  termforge::FocusRing m_ring;

  // ⚠ A MEMBER, not a local. push_overlay stores a raw pointer and does not
  // own the widget; a dialog constructed on the stack of the function that
  // pushes it dangles the moment that function returns.
  termforge::ConfirmDialog m_pause{"Paused", ""};
};

}  // namespace termgame
