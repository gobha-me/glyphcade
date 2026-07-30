#include <termgame/arcade/shell.hpp>

#include <algorithm>
#include <string>
#include <variant>

#include <termforge/widgets/theme.hpp>

#include <termgame/arcade/registry.hpp>
#include <termgame/build_info.hpp>

namespace termgame {
namespace {

constexpr termforge::Rgb kAccent{0x00, 0xFF, 0x80};

}  // namespace

Shell::Shell() : Shell(std::make_unique<audio::NullSink>()) {}

Shell::Shell(std::unique_ptr<audio::AudioSink> sink)
    : Shell(std::move(sink), std::filesystem::path{}) {}

Shell::Shell(std::unique_ptr<audio::AudioSink> sink,
             std::filesystem::path scores)
    : m_scores_store(std::move(scores)),
      m_title("term-game " + std::string(version_string())) {
  // ⚠ Nothing here may touch driver(), terminal() or screen(). App::m_driver is
  // a null unique_ptr until setup() (or test_run_frames) builds one, and
  // driver() dereferences it — a capability query in this constructor is a null
  // dereference in every configuration, including the ones no test runs.
  // Capabilities are synced lazily on the first frame; see sync_capabilities().

  // The fixed timestep is termforge's, not ours (#59, shipped in v0.1.8). The
  // accumulator, the delta clamp and the per-frame tick bound all live in
  // App::tick_step; the Shell's entire contribution is choosing the rate.
  // DESIGN.md used to describe hand-rolling this — do not reintroduce it.
  //
  // max_tick_dt is left at App::kDefaultMaxTickDt (250 ms), which bounds a
  // frame at ceil(0.25 * 60) = 15 game ticks however long the frame took. That
  // is the "a breakpoint or a laptop suspend cannot deliver one enormous dt"
  // rule in AGENTS.md, and test/13tick pins it at this layer.
  set_tick_hz(kTickHz);

  // ⚠ The engine opens HERE, not in setup(), and that is upstream's constraint
  // rather than a preference: termforge::App::setup and ::teardown are private
  // and non-virtual, so a subclass has no hook to bring up its own resources
  // inside the loop's lifetime. The consequence is that a device failure
  // happens before any terminal exists to report it on — hence the stash below,
  // drained on the first frame by sync_capabilities(), which exists for exactly
  // this class of "cannot be done in the constructor" problem.
  //
  // Filed upstream; see STATUS.md.
  // ⚠ The dash here is ASCII '-', and it must stay ASCII. This string reaches
  // m_notice and m_notice is painted on the selector's footer row, which
  // test/11selector sweeps cell by cell for any byte >= 0x80. It used to be an
  // em dash (U+2014), which meant a headless run that failed to open a device
  // would have turned that case red — a latent, machine-dependent failure that
  // only never fired because nothing in this container reaches a real device.
  // opened.error() is the sink's own text and is held to the same rule.
  if (auto opened = m_audio.open(std::move(sink)); !opened) {
    m_audio_notice = "audio: " + opened.error() + " - running silent";
  }
  m_ctx.set_audio(&m_audio);

  // The store's constructor already read the file (a read is not a terminal
  // touch, so it is allowed here); this only picks up what it found. Same stash
  // as the audio notice above and for the same reason — there is no screen yet.
  //
  // ⚠ A failure to LOAD is knowable now; a failure to WRITE is not, without a
  // probe write this constructor deliberately does not make. That is why there
  // are two report sites: here for load, apply_transitions() for flush.
  if (!m_scores_store.load_error().empty()) {
    m_scores_notice = m_scores_store.load_error();
  }
  m_ctx.set_scores(&m_scores_store);

  m_title.set_align(termforge::Label::Align::Center);
  m_title.set_colors(kAccent, termforge::theme::kBg);
  m_list_frame.set_title("Games");

  rebuild_list();

  // Enter and a left click both arrive here — ListWidget fires on_select for
  // each — so the two entry paths cannot drift apart.
  m_list.on_select([this](int, const std::string&) { enter_selected_game(); });

  m_ring.add(&m_list);

  m_pause.set_labels("Menu", "Resume");
  m_pause.set_text("Leave the game and return to the menu?");
  m_pause.on_close([this] { pop_overlay(); });
  m_pause.on_result([this](bool to_menu) {
    if (to_menu) {
      request_to_menu();
    } else {
      m_state = State::InGame;
    }
  });
}

// ── Input ───────────────────────────────────────────────────────────────────

auto Shell::on_event(const termforge::Event& ev) -> void {
  // ⚠ This function NEVER calls App::on_event. See the escape rule at the top
  // of arcade/shell.hpp. It is total by construction: everything either has a
  // meaning here or is deliberately dropped.

  // Ctrl+C is the break-glass, and it is ours now that the base handler is not
  // in the chain. App::dispatch_event routes it past overlays on purpose (raw
  // mode turned SIGINT into an ordinary key), so this must also work while
  // paused — otherwise a modal with no wired close path is unkillable from its
  // own terminal.
  if (const auto* key = std::get_if<termforge::KeyEvent>(&ev)) {
    if (key->ctrl && (key->ch == U'c' || key->ch == U'C')) {
      quit_app();
      return;
    }
  }

  if (std::holds_alternative<termforge::ResizeEvent>(ev)) {
    // Nothing else to do: every layout is recomputed from screen.cols()/rows()
    // in on_render, and Dialog::draw re-centres itself. frame_step has already
    // resized the Screen before this dispatch.
    if (m_game) m_game->on_event(ev);
    return;
  }

  if (const auto* err = std::get_if<termforge::ErrorEvent>(&ev)) {
    // Degradation is an event, never a silent downgrade (AGENTS.md). The
    // selector footer is where it surfaces.
    m_notice = err->message;
    if (m_game) m_game->on_event(ev);
    return;
  }

  // Paused: nothing below is reachable. App::dispatch_event hands every
  // remaining event to the top overlay and ignores its return value, so "pause
  // suspends the game's input" costs no code at all.

  if (const auto* mouse = std::get_if<termforge::MouseEvent>(&ev)) {
    if (m_state == State::Selector) {
      // Same edge detection as the key path, and since termforge v0.2.0 the
      // CLICK is the only gesture it can fire on.
      //
      // ⚠ The wheel used to reach here too: ListWidget answered a wheel event
      // with set_selected(selected ± 3) — moving the selection without firing
      // on_select, which is exactly the shape edge detection exists for. #35
      // unified that away. The wheel now scrolls a view offset and leaves the
      // selection alone, so `before` cannot change on a wheel event and
      // MenuMove no longer sounds for one. That is the honest outcome, not a
      // regression to patch: nothing moved, so nothing should say it did.
      //
      // We adopted upstream's convention rather than rebuilding the old one
      // here, which would have meant intercepting the wheel before route_mouse
      // and diverging from the framework on purpose — the workaround shape
      // gitea #16 and #17 spent two issues deleting. test/11selector holds both
      // halves: the selection does not move, and no sound plays.
      //
      // A click still moves the selection and then fires on_select, and the
      // State guard is what makes that case emit only MenuSelect.
      const int before = m_list.selected();
      if (mouse->pressed) m_ring.focus_at(mouse->x, mouse->y);
      route_mouse(*mouse, {&m_list});
      if (m_state == State::Selector && m_list.selected() != before) {
        m_audio.play(audio::SfxId::MenuMove);
      }
      return;
    }
    if (m_game) {
      // Verbatim, in screen coordinates: a running game owns the whole Screen,
      // so there is no offset arithmetic to get wrong. The return value is
      // ignored because there is no Shell-level mouse gesture to fall back to.
      m_game->on_event(ev);
      apply_transitions();
    }
    return;
  }

  if (std::holds_alternative<termforge::PasteEvent>(ev)) {
    if (m_state == State::InGame && m_game) {
      m_game->on_event(ev);
      apply_transitions();
    }
    return;
  }

  const auto* key = std::get_if<termforge::KeyEvent>(&ev);
  if (key == nullptr) return;

  switch (m_state) {
    case State::Selector:
      handle_selector_key(ev, *key);
      return;
    case State::InGame:
      handle_in_game_key(ev, *key);
      return;
    case State::Paused:
      return;  // see the note above; only Ctrl+C gets here
  }
}

auto Shell::handle_selector_key(const termforge::Event& ev,
                                const termforge::KeyEvent& key) -> void {
  const int before = m_list.selected();

  // The ring first: ListWidget owns Up/Down/PageUp/PageDown/Home/End/Enter, and
  // Enter reaches enter_selected_game() through its on_select callback.
  if (m_ring.handle_key(ev)) {
    // ⚠ EDGE-DETECTED on the selection, not bound to keys. ListWidget owns
    // Up/Down/PageUp/PageDown/Home/End and exposes no per-key hook, so binding
    // the arrow keys here would sound on a Down that clamped and moved nothing,
    // and stay silent on Home/End. "Did the selection move" is the only honest
    // question.
    //
    // ⚠ And the state check is not belt-and-braces. Enter also goes through
    // m_ring.handle_key(), and its on_select fires enter_selected_game() INSIDE
    // that call — so by the time control returns here the Shell may already be
    // InGame, and without the guard entering a game would emit MenuMove as well
    // as MenuSelect.
    if (m_state == State::Selector && m_list.selected() != before) {
      m_audio.play(audio::SfxId::MenuMove);
    }
    return;
  }

  // Escape quits the app HERE and only here. The paired case is in
  // handle_in_game_key, where the same key means "back to the menu".
  if (key.key == termforge::Key::Escape) quit_app();
}

auto Shell::handle_in_game_key(const termforge::Event& ev,
                               const termforge::KeyEvent& key) -> void {
  // The game gets first refusal on everything, so a game that needs 'p' for
  // something can have it — at the price of writing its own pause.
  if (m_game && m_game->on_event(ev)) {
    apply_transitions();
    return;
  }

  // Shell-level keys, on anything the game declined. This is how every game
  // gets pause and quit-to-menu without implementing either.
  if (key.key == termforge::Key::Escape) {
    request_to_menu();
    return;
  }
  if (key.key == termforge::Key::Char && (key.ch == U'p' || key.ch == U'P')) {
    open_pause();
    return;
  }
  apply_transitions();
}

// ── Simulation ──────────────────────────────────────────────────────────────

auto Shell::on_tick(std::chrono::duration<double> dt) -> void {
  // ⚠ ABOVE the pause gate, and not test scaffolding.
  //
  // An offline sink has no audio thread, so the UI thread is both producer and
  // consumer for it — still one of each, still a legal SPSC pairing. Without
  // this call a WavFileSink handed to the Shell would produce an empty file.
  // No-op for a device sink, which pulls itself, and for a NullSink, which is
  // never pulled at all.
  //
  // Above the gate because a sound already in flight must finish while the
  // pause dialog is up, and because the selector needs audio too.
  //
  // 48000/60 == 800 exactly, so under test/13tick's fake clock this renders a
  // whole number of frames per tick and the resulting wav is deterministic.
  m_audio.pump(dt);

  // The framework keeps ticking while an overlay is up, by design — only the
  // app knows whether its simulation is a game (pause it) or a progress
  // animation (keep going). So the pause gate is ours.
  //
  // modal() as well as the state, so that a *future* overlay — an error toast,
  // a confirm-quit — pauses the game too rather than silently letting it run
  // underneath.
  if (m_state != State::InGame || modal() || !m_game) return;

  // dt is forwarded verbatim. termforge's accumulator already guarantees the
  // constant 1/kTickHz; scaling it here would be inventing a second timestep.
  m_game->tick(dt);

  // Safe: the game's tick has unwound, so releasing it here cannot destroy an
  // object with a live frame on the stack.
  apply_transitions();
}

// ── Frame ───────────────────────────────────────────────────────────────────

auto Shell::on_render(termforge::Screen& screen) -> void {
  // THE point at which a pending game destruction actually happens. It runs
  // exactly once per frame — unlike on_tick, which under a fixed timestep may
  // run zero times on a fast frame — and no Game entry point is anywhere on the
  // stack here. The calls in on_tick and the key handlers only reduce latency;
  // this one is the guarantee.
  apply_transitions();
  sync_capabilities();

  screen.clear();  // the loop does not clear for us

  if (screen.cols() < kMinCols || screen.rows() < kMinRows) {
    draw_too_small(screen);
    return;
  }

  switch (m_state) {
    case State::Selector:
      draw_selector(screen);
      break;
    case State::InGame:
    case State::Paused:
      // Paused draws exactly as InGame. App::frame_step then dims this frame
      // and draws the dialog over it, and restore_backdrop undoes the damage
      // after present() — so "the game freezes behind a dimmed pause dialog"
      // costs nothing here.
      if (m_game) m_game->draw(screen);
      break;
  }
  // No `default:`. With -Wswitch and CI's -Werror, a fourth State becomes a
  // compile error rather than a silently blank screen.
}

// ── Transitions ─────────────────────────────────────────────────────────────

auto Shell::enter_selected_game() -> void {
  const auto games = all_games();
  const int index = m_list.selected();
  if (index < 0 || static_cast<std::size_t>(index) >= games.size()) return;

  // After the range guard, so a click on an empty list is silent as well as
  // harmless. Enter and a left click both arrive here — ListWidget fires
  // on_select for each — so the two entry paths cannot drift apart.
  m_audio.play(audio::SfxId::MenuSelect);

  // A NEW object, from the registry's factory. Freshness is structural: there
  // is no previous instance to have leaked a field. See registry.hpp.
  m_game = games[static_cast<std::size_t>(index)].make();
  m_ctx.clear_quit_to_menu();

  // Re-arming the same rate is a no-op except for the part that matters:
  // set_tick_hz clears the accumulator, so a game starts with no residue banked
  // while the menu was up rather than inheriting up to one period of it.
  set_tick_hz(kTickHz);

  m_state = State::InGame;
  m_notice.clear();
  m_game->start(m_ctx);
}

auto Shell::request_to_menu() -> void {
  if (!m_game) return;
  // The STATE flips immediately, so input routing changes at once — the rest of
  // this input batch must not reach a game that is on its way out. Only the
  // OWNERSHIP release is deferred, to the single point in on_render.
  m_state = State::Selector;
  m_release_game = true;
}

auto Shell::open_pause() -> void {
  if (m_state != State::InGame) return;
  m_state = State::Paused;
  // Every showing, not once in the constructor: Dialog focus persists across
  // showings, so without this the second pause opens with "Menu" focused and
  // Enter drops the player out of the game they just paused.
  m_pause.set_default(false);
  push_overlay(m_pause);
}

auto Shell::apply_transitions() -> void {
  // done() is polled here and nowhere else.
  if (m_game && m_state != State::Selector &&
      (m_game->done() || m_ctx.quit_to_menu_requested())) {
    request_to_menu();
  }
  if (!m_release_game) return;

  m_release_game = false;
  clear_overlays();  // we may be leaving from Paused
  m_game.reset();
  m_ctx.clear_quit_to_menu();
  m_state = State::Selector;
  refresh_detail();

  // ⚠ THE ONLY REPORTABLE FLUSH, and this is the frame to do it on: exactly one
  // per game exit whichever of the three callers got us here, and we are on our
  // way back to the selector, which is the one screen with a footer to print a
  // failure in. A failure reported here lands on the very next frame drawn.
  //
  // ⚠ Once per game exit, NOT once per improvement. A flush per record would be
  // a write syscall on every 2048 move, which is precisely what "no syscalls on
  // the frame path" exists to prevent — and apply_transitions() is itself
  // reachable from on_tick(), so this write can land inside a tick. One write on
  // a frame already doing clear_overlays() and refresh_detail() is affordable;
  // sixty a second is not. The trade is that a SIGKILL loses the current run's
  // records, which is written down in STATUS.md rather than hidden here.
  if (auto written = m_scores_store.flush(); !written) {
    on_event(termforge::Event{termforge::ErrorEvent{
        termforge::Severity::Warning, "scores", written.error()}});
  }
}

// One line, and it stays a named function anyway: this is the single place
// Escape-in-the-selector and Ctrl+C both funnel through, and the escape rule at
// the top of shell.hpp is entirely about that routing. Inlining quit() at the
// two call sites would scatter the decision that rule protects.
auto Shell::quit_app() -> void { quit(); }

auto Shell::sync_capabilities() -> void {
  if (m_caps_synced) return;
  m_caps_synced = true;

  const termforge::Capabilities caps = driver().capabilities();
  m_ctx.set_capabilities(caps);

  // ⚠ THE ORDER OF THE THREE DRAINS BELOW IS LOAD-BEARING, because m_notice
  // keeps only the most recent message (see on_event) — so the LAST one to fire
  // is the one a player sees. The rule is least-urgent first, and the only part
  // of it that is a contract is that the COLOUR notice goes last: it is what the
  // AGENTS.md pty recipe checks for, and it is the one that describes what the
  // whole screen will look like for the rest of the session.
  //
  // ⚠ It is NOT test/11selector that pins the colour string — that test has no
  // notice-text assertion at all. What it does have is a sweep of every cell for
  // any byte >= 0x80, which the footer row is inside, so what it pins is that
  // whatever survives here is 7-bit. The automated check on the ordering itself
  // is test/24scores, which is only possible because a bad-version scores file
  // is a deterministic way to make a second notice fire.
  //
  // scores before audio is a judgement call rather than a contract: silence is
  // more immediately noticeable to a player than a score file that will not
  // save, so audio gets to outrank it.
  if (!m_scores_notice.empty()) {
    on_event(termforge::Event{termforge::ErrorEvent{
        termforge::Severity::Warning, "scores", m_scores_notice}});
    m_scores_notice.clear();
  }

  // Degradation is an event, never a silent downgrade (AGENTS.md) — so a device
  // we asked for and could not get is reported, drained here because the
  // constructor had no terminal to report it on.
  //
  // ⚠ Nothing is reported when the build simply has no RtAudio backend. There
  // was never an audio path to fall back FROM, build_has_audio() already says
  // so, and a permanent footer line on every CI and no-audio run is noise
  // rather than a degradation event. Likewise nothing is reported for a dropped
  // command or a stolen voice — those are counters, not events.
  if (!m_audio_notice.empty()) {
    on_event(termforge::Event{termforge::ErrorEvent{
        termforge::Severity::Warning, "audio", m_audio_notice}});
    m_audio_notice.clear();
  }

  // There is NO capability bit for "can render box drawing" (termforge #16), so
  // this is a heuristic and is labelled as one: a driver reporting no colour at
  // all is the fallback tier, i.e. a terminal we should assume has no box
  // drawing or emoji font either.
  const bool floor = caps.color_levels == 0 && !caps.truecolor;
  m_ctx.set_border_style(floor ? termforge::BorderStyle::Ascii
                               : termforge::BorderStyle::Rounded);
  if (floor) {
    // Degradation is an event (AGENTS.md). termforge gives an app no way to
    // inject into its own input queue, so this goes through our own handler —
    // which is what puts it in the footer.
    on_event(termforge::Event{termforge::ErrorEvent{
        termforge::Severity::Info, "shell",
        "no colour capability: ASCII border tier"}});
  }

  rebuild_list();  // item text depends on the tier; see rebuild_list()
}

// ── Selector ────────────────────────────────────────────────────────────────

auto Shell::rebuild_list() -> void {
  // ⚠ Built here, never per frame. ListWidget::set_items resets scroll_offset
  // to 0, so rebuilding every frame makes a list longer than its pane
  // permanently unscrollable.
  const int keep = m_list.selected();
  std::vector<std::string> items;
  const bool ascii = m_ctx.border_style() == termforge::BorderStyle::Ascii;

  for (const auto& entry : all_games()) {
    std::string row;
    // At the bottom tier, two spaces instead of the icon: that terminal
    // probably has no emoji font, and a missing-glyph box is worse than a gap.
    if (!ascii && !entry.meta.icon.empty()) {
      row += std::string(entry.meta.icon);
    } else {
      row += std::string(static_cast<std::size_t>(kIconCols), ' ');
    }
    row += "  ";
    row += std::string(entry.meta.title);
    if (!entry.meta.tag.empty()) {
      row += "   ";
      row += std::string(entry.meta.tag);
    }
    items.push_back(std::move(row));
  }

  m_list.set_items(std::move(items));
  if (keep >= 0) m_list.set_selected(keep);  // set_selected clamps
  m_detail_index = -1;                       // force a detail rebuild
}

auto Shell::refresh_detail() -> void {
  const int index = m_list.selected();
  if (index == m_detail_index) return;
  m_detail_index = index;

  m_detail.clear();
  const auto games = all_games();
  if (index < 0 || static_cast<std::size_t>(index) >= games.size()) return;

  const GameMeta& meta = games[static_cast<std::size_t>(index)].meta;
  m_detail_frame.set_title(std::string(meta.title));
  m_detail.append(std::string(meta.description));
  m_detail.append("");
  m_detail.append("tag:  " + std::string(meta.tag));
  m_detail.append("slug: " + std::string(meta.slug));
}

auto Shell::draw_selector(termforge::Screen& screen) -> void {
  refresh_detail();

  const int w = screen.cols();
  const int h = screen.rows();
  const auto style = m_ctx.border_style();
  const bool ascii = style == termforge::BorderStyle::Ascii;

  m_title.set_geometry({0, 0, w, 1});
  m_title.draw(screen);

  // Rows 1 .. h-3 are the panes; h-2 is the notice, h-1 the hints.
  const int body_y = 1;
  const int body_h = h - 3;

  const bool with_detail = w >= kDetailPaneMinCols;
  const int list_w = with_detail ? std::max(24, w * 2 / 5) : w;

  m_list_frame.set_style(style);
  m_list_frame.set_geometry({0, body_y, list_w, body_h});
  m_list_frame.draw(screen);

  const termforge::Rect inner = m_list_frame.content_rect();
  if (inner.w > 0 && inner.h > 0) {
    // ⚠ set_style, and it is load-bearing TWICE over — not symmetry with the
    // frame above.
    //
    // ListWidget picks its marker from mark_glyphs(style), and ONLY
    // BorderStyle::Ascii yields '>'; every other family yields '▸' (U+25B8).
    // The default is BorderStyle::Single. So dropping this line puts three
    // bytes of UTF-8 on a terminal we have already concluded cannot draw a box
    // — the bottom tier this repo promises always works. It is silent: the
    // widget renders, the layout is identical, and only the glyph is wrong.
    // test/11selector asserts the whole selector screen is 7-bit for this.
    //
    // The second reason arrived with termforge v0.2.1 (#21): ListWidget now
    // paints a one-column scrollbar when its content overflows, and the strip
    // reads its track and thumb from scrollbar_glyphs(style) off this SAME
    // enum — '|'/'#' under Ascii, '│'/'█' under every other family. So the
    // exact failure above has a second entrance, and the 7-bit case cannot see
    // it yet: two roster entries never overflow the pane, so no scrollbar is
    // drawn at any legal size. Whoever adds the fourth game inherits the
    // coverage. Do not read "the test passes without this line" as evidence.
    m_list.set_style(style);

    // The FULL content rect, gutter included. Until termforge v0.1.11 this
    // shrank by two columns and the selector hand-drew "> " into the gap,
    // because ListWidget's only selection affordance was a theme inversion and
    // FallbackDriver discards colour — so on the bottom tier the selected row
    // was byte-identical to every other one. #72 moved that upstream: the
    // marker now lives in a two-column gutter ListWidget reserves on every row.
    //
    // Handing back the two columns costs the text nothing, which is worth
    // knowing before anyone "fixes" the width: the widget computes
    // text_x = rect.x + gutter_cols() and max_w = rect.w - gutter - 1, so at
    // 60x20 the item text starts at x=3 with 19 columns either way. Identical
    // before and after — no layout moved.
    //
    // What did change is that the gutter is now INSIDE rect(). The workaround
    // documented the opposite as a known limitation: a click on the marker
    // landed outside m_list.rect() and did not select. It selects now.
    m_list.set_geometry(inner);
    m_list.draw(screen);
  }

  if (with_detail) {
    m_detail_frame.set_style(style);
    m_detail_frame.set_geometry({list_w, body_y, w - list_w, body_h});
    m_detail_frame.draw(screen);
    const termforge::Rect dinner = m_detail_frame.content_rect();
    if (dinner.w > 0 && dinner.h > 0) {
      // Not in the focus ring and not in route_mouse, on purpose: it is a
      // display surface here, and a focusable one would eat the PageUp/PageDown
      // and wheel events the list needs.
      m_detail.set_geometry(dinner);
      m_detail.draw(screen);
    }
  }

  if (!m_notice.empty()) {
    screen.write_text(0, h - 2, m_notice, termforge::theme::kDim,
                      termforge::theme::kBg);
  }
  screen.write_text(0, h - 1,
                    ascii ? "Up/Down select  Enter play  Esc quit"
                          : "↑↓ select · Enter play · Esc quit",
                    termforge::theme::kDim, termforge::theme::kBg);
}

auto Shell::draw_too_small(termforge::Screen& screen) -> void {
  // Deliberately the crudest possible path: no widgets, no Rect arithmetic.
  // This runs precisely when the geometry the rest of the selector assumes does
  // not hold, so it must not depend on any of it.
  const std::string msg = "term-game needs " + std::to_string(kMinCols) + "x" +
                          std::to_string(kMinRows);
  screen.write_text(0, 0, msg, termforge::theme::kFg, termforge::theme::kBg);
}

}  // namespace termgame
