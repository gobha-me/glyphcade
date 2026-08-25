#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <termforge/core/types.hpp>
#include <termforge/widgets/frame.hpp>

#include <glyphcade/arcade/context.hpp>
#include <glyphcade/arcade/game.hpp>
#include <glyphcade/arcade/options_screen.hpp>
#include <glyphcade/games/solitaire/board.hpp>
#include <glyphcade/games/solitaire/layout.hpp>

namespace glyphcade {

inline constexpr std::string_view kSolitaireDrawChoices[]{"Draw 1", "Draw 3"};
inline constexpr std::string_view kSolitaireScoringChoices[]{"Standard",
                                                             "Vegas"};
inline constexpr std::string_view kSolitaireDeckChoices[]{"Neon", "Classic"};
inline constexpr OptionSpec kSolitaireOptions[]{
    {.label = "Draw", .choices = kSolitaireDrawChoices, .default_index = 1},
    {.label = "Scoring",
     .choices = kSolitaireScoringChoices,
     .default_index = 0},
    {.label = "Deck", .choices = kSolitaireDeckChoices, .default_index = 0},
};

class Solitaire final : public Game {
 public:
  static constexpr GameMeta kMeta{
      .slug = "solitaire",
      .title = "Solitaire",
      .description =
          "Play a daily Klondike deal with draw-one or draw-three rules, "
          "Standard or Vegas scoring, undo, mouse dragging and a complete "
          "keyboard path.",
      .tag = "Card Classic",
      .icon = "\U0001F3B4",
      .options = kSolitaireOptions,
      .geometry = {.cols = solitaire::kNeedCols,
                   .rows = solitaire::kNeedRows,
                   .floor = SizeFloor::Drawable},
  };

  Solitaire();

  [[nodiscard]] auto meta() const -> const GameMeta& override { return kMeta; }
  auto start(GameContext& ctx) -> void override;
  auto tick(std::chrono::duration<double> dt) -> void override;
  auto on_event(const termforge::Event& ev) -> bool override;
  auto draw(termforge::Screen& screen) -> void override;

  auto pixel_regions() -> std::vector<termforge::Rect> override;
  auto draw_pixels(termforge::Rect region, termforge::Extent preferred)
      -> const termforge::Image* override;
  [[nodiscard]] auto pixel_placement(termforge::Rect region) const noexcept
      -> termforge::ImagePlacementOptions override;

  [[nodiscard]] auto board() const noexcept -> const solitaire::Board& {
    return m_board;
  }
  [[nodiscard]] auto board() noexcept -> solitaire::Board& { return m_board; }
  [[nodiscard]] auto layout() const noexcept -> const solitaire::Layout& {
    return m_layout;
  }
  [[nodiscard]] auto options_open() const noexcept -> bool {
    return m_options.is_open();
  }
  [[nodiscard]] auto elapsed() const noexcept -> std::chrono::duration<double> {
    return m_elapsed;
  }
  [[nodiscard]] auto selected_source() const noexcept
      -> std::optional<solitaire::PileRef> {
    return m_selected;
  }
  [[nodiscard]] auto daily_key() const noexcept -> std::string_view {
    return m_date_key;
  }

 private:
  enum class PixelCue : std::uint8_t { None, Selected, Valid };

  struct Cursor {
    bool top{true};
    int pile{0};
    int card{-1};
  };

  struct PixelCard {
    termforge::Rect region{};
    int id{0};
    bool face_up{false};
    bool full{true};
    PixelCue cue{PixelCue::None};
  };

  struct CuedImage {
    int id{0};
    bool face_up{false};
    bool full{true};
    PixelCue cue{PixelCue::None};
    termforge::Image image{};
  };

  auto apply_options() -> void;
  auto load_art(int deck) -> void;
  auto prepare_display_art(termforge::Extent full, int fan_height) -> void;
  auto report_fidelity() -> void;
  auto reset_daily() -> void;
  auto handle_key(const termforge::KeyEvent& key) -> bool;
  auto handle_mouse(const termforge::MouseEvent& mouse) -> bool;
  auto activate_cursor() -> void;
  auto select_or_move(solitaire::PileRef at) -> void;
  auto announce(const solitaire::ActionResult& result) -> void;
  auto normalize_cursor() -> void;
  auto record_win() -> void;
  [[nodiscard]] auto best_score() const -> std::string;

  auto move_horizontal(int delta) -> void;
  auto move_vertical(int delta) -> void;
  [[nodiscard]] auto cursor_source() const -> std::optional<solitaire::PileRef>;
  [[nodiscard]] auto mouse_source(int x, int y) const
      -> std::optional<solitaire::PileRef>;
  [[nodiscard]] auto mouse_target(int x, int y) const
      -> std::optional<solitaire::PileRef>;

  auto draw_status(termforge::Screen& screen) -> void;
  auto draw_hints(termforge::Screen& screen) -> void;
  auto draw_table(termforge::Screen& screen) -> void;
  auto draw_top(termforge::Screen& screen) -> void;
  auto draw_tableau(termforge::Screen& screen) -> void;
  auto draw_drag(termforge::Screen& screen) -> void;
  auto draw_too_small(termforge::Screen& screen) -> void;
  auto draw_card(termforge::Screen& screen, int x, int y,
                 const solitaire::Card& card, bool full, bool selected,
                 bool valid_target) -> void;
  auto draw_back(termforge::Screen& screen, int x, int y, int count, bool full,
                 int id, bool selected) -> void;
  auto draw_empty(termforge::Screen& screen, int x, int y,
                  std::string_view label, bool selected, bool valid_target)
      -> void;
  auto add_pixel(termforge::Rect region, const solitaire::Card& card, bool full,
                 PixelCue cue) -> void;

  GameContext* m_ctx{nullptr};
  OptionsScreen m_options{};
  solitaire::Board m_board;
  solitaire::Layout m_layout{};
  Cursor m_cursor{};
  std::optional<solitaire::PileRef> m_selected{};
  bool m_dragging{false};
  bool m_drag_moved{false};
  int m_drag_x{0};
  int m_drag_y{0};
  bool m_started{false};
  bool m_win_recorded{false};
  bool m_art_ready{false};
  bool m_use_native{false};
  bool m_fidelity_reported{false};
  int m_loaded_deck{-1};
  std::string m_date_key;
  std::chrono::duration<double> m_elapsed{0.0};

  std::array<termforge::Image, 52> m_front_native{};
  std::array<termforge::Image, 52> m_back_native{};
  std::array<termforge::Image, 52> m_front_ansi{};
  std::array<termforge::Image, 52> m_back_ansi{};
  std::array<termforge::Image, 52> m_front_display{};
  std::array<termforge::Image, 52> m_back_display{};
  std::array<termforge::Image, 52> m_front_display_fan{};
  std::array<termforge::Image, 52> m_back_display_fan{};
  termforge::Extent m_display_extent{};
  int m_display_fan_height{0};
  bool m_display_native{false};
  std::vector<CuedImage> m_cued_images{};
  std::vector<PixelCard> m_pixel_cards{};

  termforge::Frame m_frame{"Solitaire"};
};

} // namespace glyphcade
