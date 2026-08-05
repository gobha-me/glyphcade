#pragma once

// term-game — Sokoban: the Game.
//
// board.hpp has the rules, level.hpp the format and its parser, levels.hpp the
// twenty maps, layout.hpp the geometry, glyphs.hpp the tiers — and none of them
// includes a termforge header. This file is where Screen, Event, MapWidget and
// GameContext appear, which is what makes the split load-bearing rather than
// decorative: test/31sokoban drives every rule, every parse failure and every
// deadlock with no terminal, because it *cannot* reach one.
//
// ── What this file does NOT do ──────────────────────────────────────────────
//
// No pause and no quit-to-menu. The Shell binds 'p' and Escape on anything a
// game declines, so gitea #8's "pause" is satisfied by NOT writing it — the
// same as the other four games. ⚠ Which is also why the level keys are '[' and
// ']' and not 'p'/'n': a game that consumed 'p' would silently take pause away
// from the player.
//
// ── This is MapWidget's first consumer ──────────────────────────────────────
//
// gitea #8 is explicit that this epic has two deliverables — a game, and
// feedback into termforge #64 from OUTSIDE the library, before its API freezes.
// That is why the map is drawn by termforge::MapWidget rather than straight to
// the Screen the way Tetris draws its well: hand-drawn tiles would have been
// less work and would have validated nothing. What the API turned out to make
// awkward is written up at the top of sokoban.cpp, next to the code that has to
// work around it.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>
#include <termforge/widgets/frame.hpp>
#include <termforge/widgets/map_widget.hpp>

#include <termgame/arcade/game.hpp>
#include <termgame/arcade/options_screen.hpp>
#include <termgame/games/sokoban/board.hpp>
#include <termgame/games/sokoban/glyphs.hpp>
#include <termgame/games/sokoban/layout.hpp>
#include <termgame/games/sokoban/levels.hpp>

namespace termgame {

// ⚠ ONE option with twenty choices, which makes Sokoban the only game that
// renders as a windowed LIST rather than a row of `< value >` cyclers. That is
// the case worth having: a schema proved only against three-choice cyclers
// would have baked in a cycler-shaped API for Epic 8 to fight.
//
// ⚠ default_index is 0, and it is NOT what the game starts on. Sokoban's real
// default is "the first level you have not solved", which is a function of the
// score store and cannot be constexpr — start() computes it and calls
// preselect(). default_index is what the SELECTOR advertises, before any game
// exists to ask. See OptionsScreen::preselect.
inline constexpr OptionSpec kSokobanOptions[]{
    {.label = "Level", .choices = sokoban::kLevelNames, .default_index = 0},
};

class Sokoban final : public Game {
 public:
  static constexpr GameMeta kMeta{
      .slug = "sokoban",
      .title = "Sokoban",
      // ⚠ 7-bit ASCII only, enforced by a static_assert in all_games.cpp.
      .description =
          "Push every crate onto a goal. You can push but never pull, so one "
          "crate in one corner ends the level - and this one says so instead "
          "of letting you find out twenty moves later. Twenty levels, "
          "unlimited undo, and a par that was measured rather than guessed.",
      .tag = "Puzzle",
      .icon = "\U0001F4E6",
      // Nothing here needs key releases: a Sokoban move is one discrete step
      // per press, so Legacy is not a fallback, it is the right tier.
      .keyboard = termforge::KeyboardMode::Legacy,
      .options = kSokobanOptions,
      // ⚠ THE ONE Playable ON THE ROSTER, and it is the reason SizeFloor
      // exists at all. Sokoban has a camera: a level larger than the window
      // scrolls rather than being refused, so there is no size at which this
      // game cannot be DRAWN. 34x12 is sixteen tiles across and eight down,
      // which is a judgement about seeing enough of a room to plan a push —
      // see layout.hpp for why it is deliberately not derived from the level
      // pack. The selector prints it as "recommended" where the other four say
      // "minimum", which is that distinction reaching the player.
      .geometry = {.cols = sokoban::kNeedCols,
                   .rows = sokoban::kNeedRows,
                   .floor = SizeFloor::Playable},
  };

  Sokoban();

  [[nodiscard]] auto meta() const -> const GameMeta& override { return kMeta; }
  auto start(GameContext& ctx) -> void override;
  auto on_event(const termforge::Event& ev) -> bool override;
  auto draw(termforge::Screen& screen) -> void override;

  // ── Test seams ────────────────────────────────────────────────────────────
  [[nodiscard]] auto board() const noexcept -> const sokoban::Board* {
    return m_board ? &*m_board : nullptr;
  }
  [[nodiscard]] auto board() noexcept -> sokoban::Board* {
    return m_board ? &*m_board : nullptr;
  }
  [[nodiscard]] auto layout() const noexcept -> const sokoban::Layout& {
    return m_layout;
  }
  [[nodiscard]] auto index() const noexcept -> int { return m_index; }
  // ⚠ Exposed because index() CANNOT witness preselect(). start() calls
  // load(start_at) before opening the picker, so the game is already on the
  // resume level whether or not the picker's cursor agrees -- deleting
  // preselect() leaves every index()-based assertion green while the picker
  // opens on the wrong row.
  [[nodiscard]] auto options() const noexcept -> const OptionsScreen& {
    return m_options;
  }
  [[nodiscard]] auto map() const noexcept -> const termforge::MapWidget& {
    return m_map;
  }
  [[nodiscard]] auto load_error() const noexcept -> std::string_view {
    return m_load_error;
  }

  // Load a level by index. Clamped — see the note on the persisted index in
  // sokoban.cpp.
  auto load(int index) -> void;

 private:
  auto handle_key(const termforge::KeyEvent& key) -> bool;
  auto handle_mouse(const termforge::MouseEvent& mouse) -> bool;
  auto attempt(sokoban::Dir d) -> bool;
  auto announce(const sokoban::MoveResult& r) -> void;
  auto record_best() -> void;
  [[nodiscard]] auto best_moves(int index) const -> long long;
  [[nodiscard]] auto solved_count() const -> int;

  auto rebuild_tiles() -> void;
  auto sync_map() -> void;

  auto draw_status(termforge::Screen& screen) -> void;
  auto draw_hints(termforge::Screen& screen) -> void;
  auto draw_too_small(termforge::Screen& screen) -> void;
  auto draw_broken(termforge::Screen& screen) -> void;

  // The pre-start level picker (gitea #38).
  OptionsScreen m_options{};
  GameContext* m_ctx{nullptr};
  std::optional<sokoban::Board> m_board;
  int m_index{0};
  sokoban::Layout m_layout{};
  termforge::MapWidget m_map;
  termforge::Frame m_frame{"Sokoban"};
  // Empty unless a bundled level failed to parse, which would be a build-time
  // mistake reaching a player. Degradation is an event, not a silence.
  std::string m_load_error;
  // Cached so draw() does not rebuild a TileSet every frame; invalidated when
  // the tier answer changes.
  bool m_tiles_ascii{true};
  bool m_tiles_built{false};

  // The three layers, in painter order. Indices, not names, because MapWidget
  // hands back an int from add_layer.
  int m_layer_terrain{0};
  int m_layer_entities{0};
  int m_layer_overlay{0};
};

}  // namespace termgame
