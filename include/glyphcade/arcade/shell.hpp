#pragma once

// glyphcade — the arcade Shell: the ONE termforge::App in this process.
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
#include <filesystem>
#include <memory>
#include <string>

#include <termforge/core/app.hpp>
#include <termforge/widgets/dialogs.hpp>
#include <termforge/widgets/focus_ring.hpp>
#include <termforge/widgets/frame.hpp>
#include <termforge/widgets/label.hpp>
#include <termforge/widgets/list_widget.hpp>
#include <termforge/widgets/text_box.hpp>

#include <glyphcade/arcade/context.hpp>
#include <glyphcade/arcade/game.hpp>
#include <glyphcade/arcade/scores.hpp>
#include <glyphcade/audio/engine.hpp>
#include <glyphcade/audio/sink.hpp>

namespace glyphcade {

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

  // ⚠ THE SCORES INJECTION SEAM, and the same argument one layer over: the Shell
  // owns the store but does not choose where it lives. src/bin/main.cpp resolves
  // $XDG_DATA_HOME; a test passes a temp file; an EMPTY path is memory-only.
  //
  // Empty is what the two constructors above delegate with, so every existing
  // test and probe subclass keeps a Shell that cannot touch the filesystem —
  // which is a structural guarantee, not a convention. Nothing in this library
  // reads an environment variable, so nothing here can name a real file.
  Shell(std::unique_ptr<audio::AudioSink> sink, std::filesystem::path scores);

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

  // The list pane's own floor, in the one place it is not derived from a
  // fraction of the screen. Only reachable while the detail pane is up, i.e.
  // from 48 columns, and it BITES from 48 to 60 (48 * 2 / 5 == 19) — so in
  // that band the detail pane gets w - 24 rather than the three fifths the
  // fraction implies. It was a bare literal for six releases; naming it is not
  // a behaviour change.
  static constexpr int kListPaneMinCols = 24;

  // ⚠ THE SUITE'S FIRST CEILING, and gitea #42 is the whole argument for it.
  // Every game is a fixed rectangle centred in whatever it is given; the
  // selector was the one screen that grew without bound, so a 300-column
  // terminal got a full-width menu and then, one keystroke later, a 58x20 box
  // floating in the middle of it. The same binary disagreeing with itself.
  //
  // Past this the list+detail body stops widening and is CENTRED, which is
  // exactly what a game does. 120 splits as a 48-column list and a 72-column
  // detail pane, and 72 is about where prose stops getting easier to read.
  //
  // ⚠ COLUMNS ONLY, and the asymmetry is deliberate rather than an omission.
  // Rows past the floor are CAPACITY — more roster visible in a scrolling
  // list, and for the game gitea #43 is about, more of a pile — while columns
  // past a measure only stretch a line nobody wanted stretched. There is no
  // kSelectorMaxRows and there should not be one.
  //
  // ⚠ Below it nothing changes at all, which is why the number is 120 and not
  // 100: test/11selector's widest existing sweep is 120 columns, so those
  // cases stay byte-identical and the new wide case is the only thing in the
  // suite that can see this constant work.
  static constexpr int kSelectorMaxCols = 120;

  // ⚠ The ceiling is now what `with_detail` and `list_w` are keyed off, so it
  // silently owns both floors. Drop it below kDetailPaneMinCols and the detail
  // pane — and with it the `size:` line this whole change exists to surface —
  // disappears at EVERY width, on a 300-column terminal, with no compile error
  // and no red test. Drop it into the 48..59 band and `list_w` clamps to
  // kListPaneMinCols everywhere, so the documented "120 splits as 48 + 72"
  // quietly stops being true. Both are stated here instead of discovered.
  static_assert(kSelectorMaxCols >= kDetailPaneMinCols,
                "the selector ceiling must leave room for the detail pane, or "
                "the pane is dropped at every width");
  static_assert(kSelectorMaxCols * 2 / 5 >= kListPaneMinCols,
                "at the ceiling the list's fraction must beat its floor, or "
                "the two-fifths split is not what happens");

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

  // Read-only, for tests, and the SHELL's store for the same reason audio() is
  // the Shell's engine: it outlives every game, so an assertion through here is
  // safe on the exact paths where the Game* has already been destroyed. It is
  // also the only way to read a record that no status row can show — an
  // unclamped Minesweeper time above 999 seconds, say.
  [[nodiscard]] auto scores() const noexcept -> const scores::Store& {
    return m_scores_store;
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

  // Rebuild m_footer_warning if and only if the selection or the terminal size
  // has changed since it was last built. See the cache members below for why
  // this is not optional.
  auto refresh_footer_warning(int cols, int rows) -> void;
  auto draw_selector(termforge::Screen& screen) -> void;
  auto draw_too_small(termforge::Screen& screen) -> void;

  State m_state{State::Selector};
  audio::Engine m_audio;
  // Stashed at construction, emitted on the first frame. See sync_capabilities.
  std::string m_audio_notice;
  // ⚠ BY VALUE, and that is the teardown hook: ~Store flushes, so Ctrl+C and an
  // unwinding exception both persist the session without this class needing a
  // destructor of its own. A unique_ptr would work and buy nothing; a store
  // handed in from outside could not be trusted to outlive m_ctx's Recorder.
  scores::Store m_scores_store;
  std::string m_scores_notice;  // same shape as m_audio_notice, same timing
  GameContext m_ctx;
  std::unique_ptr<Game> m_game;  // null unless InGame or Paused
  bool m_release_game{false};    // deferred destruction; see apply_transitions
  bool m_caps_synced{false};
  int m_detail_index{-1};  // the index the detail pane was last built for

  // The footer's size warning, cached on everything it depends on.
  //
  // ⚠ THIS CACHE IS NOT A MICRO-OPTIMISATION, and it was added because leaving
  // it out broke a test in another suite. The warning was rebuilt every frame —
  // three std::strings constructed and concatenated, on the render path, for an
  // answer that changes only when the selection or the terminal does. That is
  // small, but it is not free, and test/26snake-ui's "the board does not move
  // while the options screen is up" counts TICKS: test_run_frames does not
  // reset the tick clock, so wall time spent drawing selector frames becomes
  // ticks the Shell's accumulator delivers. Slower frames, more ticks, red
  // test — under load, on the TSan build, five runs in six, where main was
  // stable in all six. A render path that allocates per frame is measurable
  // from two suites away.
  int m_footer_index{-1};
  int m_footer_cols{-1};
  int m_footer_rows{-1};
  std::string m_footer_warning;
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
  //
  // ⚠ Being a member is also why its per-showing state is set at the push site
  // rather than here: the constructor may not touch driver(), so it cannot know
  // the border tier, and focus persists between showings. See open_pause().
  termforge::ConfirmDialog m_pause{"Paused", ""};
};

}  // namespace glyphcade
