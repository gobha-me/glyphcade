// glyphcade — Sokoban: the Game, and the only file here that knows a terminal
// exists.
//
// ── What MapWidget made awkward, written down where the workarounds are ─────
//
// gitea #8 makes feedback into termforge #64 a deliverable of this epic rather
// than a side effect, so each item below is a note at the site that pays for
// it. All four are v0.2.2 (== v0.5.1: the widget has not changed in four tags).
//
//  1. set_map_size() WIPES EVERY LAYER, while its own comment says it preserves
//     the overlapping top-left corner "like Screen::resize does". It builds a
//     fresh zero-filled grid and moves it in. Loading a level therefore has to
//     size first and populate second, and a second set_map_size() with the same
//     dimensions still throws the map away. See load(). Filed as termforge
//     https://github.com/gobha-me/termforge/issues/127.
//  2. There is NO tile_at(cell_x, cell_y). To turn a click into a tile the app
//     must re-derive camera(), the tile size and the FLOORED viewport extent —
//     which is the widget's own private viewport_tiles(). That is the exact
//     arithmetic the design doc says the app should not be doing, in the
//     paragraph explaining why the widget owns the camera. See handle_mouse().
//     Filed as termforge https://github.com/gobha-me/termforge/issues/128, and
//     it is this repo's ONE workaround.
//  3. Tile id 0 is "transparent" and the constant (kEmptyId) is private, so the
//     convention is one a consumer has to know rather than name. Our Tile enum
//     starts at 1 for that reason; see glyphs.hpp.
//  4. TileDef::glyph is documented as "a UTF-8 grapheme", singular, but tile
//     size is declared in CELLS and the doc says non-square is the EXPECTED
//     case. A {2,1} tile with a one-grapheme glyph is half glyph and half
//     background fill. We pass two-column strings, which works because
//     write_text lays out text rather than a single cell — but the contract
//     does not say so, and the field name says otherwise.

#include <glyphcade/games/sokoban/sokoban.hpp>

#include <algorithm>
#include <string>
#include <string_view>

#include <termforge/widgets/theme.hpp>

#include <glyphcade/games/sokoban/level.hpp>

namespace glyphcade {

namespace {

using sokoban::Dir;
using sokoban::Tile;

// Same hand-rolled integer formatting the other three games carry, and for the
// same reason: this is the render path and the repo has no formatting
// dependency.
[[nodiscard]] auto num(int v) -> std::string {
  if (v == 0) return "0";
  std::string out;
  for (int n = v; n > 0; n /= 10) {
    out.push_back(static_cast<char>('0' + (n % 10)));
  }
  std::ranges::reverse(out);
  return out;
}

// ⚠ A TABLE, not a switch, and not derived from the level's name.
//
// Every other game switches on an enum because it has three or six settings.
// Sokoban has twenty levels, and a twenty-arm switch is a twenty-arm switch
// nobody reads. What matters is the property the switch was protecting: the key
// must not be derived from display copy. Renaming "Two Texts" must not orphan
// the record a player earned on it, so the key is its ORDINAL and the table
// makes that literal.
//
// ⚠ And every one of these must be exercised by a test. Snake shipped a
// score_key whose Walls branch no test reached, and Tetris shipped one whose
// middle StartLevel never appeared — the same finding two epics running. With
// twenty keys the only honest test is a loop over all of them.
constexpr std::string_view kBestKeys[] = {
    "best_moves_01", "best_moves_02", "best_moves_03", "best_moves_04",
    "best_moves_05", "best_moves_06", "best_moves_07", "best_moves_08",
    "best_moves_09", "best_moves_10", "best_moves_11", "best_moves_12",
    "best_moves_13", "best_moves_14", "best_moves_15", "best_moves_16",
    "best_moves_17", "best_moves_18", "best_moves_19", "best_moves_20",
};
static_assert(std::size(kBestKeys) ==
                  static_cast<std::size_t>(sokoban::level_count()),
              "one score key per bundled level, or a level silently shares "
              "another level's record");

constexpr auto keys_are_unique() -> bool {
  for (std::size_t i = 0; i < std::size(kBestKeys); ++i) {
    for (std::size_t j = i + 1; j < std::size(kBestKeys); ++j) {
      if (kBestKeys[i] == kBestKeys[j]) return false;
    }
  }
  return true;
}
static_assert(keys_are_unique(), "two levels share a score key");

// The entity a cell shows, given what is on it. Terrain is the lower layer, so
// this answers only for the things that move.
[[nodiscard]] auto entity_tile(bool box, bool on_goal, bool player) -> Tile {
  if (player) return on_goal ? Tile::PlayerOnGoal : Tile::Player;
  if (box) return on_goal ? Tile::BoxOnGoal : Tile::Box;
  return Tile::None;
}

}  // namespace

Sokoban::Sokoban() { load(0); }

auto Sokoban::start(GameContext& ctx) -> void {
  m_ctx = &ctx;
  m_frame.set_style(ctx.border_style());
  m_tiles_built = false;

  // ⚠ Resume at the first UNSOLVED level, derived from the score store rather
  // than persisted separately. The reference persists a currentLevel index and
  // never range-checks it on the way back in (game.js:67 then :88), so a stale
  // index past the end makes loadLevel return BEFORE its first render and the
  // player gets a permanently blank board with no error — and that level set
  // has already been replaced wholesale twice in its own git history. A value
  // that is derived cannot go stale.
  int resume = 0;
  while (resume < sokoban::level_count() && best_moves(resume) > 0) ++resume;
  const int start_at = resume >= sokoban::level_count() ? 0 : resume;

  // ⚠ load() FIRST, before the picker opens. The resume level is what the game
  // is on while the picker is up, so index() is already correct for anything
  // that asks — and dismissing without touching anything is then genuinely a
  // no-op rather than a reload.
  load(start_at);

  // gitea #38: twenty levels is a list, not three keys. preselect() rather than
  // default_index because this number comes from the score store and cannot be
  // constexpr; see the note on kSokobanOptions.
  m_options.open(kMeta.title, kMeta.options, &ctx);
  m_options.preselect(0, start_at);
}

auto Sokoban::load(int index) -> void {
  // Clamped, never rejected. See the note in start().
  m_index = std::clamp(index, 0, sokoban::level_count() - 1);

  const auto& entry = sokoban::pack()[static_cast<std::size_t>(m_index)];
  auto parsed = sokoban::parse(entry.rows, entry.name, entry.par);
  if (!parsed) {
    // A bundled level that does not parse is a build-time mistake that reached
    // a player. Say so rather than drawing an empty room; test/31sokoban
    // asserts all twenty parse, so this path should be unreachable in a shipped
    // build and is still not allowed to be silent.
    m_board.reset();
    m_load_error = std::string(sokoban::describe(parsed.error()));
    return;
  }
  m_load_error.clear();
  m_board.emplace(std::move(*parsed));

  // ⚠ Size FIRST, then populate, and never size twice for the same level.
  // set_map_size clears every layer despite its comment (feedback item 1).
  const auto& lv = m_board->level();
  m_map.set_map_size(lv.w, lv.h);
  m_map.set_tile_size(sokoban::kTileCols, sokoban::kTileRows);

  // Layer 0 exists implicitly; the other two are added once per load, because
  // set_map_size has just discarded whatever was there.
  m_layer_terrain = 0;
  m_layer_entities = m_map.add_layer("entities");
  m_layer_overlay = m_map.add_layer("overlay");

  for (int y = 0; y < lv.h; ++y) {
    for (int x = 0; x < lv.w; ++x) {
      const Tile t = lv.is_wall(x, y)    ? Tile::Wall
                     : lv.is_goal(x, y)  ? Tile::Goal
                                         : Tile::Floor;
      m_map.set_tile(m_layer_terrain, x, y, static_cast<int>(t));
    }
  }
  sync_map();
}

auto Sokoban::rebuild_tiles() -> void {
  const bool ascii =
      m_ctx == nullptr || termforge::is_ascii(m_ctx->border_style());
  if (m_tiles_built && ascii == m_tiles_ascii) return;

  const auto& g = sokoban::tiles_for(ascii);
  termforge::TileSet ts;
  for (int id = 1; id < sokoban::kTileCount; ++id) {
    const auto t = static_cast<Tile>(id);
    termforge::TileDef def;
    def.glyph = std::string(sokoban::glyph_for(g, t));
    // At the ASCII tier the glyph carries everything, because FallbackDriver
    // discards colour outright. Asking for theme colours there keeps the tiles
    // identical to the rest of the suite's bottom tier.
    def.fg = ascii ? termforge::theme::kFg : sokoban::colour_for(t);
    def.bg = termforge::theme::kBg;
    ts.define(id, def);
  }
  m_map.set_tileset(std::move(ts));
  m_tiles_ascii = ascii;
  m_tiles_built = true;
}

// Rebuilds the two moving layers from the model.
//
// ⚠ Called from draw(), every frame, NOT from each mutator. The first version
// called it after every step, undo and reset, which is the same list as "every
// mutator" right up until someone adds a sixth one — and the symptom of missing
// it is a screen that silently disagrees with the model rather than anything
// that fails. A test that drove the model directly caught exactly that.
//
// This is also just the immediate-mode contract the rest of the repo keeps:
// draw() fully repaints from state. It costs one pass over the crates, which is
// at most six.
auto Sokoban::sync_map() -> void {
  if (!m_board) return;
  const auto& lv = m_board->level();

  m_map.clear_layer(m_layer_entities);
  m_map.clear_layer(m_layer_overlay);

  for (const sokoban::Pos b : m_board->boxes()) {
    m_map.set_tile(m_layer_entities, b.x, b.y,
                   static_cast<int>(entity_tile(true, lv.is_goal(b.x, b.y),
                                                false)));
    // The overlay is what a deadlock looks like. ⚠ A frozen box ON a goal is
    // not marked — it is a finished box, and marking it would tell the player
    // their solved level is broken.
    if (!lv.is_goal(b.x, b.y) && m_board->is_frozen(b)) {
      m_map.set_tile(m_layer_overlay, b.x, b.y,
                     static_cast<int>(Tile::DeadBox));
    }
  }
  const sokoban::Pos p = m_board->player();
  m_map.set_tile(m_layer_entities, p.x, p.y,
                 static_cast<int>(entity_tile(false, lv.is_goal(p.x, p.y),
                                              true)));
}

// ── Input ──────────────────────────────────────────────────────────────────

auto Sokoban::attempt(Dir d) -> bool {
  if (!m_board) return false;
  const auto r = m_board->step(d);
  if (!r.moved) return true;  // consumed: a bump into a wall is still an answer
  announce(r);
  if (r.won) record_best();
  return true;
}

auto Sokoban::announce(const sokoban::MoveResult& r) -> void {
  if (m_ctx == nullptr) return;
  // ⚠ One gesture, one sound, the discipline the bank has kept since 2048. A
  // push that seats a crate is a seating, not a seating AND a push.
  if (r.won) {
    m_ctx->audio().play(audio::SfxId::Win);
  } else if (r.seated) {
    m_ctx->audio().play(audio::SfxId::Seat);
  } else if (r.pushed) {
    m_ctx->audio().play(audio::SfxId::Slide);
  } else {
    m_ctx->audio().play(audio::SfxId::Click);
  }
}

auto Sokoban::handle_key(const termforge::KeyEvent& key) -> bool {
  // ⚠ Legacy tier, so every KeyEvent is a Press — but the Shell may be handing
  // us releases if some other game left Enhanced on, and acting on both halves
  // of one keystroke would move twice. Same guard the Shell applies to itself.
  if (key.action != termforge::KeyAction::Press) return false;

  switch (key.key) {
    case termforge::Key::Up: return attempt(Dir::Up);
    case termforge::Key::Down: return attempt(Dir::Down);
    case termforge::Key::Left: return attempt(Dir::Left);
    case termforge::Key::Right: return attempt(Dir::Right);
    default: break;
  }
  if (key.key != termforge::Key::Char) return false;

  switch (key.ch) {
    case U'k': case U'K': case U'w': case U'W': return attempt(Dir::Up);
    case U'j': case U'J': case U's': case U'S': return attempt(Dir::Down);
    case U'h': case U'H': case U'a': case U'A': return attempt(Dir::Left);
    case U'l': case U'L': case U'd': case U'D': return attempt(Dir::Right);
    case U'u': case U'U':
      // ⚠ Deliberately live after the level is solved. The reference disables
      // undo AND reset the moment its completion flag goes up and only clears
      // that flag inside loadLevel, so dismissing its celebration overlay by
      // clicking the backdrop strands the player on a frozen board. A flag set
      // on one path and cleared on another is the defect; not having the flag
      // is the fix.
      if (m_board && m_board->undo() && m_ctx != nullptr) {
        m_ctx->audio().play(audio::SfxId::Click);
      }
      return true;
    case U'r': case U'R':
      if (m_board) {
        m_board->reset();
        if (m_ctx != nullptr) m_ctx->audio().play(audio::SfxId::Click);
      }
      return true;
    case U'[':
      load(m_index - 1);
      if (m_ctx != nullptr) m_ctx->audio().play(audio::SfxId::MenuMove);
      return true;
    case U']':
      load(m_index + 1);
      if (m_ctx != nullptr) m_ctx->audio().play(audio::SfxId::MenuMove);
      return true;
    default:
      break;
  }
  return false;
}

auto Sokoban::handle_mouse(const termforge::MouseEvent& mouse) -> bool {
  if (!m_board || !m_layout.fits) return false;
  if (!mouse.pressed || mouse.button != 0) return false;

  // ⚠ FEEDBACK ITEM 2, and this block is the whole of it. MapWidget has no
  // tile_at(), so turning a click into a tile means redoing, out here, exactly
  // what the widget does privately: subtract the rect origin, divide by the
  // tile size, add the camera. The widget owns the camera precisely so an app
  // does not have to reproduce its arithmetic — and then requires the app to
  // reproduce its arithmetic in order to hit-test.
  //
  // ⚠ THIS IS A WORKAROUND, and it is the only one in the repo (STATUS.md keeps
  // the count). DELETION CONDITION: termforge
  // https://github.com/gobha-me/termforge/issues/128 ships a tile-picking
  // accessor. When it lands, this block becomes one `tile_at(x, y)` call and
  // the duplicated viewport arithmetic goes with it.
  const auto [cam_x, cam_y] = m_map.camera();
  const int cx = mouse.x - m_layout.view_x;
  const int cy = mouse.y - m_layout.view_y;
  if (cx < 0 || cy < 0) return false;
  const int tx = (cx / sokoban::kTileCols) + cam_x;
  const int ty = (cy / sokoban::kTileRows) + cam_y;
  // The floored viewport, again reproduced: a click in a trailing PARTIAL tile
  // is a click on a tile the widget did not draw.
  if (cx / sokoban::kTileCols >= m_layout.view_tiles_w()) return false;
  if (cy / sokoban::kTileRows >= m_layout.view_tiles_h()) return false;

  // Only an orthogonally adjacent tile means anything: Sokoban's single verb is
  // a direction, so a click is a direction or it is nothing. No pathfinding —
  // that would be inventing a mechanic the reference does not have, which is
  // the line Snake's layout.hpp already draws about clicks.
  const sokoban::Pos p = m_board->player();
  const int dx = tx - p.x;
  const int dy = ty - p.y;
  if (dx == 0 && dy == -1) return attempt(Dir::Up);
  if (dx == 0 && dy == 1) return attempt(Dir::Down);
  if (dx == -1 && dy == 0) return attempt(Dir::Left);
  if (dx == 1 && dy == 0) return attempt(Dir::Right);
  return false;
}

auto Sokoban::on_event(const termforge::Event& ev) -> bool {
  if (m_options.is_open()) {
    switch (m_options.on_event(ev)) {
      case OptionsScreen::Reply::Ignored:
        return false;  // Escape and 'p' stay the Shell's
      case OptionsScreen::Reply::Consumed:
        return true;
      case OptionsScreen::Reply::Dismissed:
        // Unconditional, like the other three games. load() clamps, so a
        // selection out of range cannot reach the parser.
        load(m_options.selected(0));
        return true;
    }
  }

  if (const auto* key = std::get_if<termforge::KeyEvent>(&ev)) {
    return handle_key(*key);
  }
  if (const auto* mouse = std::get_if<termforge::MouseEvent>(&ev)) {
    return handle_mouse(*mouse);
  }
  return false;
}

// ── Scores ─────────────────────────────────────────────────────────────────

auto Sokoban::record_best() -> void {
  if (m_ctx == nullptr || !m_board) return;
  m_ctx->scores().record(kMeta.slug,
                         kBestKeys[static_cast<std::size_t>(m_index)],
                         m_board->moves(), scores::Better::Lower);
}

auto Sokoban::best_moves(int index) const -> long long {
  if (m_ctx == nullptr) return 0;
  if (index < 0 || index >= sokoban::level_count()) return 0;
  return m_ctx->scores()
      .get(kMeta.slug, kBestKeys[static_cast<std::size_t>(index)])
      .value_or(0);
}

auto Sokoban::solved_count() const -> int {
  // Derived, not stored. A second key would be a second thing to keep in step
  // with the first, and a record only exists for a level that was finished.
  int n = 0;
  for (int i = 0; i < sokoban::level_count(); ++i) {
    if (best_moves(i) > 0) ++n;
  }
  return n;
}

// ── Rendering ──────────────────────────────────────────────────────────────

auto Sokoban::draw(termforge::Screen& screen) -> void {
  // Same arm as draw_broken()/draw_too_small(): the picker owns the whole
  // Screen. Sokoban is turn-based and has no tick(), so unlike Snake and Tetris
  // there is nothing to gate -- nothing moves on its own.
  if (m_options.is_open()) {
    m_options.draw(screen);
    return;
  }

  m_layout = sokoban::compute_layout(screen.cols(), screen.rows(),
                                     m_board ? m_board->level().w : 0,
                                     m_board ? m_board->level().h : 0);

  if (!m_load_error.empty()) {
    draw_broken(screen);
    return;
  }
  if (!m_layout.fits) {
    // ⚠ The status and hint rows are drawn whether or not the map fits, so the
    // width budget below has to survive terminals NARROWER than kNeedCols. That
    // exact mutation went green in two consecutive epics before the fix was
    // carried forward.
    draw_too_small(screen);
    draw_status(screen);
    draw_hints(screen);
    return;
  }

  rebuild_tiles();

  m_frame.set_geometry({.x = m_layout.frame_x,
                        .y = m_layout.frame_y,
                        .w = m_layout.frame_w,
                        .h = m_layout.frame_h});
  m_frame.draw(screen);

  m_map.set_geometry({.x = m_layout.view_x,
                      .y = m_layout.view_y,
                      .w = m_layout.view_w,
                      .h = m_layout.view_h});
  sync_map();
  if (m_board) {
    const sokoban::Pos p = m_board->player();
    // The camera is the widget's, and center_on clamps at the map edges rather
    // than revealing void — so on every bundled level, which is smaller than a
    // normal terminal window, this pins to 0,0 and the map simply sits still.
    m_map.center_on(p.x, p.y);
  }
  m_map.draw(screen);

  draw_status(screen);
  draw_hints(screen);
}

auto Sokoban::draw_status(termforge::Screen& screen) -> void {
  const auto bg = termforge::theme::kBg;
  const auto fg = termforge::theme::kFg;
  const int y = m_layout.fits ? m_layout.status_y : 0;
  if (y >= screen.rows()) return;

  const int cols = screen.cols();
  const int moves = m_board ? m_board->moves() : 0;
  const int pushes = m_board ? m_board->pushes() : 0;
  const int on_goal = m_board ? m_board->boxes_on_goals() : 0;
  const int total =
      m_board ? static_cast<int>(m_board->boxes().size()) : 0;
  const auto& entry = sokoban::pack()[static_cast<std::size_t>(m_index)];
  const long long best = best_moves(m_index);

  const std::string lv = num(m_index + 1) + "/" + num(sokoban::level_count());
  const std::string crates = num(on_goal) + "/" + num(total);
  const std::string bestStr =
      best > 0 ? num(static_cast<int>(best)) : std::string("-");

  // Four budgets. Same approach as the other three games: a line clipped
  // mid-number reads as a rendering bug, so each width gets its own string
  // rather than being truncated.
  std::string s;
  if (cols >= 72) {
    s = "Lv " + lv + " " + std::string(entry.name) + "  Crates " + crates +
        "  Moves " + num(moves) + "  Push " + num(pushes) + "  Best " +
        bestStr + "  Par " + num(entry.par);
  } else if (cols >= 48) {
    s = "Lv " + lv + "  Crates " + crates + "  Moves " + num(moves) +
        "  Best " + bestStr + "  Par " + num(entry.par);
  } else if (cols >= 30) {
    s = "Lv " + lv + "  " + crates + "  Mv " + num(moves) + "  Par " +
        num(entry.par);
  } else {
    s = lv + " " + crates + " " + num(moves);
  }
  screen.write_text(0, y, s, fg, bg);
}

auto Sokoban::draw_hints(termforge::Screen& screen) -> void {
  const auto bg = termforge::theme::kBg;
  const int y = m_layout.fits ? m_layout.hint_y : 1;
  if (y >= screen.rows()) return;

  const int cols = screen.cols();
  const bool won = m_board && m_board->won();
  const bool stuck = m_board && !won && m_board->deadlocked();

  std::string_view hint;
  if (won) {
    hint = cols >= 56 ? "Solved.  ] next level  r replay  u undo  Esc menu"
                      : "Solved.  ] next  r replay";
  } else if (stuck) {
    // The whole point of detecting it. The reference lets a player push a crate
    // into a corner and keep playing a level that can no longer be won.
    hint = cols >= 56
               ? "Stuck: a crate is cornered.  u undo  r reset  Esc menu"
               : "Stuck.  u undo  r reset";
  } else if (cols >= 72) {
    hint =
        "Arrows/hjkl/wasd push  u undo  r reset  [ ] level  p pause  Esc menu";
  } else if (cols >= 44) {
    hint = "Arrows push  u undo  r reset  [ ] level  Esc menu";
  } else {
    hint = "Arrows  u undo  r reset  Esc";
  }
  screen.write_text(0, y, hint, termforge::theme::kDim, bg);
}

auto Sokoban::draw_too_small(termforge::Screen& screen) -> void {
  const auto bg = termforge::theme::kBg;
  const auto fg = termforge::theme::kFg;

  // Same answer the other four games give — but a different REASON, which
  // layout.hpp explains: this floor is about being able to plan a push, not
  // about being able to draw the level. That difference is no longer only a
  // comment: gitea #15 shipped SizeFloor, this is the roster's one Playable,
  // and the selector says "recommended" here where it says "minimum" elsewhere.
  const std::string need = "Sokoban needs " + num(sokoban::kNeedCols) + "x" +
                           num(sokoban::kNeedRows);
  const int mid = screen.rows() / 2;
  const int nx =
      std::max(0, (screen.cols() - static_cast<int>(need.size())) / 2);
  if (mid > 1 && mid < screen.rows()) {
    screen.write_text(nx, mid, need, fg, bg);
  }
}

auto Sokoban::draw_broken(termforge::Screen& screen) -> void {
  const std::string msg = "Level " + num(m_index + 1) + ": " + m_load_error;
  screen.write_text(0, 0, msg, termforge::theme::kFg, termforge::theme::kBg);
}

}  // namespace glyphcade
