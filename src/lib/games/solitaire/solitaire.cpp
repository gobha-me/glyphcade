#include <glyphcade/games/solitaire/solitaire.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <variant>

#include <termforge/widgets/detail/width.hpp>
#include <termforge/widgets/glyphs.hpp>
#include <termforge/widgets/theme.hpp>

#include <glyphcade/arcade/hud.hpp>
#include <glyphcade/assets/png.hpp>
#include <glyphcade/generated_assets/solitaire_classic_atlas.hpp>
#include <glyphcade/generated_assets/solitaire_neon_atlas.hpp>

namespace glyphcade {
namespace {

using solitaire::ActionKind;
using solitaire::Card;
using solitaire::PileKind;
using solitaire::PileRef;

constexpr int kSpriteW = 128;
constexpr int kSpriteH = 192;
constexpr termforge::Rgb kRed{0xEF, 0x44, 0x44};
constexpr termforge::Rgb kValidBg{0x16, 0x5B, 0x3A};
constexpr termforge::Pixel kFelt{0x05, 0x32, 0x2C, 255};
constexpr termforge::Pixel kSelectedPixel{0x40, 0x80, 0xFF, 255};
constexpr termforge::Pixel kValidPixel{0x16, 0xA5, 0x5A, 255};

[[nodiscard]] auto number(int value) -> std::string {
  if (value == 0) return "0";
  const bool negative = value < 0;
  unsigned int n = negative ? static_cast<unsigned int>(-(value + 1)) + 1U
                            : static_cast<unsigned int>(value);
  std::string out;
  do {
    out.push_back(static_cast<char>('0' + n % 10U));
    n /= 10U;
  } while (n != 0U);
  if (negative) out.push_back('-');
  std::ranges::reverse(out);
  return out;
}

[[nodiscard]] auto pad2(unsigned int value) -> std::string {
  std::string out;
  out.push_back(static_cast<char>('0' + (value / 10U) % 10U));
  out.push_back(static_cast<char>('0' + value % 10U));
  return out;
}

struct Daily {
  std::uint64_t seed{0};
  std::string label;
};

[[nodiscard]] auto daily() -> Daily {
  using namespace std::chrono;
  const year_month_day date{floor<days>(system_clock::now())};
  const int year = static_cast<int>(date.year());
  const auto month = static_cast<unsigned int>(date.month());
  const auto day = static_cast<unsigned int>(date.day());
  return {.seed = static_cast<std::uint64_t>(year) * 10000ULL +
                  static_cast<std::uint64_t>(month) * 100ULL + day,
          .label = number(year) + pad2(month) + pad2(day)};
}

[[nodiscard]] auto rank_text(solitaire::Rank rank) -> std::string_view {
  constexpr std::string_view names[]{"",  "A", "2", "3",  "4", "5", "6",
                                     "7", "8", "9", "10", "J", "Q", "K"};
  return names[static_cast<std::size_t>(solitaire::rank_value(rank))];
}

[[nodiscard]] auto suit_ascii(solitaire::Suit suit) -> char {
  constexpr char names[]{'C', 'D', 'H', 'S'};
  return names[static_cast<std::size_t>(solitaire::suit_index(suit))];
}

[[nodiscard]] auto suit_unicode(solitaire::Suit suit) -> std::string_view {
  constexpr std::string_view names[]{"\u2663", "\u2666", "\u2665", "\u2660"};
  return names[static_cast<std::size_t>(solitaire::suit_index(suit))];
}

[[nodiscard]] auto repeated(std::string_view glyph, int count) -> std::string {
  std::string out;
  if (count <= 0) return out;
  out.reserve(glyph.size() * static_cast<std::size_t>(count));
  for (int index = 0; index < count; ++index)
    out += glyph;
  return out;
}

[[nodiscard]] auto scale_contained(const termforge::Image& source,
                                   termforge::Extent destination,
                                   termforge::Pixel matte)
    -> termforge::Image {
  if (source.empty() || destination.w <= 0 || destination.h <= 0) return {};

  std::vector<termforge::Pixel> pixels(
      static_cast<std::size_t>(destination.w) * destination.h, matte);
  termforge::Image out{destination.w, destination.h, std::move(pixels)};

  int width = destination.w;
  int height = destination.h;
  if (static_cast<std::int64_t>(destination.w) * source.height() <=
      static_cast<std::int64_t>(destination.h) * source.width()) {
    height = std::max(
        1, static_cast<int>((static_cast<std::int64_t>(source.height()) *
                                 destination.w +
                             source.width() / 2) /
                            source.width()));
  } else {
    width = std::max(
        1, static_cast<int>((static_cast<std::int64_t>(source.width()) *
                                 destination.h +
                             source.height() / 2) /
                            source.height()));
  }
  width = std::min(width, destination.w);
  height = std::min(height, destination.h);
  const int x0 = (destination.w - width) / 2;
  const int y0 = (destination.h - height) / 2;
  for (int y = 0; y < height; ++y) {
    const int sy = (y * source.height()) / height;
    for (int x = 0; x < width; ++x) {
      const int sx = (x * source.width()) / width;
      out.at(x0 + x, y0 + y) = source.at(sx, sy);
    }
  }
  return out;
}

[[nodiscard]] auto centred(std::string_view left, std::string_view text,
                           std::string_view right, int width,
                           int measured_text_cols = -1) -> std::string {
  const int inner = width - 2;
  const int text_cols = measured_text_cols >= 0 ? measured_text_cols
                                                : static_cast<int>(text.size());
  const int before = std::max(0, (inner - text_cols) / 2);
  const int after = std::max(0, inner - text_cols - before);
  return std::string(left) + repeated(" ", before) + std::string(text) +
         repeated(" ", after) + std::string(right);
}

[[nodiscard]] auto elapsed_text(std::chrono::duration<double> elapsed)
    -> std::string {
  const auto seconds = static_cast<long long>(elapsed.count());
  return number(static_cast<int>(seconds / 60)) + ":" +
         pad2(static_cast<unsigned int>(seconds % 60));
}

[[nodiscard]] auto top_slot_nearest(const solitaire::Layout& layout,
                                    int tableau_pile) -> int {
  const int x = layout.tableau_pile_x(tableau_pile) + layout.card_cols / 2;
  int best = 0;
  int distance = 100000;
  for (int slot = 0; slot < solitaire::kTopPiles; ++slot) {
    const int sx = layout.top_pile_x(slot) + layout.card_cols / 2;
    if (std::abs(sx - x) < distance) {
      distance = std::abs(sx - x);
      best = slot;
    }
  }
  return best;
}

[[nodiscard]] auto tableau_nearest(const solitaire::Layout& layout,
                                   int top_slot) -> int {
  const int x = layout.top_pile_x(top_slot) + layout.card_cols / 2;
  return std::clamp(
      (x - layout.tableau_x + (layout.card_cols + layout.pile_gap_cols) / 2) /
          (layout.card_cols + layout.pile_gap_cols),
      0, solitaire::kTableauPiles - 1);
}

} // namespace

Solitaire::Solitaire() : m_board(0) {
}

auto Solitaire::start(GameContext& ctx) -> void {
  m_ctx = &ctx;
  m_fidelity_reported = false;
  m_frame.set_style(ctx.border_style());
  m_options.open(kMeta.title, kMeta.options, &ctx);
}

auto Solitaire::apply_options() -> void {
  const auto draw = m_options.selected(0) == 0 ? solitaire::DrawMode::One
                                               : solitaire::DrawMode::Three;
  const auto scoring = m_options.selected(1) == 0
                           ? solitaire::ScoringMode::Standard
                           : solitaire::ScoringMode::Vegas;
  const Daily deal = daily();
  m_date_key =
      deal.label + (draw == solitaire::DrawMode::One ? ":draw1:" : ":draw3:") +
      (scoring == solitaire::ScoringMode::Standard ? "standard" : "vegas");
  m_board.reset(deal.seed, draw, scoring);
  m_frame.set_title("Solitaire " + deal.label);
  m_elapsed = std::chrono::duration<double>{0.0};
  m_cursor = Cursor{};
  m_selected.reset();
  m_dragging = false;
  m_drag_moved = false;
  m_win_recorded = false;
  m_started = true;
  load_art(m_options.selected(2));
  report_fidelity();
  if (m_ctx != nullptr) m_ctx->audio().play(audio::SfxId::CardDeal);
}

auto Solitaire::reset_daily() -> void {
  apply_options();
}

auto Solitaire::load_art(int deck) -> void {
  if (m_loaded_deck == deck && m_art_ready) return;
  m_art_ready = false;
  m_display_extent = {};
  m_display_fan_height = 0;
  m_loaded_deck = deck;
  if (m_ctx == nullptr) return;
  const auto& caps = m_ctx->capabilities();
  if (!caps.kitty_graphics && !caps.truecolor) return;

  const auto bytes = deck == 0 ? assets::embedded::solitaire_neon_atlas()
                               : assets::embedded::solitaire_classic_atlas();
  auto decoded = assets::decode_png(bytes);
  if (!decoded) {
    m_ctx->report(decoded.error());
    m_fidelity_reported = true;
    return;
  }
  if (decoded->width() != 13 * kSpriteW || decoded->height() != 5 * kSpriteH) {
    m_ctx->report(termforge::ErrorEvent{
        termforge::Severity::Warning, "solitaire",
        "Solitaire deck atlas has the wrong dimensions: using text cards"});
    m_fidelity_reported = true;
    return;
  }

  const termforge::Image back =
      decoded->sub({0, 4 * kSpriteH, kSpriteW, kSpriteH});
  for (int id = 0; id < 52; ++id) {
    const int suit = id / 13;
    const int rank = id % 13;
    m_front_native[static_cast<std::size_t>(id)] =
        decoded->sub({rank * kSpriteW, suit * kSpriteH, kSpriteW, kSpriteH});
    m_back_native[static_cast<std::size_t>(id)] = back;

    const std::vector<termforge::Pixel> table(
        static_cast<std::size_t>(kSpriteW) * kSpriteH, kFelt);
    m_front_ansi[static_cast<std::size_t>(id)] =
        termforge::Image{kSpriteW, kSpriteH, table};
    m_front_ansi[static_cast<std::size_t>(id)].blend(
        m_front_native[static_cast<std::size_t>(id)], 0, 0);
    m_back_ansi[static_cast<std::size_t>(id)] =
        termforge::Image{kSpriteW, kSpriteH, table};
    m_back_ansi[static_cast<std::size_t>(id)].blend(back, 0, 0);
  }
  m_use_native = caps.kitty_graphics;
  m_art_ready = true;
}

auto Solitaire::prepare_display_art(termforge::Extent full, int fan_height)
    -> void {
  fan_height = std::clamp(fan_height, 1, full.h);
  if (m_display_extent == full && m_display_fan_height == fan_height &&
      m_display_native == m_use_native) {
    return;
  }

  // draw_pixels() is immediate, but this work is not: it runs only when the
  // terminal's measured cell size or the responsive card geometry changes.
  // Returning an image at the driver's exact preferred extent lets Stretch be
  // an identity operation. The atlas is scaled proportionally into that canvas
  // instead of being hydraulically flattened to the destination rectangle.
  const termforge::Pixel matte =
      m_use_native ? termforge::Pixel{0, 0, 0, 0} : kFelt;
  for (std::size_t id = 0; id < m_front_display.size(); ++id) {
    const auto& front = m_use_native ? m_front_native[id] : m_front_ansi[id];
    const auto& back = m_use_native ? m_back_native[id] : m_back_ansi[id];
    m_front_display[id] = scale_contained(front, full, matte);
    m_back_display[id] = scale_contained(back, full, matte);
    // The ANSI driver cannot crop at placement time. Slice the already-scaled
    // full card so a covered tableau card keeps the exact same width and scale.
    m_front_display_fan[id] =
        m_front_display[id].sub({0, 0, full.w, fan_height});
    m_back_display_fan[id] =
        m_back_display[id].sub({0, 0, full.w, fan_height});
  }
  m_cued_images.clear();
  // Every possible (id, face/back, full/fan, selected/valid) key fits without
  // reallocation, so pointers already collected for this frame remain stable.
  m_cued_images.reserve(m_front_display.size() * 8);
  m_display_extent = full;
  m_display_fan_height = fan_height;
  m_display_native = m_use_native;
}

auto Solitaire::report_fidelity() -> void {
  if (m_ctx == nullptr || m_fidelity_reported) return;
  const auto& caps = m_ctx->capabilities();
  if (caps.kitty_graphics && m_art_ready) {
    m_fidelity_reported = true;
    return;
  }
  if (caps.truecolor && m_art_ready) {
    m_ctx->report(termforge::ErrorEvent{
        termforge::Severity::Info, "solitaire",
        "Solitaire native card art unavailable: using raster cards"});
    m_fidelity_reported = true;
    return;
  }
  const bool ascii = termforge::is_ascii(m_ctx->border_style());
  m_ctx->report(termforge::ErrorEvent{
      termforge::Severity::Info, "solitaire",
      ascii ? "Solitaire card art unavailable: using ASCII text cards"
            : "Solitaire card art unavailable: using Unicode text cards"});
  m_fidelity_reported = true;
}

auto Solitaire::tick(std::chrono::duration<double> dt) -> void {
  if (m_started && !m_board.won()) m_elapsed += dt;
}

auto Solitaire::on_event(const termforge::Event& ev) -> bool {
  if (m_options.is_open()) {
    const auto reply = m_options.on_event(ev);
    if (reply == OptionsScreen::Reply::Dismissed) {
      apply_options();
      return true;
    }
    return reply == OptionsScreen::Reply::Consumed;
  }
  if (const auto* key = std::get_if<termforge::KeyEvent>(&ev)) {
    return handle_key(*key);
  }
  if (const auto* mouse = std::get_if<termforge::MouseEvent>(&ev)) {
    return handle_mouse(*mouse);
  }
  return false;
}

auto Solitaire::announce(const solitaire::ActionResult& result) -> void {
  if (!result.changed || m_ctx == nullptr) {
    if (m_ctx != nullptr) m_ctx->audio().play(audio::SfxId::InvalidMove);
    return;
  }
  switch (result.kind) {
    case ActionKind::Deal:
    case ActionKind::Recycle:
      m_ctx->audio().play(audio::SfxId::CardDeal);
      break;
    case ActionKind::Flip: m_ctx->audio().play(audio::SfxId::CardFlip); break;
    case ActionKind::Foundation:
      m_ctx->audio().play(audio::SfxId::CardFoundation);
      break;
    case ActionKind::Place:
    case ActionKind::Undo: m_ctx->audio().play(audio::SfxId::CardPlace); break;
    case ActionKind::AutoComplete:
      m_ctx->audio().play(audio::SfxId::CardFoundation);
      break;
    case ActionKind::None: break;
  }
  normalize_cursor();
  if (result.won) {
    m_ctx->audio().play(audio::SfxId::Win);
    record_win();
  }
}

auto Solitaire::normalize_cursor() -> void {
  if (m_cursor.top) return;
  m_cursor.pile = std::clamp(m_cursor.pile, 0, 6);
  const auto& pile =
      m_board.position().tableau[static_cast<std::size_t>(m_cursor.pile)];
  m_cursor.card = pile.empty() ? -1
                               : std::clamp(m_cursor.card, 0,
                                            static_cast<int>(pile.size()) - 1);
}

auto Solitaire::record_win() -> void {
  if (m_ctx == nullptr || m_win_recorded || !m_board.won()) return;
  const std::string prefix = m_date_key + ":";
  m_ctx->scores().record(kMeta.slug, prefix + "score", m_board.score(),
                         scores::Better::Higher);
  m_ctx->scores().record(kMeta.slug, prefix + "moves", m_board.moves(),
                         scores::Better::Lower);
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(m_elapsed).count();
  m_ctx->scores().record(kMeta.slug, prefix + "time_ms", millis,
                         scores::Better::Lower);
  m_win_recorded = true;
}

auto Solitaire::best_score() const -> std::string {
  if (m_ctx == nullptr) return "---";
  const auto value = m_ctx->scores().get(kMeta.slug, m_date_key + ":score");
  return value ? number(static_cast<int>(*value)) : "---";
}

auto Solitaire::cursor_source() const -> std::optional<PileRef> {
  const auto& p = m_board.position();
  if (m_cursor.top) {
    if (m_cursor.pile == 1 && !p.waste.empty()) {
      return PileRef{PileKind::Waste, 0, -1};
    }
    if (m_cursor.pile >= 2) {
      const int foundation = m_cursor.pile - 2;
      if (!p.foundations[static_cast<std::size_t>(foundation)].empty()) {
        return PileRef{PileKind::Foundation, foundation, -1};
      }
    }
    return std::nullopt;
  }
  const auto& pile = p.tableau[static_cast<std::size_t>(m_cursor.pile)];
  if (m_cursor.card >= 0 &&
      static_cast<std::size_t>(m_cursor.card) < pile.size() &&
      pile[static_cast<std::size_t>(m_cursor.card)].face_up) {
    return PileRef{PileKind::Tableau, m_cursor.pile, m_cursor.card};
  }
  return std::nullopt;
}

auto Solitaire::select_or_move(PileRef at) -> void {
  if (!m_selected) {
    m_selected = at;
    return;
  }
  if (*m_selected == at) {
    m_selected.reset();
    return;
  }
  PileRef target = at;
  if (at.kind == PileKind::Tableau) target.card = -1;
  const auto result = m_board.move(*m_selected, target);
  announce(result);
  if (result.changed) m_selected.reset();
}

auto Solitaire::activate_cursor() -> void {
  const auto& p = m_board.position();
  if (m_cursor.top && m_cursor.pile == 0) {
    announce(m_board.act_stock());
    m_selected.reset();
    return;
  }
  if (!m_cursor.top) {
    const auto& pile = p.tableau[static_cast<std::size_t>(m_cursor.pile)];
    if (!pile.empty() && !pile.back().face_up &&
        m_cursor.card == static_cast<int>(pile.size()) - 1) {
      announce(m_board.flip_tableau(m_cursor.pile));
      return;
    }
  }
  if (const auto source = cursor_source()) {
    select_or_move(*source);
    return;
  }
  if (m_selected) {
    const PileRef target =
        m_cursor.top ? PileRef{PileKind::Foundation, m_cursor.pile - 2, -1}
                     : PileRef{PileKind::Tableau, m_cursor.pile, -1};
    const auto result = m_board.move(*m_selected, target);
    announce(result);
    if (result.changed) m_selected.reset();
  } else {
    announce({});
  }
}

auto Solitaire::move_horizontal(int delta) -> void {
  const int count =
      m_cursor.top ? solitaire::kTopPiles : solitaire::kTableauPiles;
  m_cursor.pile = std::clamp(m_cursor.pile + delta, 0, count - 1);
  if (!m_cursor.top) {
    const auto& pile =
        m_board.position().tableau[static_cast<std::size_t>(m_cursor.pile)];
    m_cursor.card = pile.empty() ? -1 : static_cast<int>(pile.size()) - 1;
  }
}

auto Solitaire::move_vertical(int delta) -> void {
  if (m_cursor.top) {
    if (delta < 0) return;
    m_cursor.pile = tableau_nearest(m_layout, m_cursor.pile);
    m_cursor.top = false;
    const auto& pile =
        m_board.position().tableau[static_cast<std::size_t>(m_cursor.pile)];
    if (pile.empty()) {
      m_cursor.card = -1;
      return;
    }
    const auto first =
        std::ranges::find_if(pile, [](Card card) { return card.face_up; });
    m_cursor.card = first == pile.end()
                        ? static_cast<int>(pile.size()) - 1
                        : static_cast<int>(first - pile.begin());
    return;
  }

  const auto& pile =
      m_board.position().tableau[static_cast<std::size_t>(m_cursor.pile)];
  if (delta > 0 && m_cursor.card + 1 < static_cast<int>(pile.size())) {
    ++m_cursor.card;
    return;
  }
  if (delta < 0) {
    const int first_face = static_cast<int>(
        std::ranges::find_if(pile, [](Card card) { return card.face_up; }) -
        pile.begin());
    if (m_cursor.card > first_face) {
      --m_cursor.card;
      return;
    }
    m_cursor.top = true;
    m_cursor.pile = top_slot_nearest(m_layout, m_cursor.pile);
    m_cursor.card = -1;
  }
}

auto Solitaire::handle_key(const termforge::KeyEvent& key) -> bool {
  if (key.action == termforge::KeyAction::Release) return false;
  switch (key.key) {
    case termforge::Key::Left: move_horizontal(-1); return true;
    case termforge::Key::Right: move_horizontal(1); return true;
    case termforge::Key::Up: move_vertical(-1); return true;
    case termforge::Key::Down: move_vertical(1); return true;
    case termforge::Key::Enter: activate_cursor(); return true;
    default: break;
  }
  if (key.key != termforge::Key::Char) return false;
  switch (key.ch) {
    case U'h':
    case U'H': move_horizontal(-1); return true;
    case U'l':
    case U'L': move_horizontal(1); return true;
    case U'k':
    case U'K': move_vertical(-1); return true;
    case U'j':
    case U'J': move_vertical(1); return true;
    case U' ': activate_cursor(); return true;
    case U'u':
    case U'U':
      announce(m_board.undo());
      m_selected.reset();
      return true;
    case U'n':
    case U'N': reset_daily(); return true;
    case U'a':
    case U'A':
      announce(m_board.auto_complete());
      m_selected.reset();
      return true;
    case U'f':
    case U'F': {
      const auto source = m_selected ? m_selected : cursor_source();
      if (!source) {
        announce({});
        return true;
      }
      const auto cards = [&]() -> const Card* {
        const auto& p = m_board.position();
        if (source->kind == PileKind::Waste) {
          return p.waste.empty() ? nullptr : &p.waste.back();
        }
        if (source->kind == PileKind::Foundation) {
          const auto& pile =
              p.foundations[static_cast<std::size_t>(source->pile)];
          return pile.empty() ? nullptr : &pile.back();
        }
        const auto& pile = p.tableau[static_cast<std::size_t>(source->pile)];
        return source->card < 0 ||
                       static_cast<std::size_t>(source->card) >= pile.size()
                   ? nullptr
                   : &pile[static_cast<std::size_t>(source->card)];
      }();
      if (cards == nullptr) {
        announce({});
        return true;
      }
      const auto result =
          m_board.move(*source, {PileKind::Foundation,
                                 solitaire::suit_index(cards->suit), -1});
      announce(result);
      if (result.changed) m_selected.reset();
      return true;
    }
    case U'q':
    case U'Q':
      if (m_ctx != nullptr) m_ctx->quit_to_menu();
      return true;
    default: return false;
  }
}

auto Solitaire::mouse_source(int x, int y) const -> std::optional<PileRef> {
  if (!m_layout.fits) return std::nullopt;
  const auto& p = m_board.position();
  for (int slot = 1; slot < solitaire::kTopPiles; ++slot) {
    const termforge::Rect rect{m_layout.top_pile_x(slot), m_layout.top_y,
                               m_layout.card_cols, m_layout.card_rows};
    if (!rect.contains(x, y)) continue;
    if (slot == 1 && !p.waste.empty()) return PileRef{PileKind::Waste, 0, -1};
    if (slot >= 2 &&
        !p.foundations[static_cast<std::size_t>(slot - 2)].empty()) {
      return PileRef{PileKind::Foundation, slot - 2, -1};
    }
  }
  for (int pile_index = 0; pile_index < 7; ++pile_index) {
    const int px = m_layout.tableau_pile_x(pile_index);
    if (x < px || x >= px + m_layout.card_cols || y < m_layout.tableau_y) {
      continue;
    }
    const auto& pile = p.tableau[static_cast<std::size_t>(pile_index)];
    const int hidden = static_cast<int>(
        std::ranges::find_if(pile, [](Card card) { return card.face_up; }) -
        pile.begin());
    const int face_up = static_cast<int>(pile.size()) - hidden;
    const auto card = solitaire::tableau_card_at(y - m_layout.tableau_y, hidden,
                                                 face_up, m_layout.card_rows);
    if (card && pile[static_cast<std::size_t>(*card)].face_up) {
      return PileRef{PileKind::Tableau, pile_index, *card};
    }
  }
  return std::nullopt;
}

auto Solitaire::mouse_target(int x, int y) const -> std::optional<PileRef> {
  if (!m_layout.fits) return std::nullopt;
  for (int slot = 2; slot < solitaire::kTopPiles; ++slot) {
    if (termforge::Rect{m_layout.top_pile_x(slot), m_layout.top_y,
                        m_layout.card_cols, m_layout.card_rows}
            .contains(x, y)) {
      return PileRef{PileKind::Foundation, slot - 2, -1};
    }
  }
  for (int pile = 0; pile < 7; ++pile) {
    const int px = m_layout.tableau_pile_x(pile);
    if (x >= px && x < px + m_layout.card_cols && y >= m_layout.tableau_y &&
        y < m_layout.status_y) {
      return PileRef{PileKind::Tableau, pile, -1};
    }
  }
  return std::nullopt;
}

auto Solitaire::handle_mouse(const termforge::MouseEvent& mouse) -> bool {
  if (!m_layout.fits) return false;
  if (mouse.button != 0 && mouse.action() != termforge::MouseAction::Move) {
    return false;
  }
  m_drag_x = mouse.x;
  m_drag_y = mouse.y;
  if (mouse.action() == termforge::MouseAction::Press) {
    const termforge::Rect stock{m_layout.top_pile_x(0), m_layout.top_y,
                                m_layout.card_cols, m_layout.card_rows};
    if (stock.contains(mouse.x, mouse.y)) {
      announce(m_board.act_stock());
      m_selected.reset();
      return true;
    }

    const auto& p = m_board.position();
    for (int pile = 0; pile < 7; ++pile) {
      const int px = m_layout.tableau_pile_x(pile);
      if (mouse.x < px || mouse.x >= px + m_layout.card_cols ||
          mouse.y < m_layout.tableau_y) {
        continue;
      }
      const auto& cards = p.tableau[static_cast<std::size_t>(pile)];
      if (!cards.empty() && !cards.back().face_up &&
          mouse.y < m_layout.tableau_y + m_layout.card_rows) {
        announce(m_board.flip_tableau(pile));
        return true;
      }
    }
    const auto source = mouse_source(mouse.x, mouse.y);
    if (m_selected) {
      if (const auto target = mouse_target(mouse.x, mouse.y);
          target && (!source || *source != *m_selected)) {
        const auto result = m_board.move(*m_selected, *target);
        announce(result);
        if (result.changed) m_selected.reset();
        return true;
      }
    }
    if (source) {
      m_selected = source;
      m_dragging = true;
      m_drag_moved = false;
      return true;
    }
    return false;
  }
  if (mouse.action() == termforge::MouseAction::Drag) {
    m_drag_moved = true;
    return m_dragging;
  }
  if (mouse.action() == termforge::MouseAction::Release && m_dragging) {
    m_dragging = false;
    if (!m_drag_moved) return true;
    if (const auto target = mouse_target(mouse.x, mouse.y);
        target && m_selected) {
      const auto result = m_board.move(*m_selected, *target);
      announce(result);
      if (result.changed) m_selected.reset();
    } else {
      announce({});
    }
    return true;
  }
  return false;
}

auto Solitaire::draw(termforge::Screen& screen) -> void {
  m_pixel_cards.clear();
  if (m_options.is_open()) {
    m_options.draw(screen);
    return;
  }
  m_layout = solitaire::compute_layout(screen.cols(), screen.rows());
  screen.clear(termforge::theme::kFg, termforge::theme::kBg);
  if (!m_layout.fits) {
    draw_too_small(screen);
    return;
  }
  m_frame.set_style(m_ctx != nullptr ? m_ctx->border_style()
                                     : termforge::BorderStyle::Ascii);
  m_frame.set_geometry(
      {m_layout.frame_x, m_layout.frame_y, m_layout.frame_w, m_layout.frame_h});
  m_frame.draw(screen);
  draw_table(screen);
  draw_status(screen);
  draw_hints(screen);
}

auto Solitaire::draw_table(termforge::Screen& screen) -> void {
  draw_top(screen);
  draw_tableau(screen);
  draw_drag(screen);
}

auto Solitaire::draw_top(termforge::Screen& screen) -> void {
  const auto& p = m_board.position();
  const bool stock_cursor = m_cursor.top && m_cursor.pile == 0;
  if (!p.stock.empty()) {
    draw_back(screen, m_layout.top_pile_x(0), m_layout.top_y,
              static_cast<int>(p.stock.size()), true,
              solitaire::card_id(p.stock.back()), stock_cursor);
  } else {
    draw_empty(screen, m_layout.top_pile_x(0), m_layout.top_y,
               p.waste.empty() ? "-" : "R", stock_cursor, false);
  }

  if (!p.waste.empty()) {
    draw_card(screen, m_layout.top_pile_x(1), m_layout.top_y, p.waste.back(),
              true,
              m_selected == PileRef{PileKind::Waste, 0, -1} ||
                  (m_cursor.top && m_cursor.pile == 1),
              false);
  } else {
    draw_empty(screen, m_layout.top_pile_x(1), m_layout.top_y, "W",
               m_cursor.top && m_cursor.pile == 1, false);
  }

  constexpr std::string_view labels[]{"C", "D", "H", "S"};
  for (int foundation = 0; foundation < 4; ++foundation) {
    const PileRef target{PileKind::Foundation, foundation, -1};
    const bool valid = m_selected && m_board.can_move(*m_selected, target);
    const auto& pile = p.foundations[static_cast<std::size_t>(foundation)];
    if (pile.empty()) {
      draw_empty(screen, m_layout.top_pile_x(foundation + 2), m_layout.top_y,
                 labels[static_cast<std::size_t>(foundation)],
                 m_cursor.top && m_cursor.pile == foundation + 2, valid);
    } else {
      draw_card(screen, m_layout.top_pile_x(foundation + 2), m_layout.top_y,
                pile.back(), true,
                m_selected == PileRef{PileKind::Foundation, foundation, -1} ||
                    (m_cursor.top && m_cursor.pile == foundation + 2),
                valid);
    }
  }
}

auto Solitaire::draw_tableau(termforge::Screen& screen) -> void {
  const auto& table = m_board.position().tableau;
  for (int pile_index = 0; pile_index < 7; ++pile_index) {
    const auto& pile = table[static_cast<std::size_t>(pile_index)];
    const int x = m_layout.tableau_pile_x(pile_index);
    const PileRef target{PileKind::Tableau, pile_index, -1};
    const bool valid = m_selected && m_board.can_move(*m_selected, target);
    if (pile.empty()) {
      draw_empty(screen, x, m_layout.tableau_y, "K",
                 !m_cursor.top && m_cursor.pile == pile_index, valid);
      continue;
    }

    const int hidden = static_cast<int>(
        std::ranges::find_if(pile, [](Card card) { return card.face_up; }) -
        pile.begin());
    const int face_up = static_cast<int>(pile.size()) - hidden;
    int y = m_layout.tableau_y;
    if (face_up == 0) {
      const Card card = pile.back();
      draw_back(screen, x, y, hidden, true, solitaire::card_id(card), false);
      continue;
    }
    if (hidden > 0) {
      const Card card = pile[static_cast<std::size_t>(hidden - 1)];
      draw_back(screen, x, y, hidden, false, solitaire::card_id(card), false);
      ++y;
    }
    for (int index = hidden; index < static_cast<int>(pile.size()); ++index) {
      const bool full = index + 1 == static_cast<int>(pile.size());
      const PileRef source{PileKind::Tableau, pile_index, index};
      draw_card(screen, x, y, pile[static_cast<std::size_t>(index)], full,
                m_selected == source ||
                    (!m_cursor.top && m_cursor.pile == pile_index &&
                     m_cursor.card == index),
                full && valid);
      y += full ? m_layout.card_rows : solitaire::kFaceUpFanRows;
    }
  }
}

auto Solitaire::draw_drag(termforge::Screen& screen) -> void {
  if (!m_dragging || !m_selected) return;
  const auto& p = m_board.position();
  std::vector<Card> cards;
  if (m_selected->kind == PileKind::Waste && !p.waste.empty()) {
    cards.push_back(p.waste.back());
  } else if (m_selected->kind == PileKind::Foundation) {
    const auto& pile =
        p.foundations[static_cast<std::size_t>(m_selected->pile)];
    if (!pile.empty()) cards.push_back(pile.back());
  } else if (m_selected->kind == PileKind::Tableau) {
    const auto& pile = p.tableau[static_cast<std::size_t>(m_selected->pile)];
    if (m_selected->card >= 0 &&
        static_cast<std::size_t>(m_selected->card) < pile.size()) {
      const auto first = static_cast<std::size_t>(m_selected->card);
      cards.reserve(pile.size() - first);
      for (std::size_t index = first; index < pile.size(); ++index) {
        cards.push_back(pile[index]);
      }
    }
  }
  if (cards.empty()) return;

  // A physical card is one pixel source per frame. Remove the stationary art
  // for the dragged run, then re-declare those same unique card buffers at the
  // pointer. The authored cells remain at the source as a stable fallback.
  for (const Card card : cards) {
    const int id = solitaire::card_id(card);
    std::erase_if(m_pixel_cards,
                  [&](const PixelCard& pixel) { return pixel.id == id; });
  }

  const int x =
      std::clamp(m_drag_x - m_layout.card_cols / 2, m_layout.frame_x + 1,
                 m_layout.frame_x + m_layout.frame_w - m_layout.card_cols - 1);
  int y = std::clamp(m_drag_y - 1, m_layout.top_y,
                     m_layout.status_y - m_layout.card_rows);
  for (std::size_t index = 0; index < cards.size(); ++index) {
    const bool full = index + 1 == cards.size();
    const termforge::Rect region{x, y, m_layout.card_cols,
                                 full ? m_layout.card_rows : 1};
    std::erase_if(m_pixel_cards, [&](const PixelCard& pixel) {
      return pixel.region == region;
    });
    draw_card(screen, x, y, cards[index], full, true, false);
    y += full ? m_layout.card_rows : solitaire::kFaceUpFanRows;
  }
}

auto Solitaire::draw_card(termforge::Screen& screen, int x, int y,
                          const Card& card, bool full, bool selected,
                          bool valid_target) -> void {
  const bool ascii =
      m_ctx == nullptr || termforge::is_ascii(m_ctx->border_style());
  const auto fg = solitaire::is_red(card.suit) ? kRed : termforge::theme::kFg;
  const auto bg = selected ? termforge::theme::kFocusBg
                           : (valid_target ? kValidBg : termforge::theme::kBg);
  const std::string rank{rank_text(card.rank)};
  const std::string suit = ascii ? std::string(1, suit_ascii(card.suit))
                                 : std::string(suit_unicode(card.suit));
  const int content = static_cast<int>(rank.size()) + 1;
  const int inner = m_layout.card_cols - 2;
  std::string top;
  const char left = selected ? '[' : (valid_target ? '<' : '+');
  const char right = selected ? ']' : (valid_target ? '>' : '+');
  if (ascii) {
    top = std::string(1, left) + rank + suit + repeated("-", inner - content) +
          right;
  } else {
    top = (selected || valid_target ? std::string(1, left) : "\u256d") + rank +
          suit + repeated("\u2500", inner - content) +
          (selected || valid_target ? std::string(1, right) : "\u256e");
  }
  screen.write_text(x, y, top, fg, bg);
  if (full) {
    for (int row = 1; row < m_layout.card_rows - 1; ++row) {
      const bool label_row = row == m_layout.card_rows / 2;
      const std::string middle = centred(
          ascii ? "|" : "\u2502", label_row ? suit : "", ascii ? "|" : "\u2502",
          m_layout.card_cols, label_row ? 1 : 0);
      screen.write_text(x, y + row, middle, fg, bg);
    }
    const std::string bottom =
        (selected || valid_target ? std::string(1, left)
                                  : (ascii ? "+" : "\u2570")) +
        repeated(ascii || selected || valid_target ? "-" : "\u2500", inner) +
        (selected || valid_target ? std::string(1, right)
                                  : (ascii ? "+" : "\u256f"));
    screen.write_text(x, y + m_layout.card_rows - 1, bottom, fg, bg);
  }
  if (m_use_native || (!selected && !valid_target)) {
    add_pixel({x, y, m_layout.card_cols, full ? m_layout.card_rows : 1}, card,
              full, selected ? PixelCue::Selected
                             : (valid_target ? PixelCue::Valid
                                             : PixelCue::None));
  }
}

auto Solitaire::draw_back(termforge::Screen& screen, int x, int y, int count,
                          bool full, int id, bool selected) -> void {
  const bool ascii =
      m_ctx == nullptr || termforge::is_ascii(m_ctx->border_style());
  const auto bg = selected ? termforge::theme::kFocusBg : termforge::theme::kBg;
  const int inner = m_layout.card_cols - 2;
  const std::string top = (selected ? "[" : (ascii ? "+" : "\u256d")) +
                          repeated(selected || ascii ? "-" : "\u2500", inner) +
                          (selected ? "]" : (ascii ? "+" : "\u256e"));
  const std::string bottom =
      (selected ? "[" : (ascii ? "+" : "\u2570")) +
      repeated(selected || ascii ? "-" : "\u2500", inner) +
      (selected ? "]" : (ascii ? "+" : "\u256f"));
  if (full) {
    screen.write_text(x, y, top, termforge::theme::kDim, bg);
    for (int row = 1; row < m_layout.card_rows - 1; ++row) {
      screen.write_text(x, y + row,
                        (ascii ? "|" : "\u2502") +
                            repeated(ascii ? "#" : "\u2592", inner) +
                            (ascii ? "|" : "\u2502"),
                        termforge::theme::kDim, bg);
    }
    screen.write_text(x, y + m_layout.card_rows - 1, bottom,
                      termforge::theme::kDim, bg);
  } else {
    const std::string label = pad2(static_cast<unsigned int>(count));
    screen.write_text(x, y,
                      (ascii ? "+" : "\u256d") + label +
                          repeated(ascii ? "#" : "\u2592", inner - 2) +
                          (ascii ? "+" : "\u256e"),
                      termforge::theme::kDim, bg);
  }
  Card card{.suit = static_cast<solitaire::Suit>(id / 13),
            .rank = static_cast<solitaire::Rank>(id % 13 + 1),
            .face_up = false};
  // A covered back carries the hidden-card COUNT in its cell row. The atlas
  // cannot encode that position-specific number, so keep this semantic strip
  // in the Baseline at every tier instead of letting a pixel region erase it.
  if (full && (m_use_native || !selected)) {
    add_pixel({x, y, m_layout.card_cols, full ? m_layout.card_rows : 1}, card,
              full, selected ? PixelCue::Selected : PixelCue::None);
  }
}

auto Solitaire::draw_empty(termforge::Screen& screen, int x, int y,
                           std::string_view label, bool selected,
                           bool valid_target) -> void {
  const bool ascii =
      m_ctx == nullptr || termforge::is_ascii(m_ctx->border_style());
  const auto bg = valid_target ? kValidBg : termforge::theme::kBg;
  const char left = selected ? '[' : (valid_target ? '<' : '+');
  const char right = selected ? ']' : (valid_target ? '>' : '+');
  const int inner = m_layout.card_cols - 2;
  const std::string top =
      (selected || valid_target ? std::string(1, left)
                                : (ascii ? "+" : "\u256d")) +
      repeated(selected || valid_target || ascii ? "." : "\u2504", inner) +
      (selected || valid_target ? std::string(1, right)
                                : (ascii ? "+" : "\u256e"));
  const std::string bottom =
      (selected || valid_target ? std::string(1, left)
                                : (ascii ? "+" : "\u2570")) +
      repeated(selected || valid_target || ascii ? "." : "\u2504", inner) +
      (selected || valid_target ? std::string(1, right)
                                : (ascii ? "+" : "\u256f"));
  screen.write_text(x, y, top, termforge::theme::kDim, bg);
  for (int row = 1; row < m_layout.card_rows - 1; ++row) {
    screen.write_text(x, y + row,
                      centred(ascii ? "|" : "\u2502",
                              row == m_layout.card_rows / 2 ? label : "",
                              ascii ? "|" : "\u2502", m_layout.card_cols),
                      termforge::theme::kDim, bg);
  }
  screen.write_text(x, y + m_layout.card_rows - 1, bottom,
                    termforge::theme::kDim, bg);
}

auto Solitaire::add_pixel(termforge::Rect region, const Card& card, bool full,
                          PixelCue cue) -> void {
  if (!m_art_ready) return;
  m_pixel_cards.push_back(
      {.region = region,
       .id = solitaire::card_id(card),
       .face_up = card.face_up,
       .full = full,
       .cue = cue});
}

auto Solitaire::pixel_regions() -> std::vector<termforge::Rect> {
  std::vector<termforge::Rect> regions;
  regions.reserve(m_pixel_cards.size());
  for (const PixelCard& card : m_pixel_cards)
    regions.push_back(card.region);
  return regions;
}

auto Solitaire::draw_pixels(termforge::Rect region, termforge::Extent preferred)
    -> const termforge::Image* {
  const auto found =
      std::ranges::find_if(m_pixel_cards, [&](const PixelCard& card) {
        return card.region == region;
      });
  if (found == m_pixel_cards.end() || found->id < 0 || found->id >= 52) {
    return nullptr;
  }
  if (preferred.w <= 0 || preferred.h <= 0) return nullptr;
  const int fan_height =
      found->full ? std::max(1, preferred.h / m_layout.card_rows) : preferred.h;
  const termforge::Extent full{
      preferred.w,
      found->full ? preferred.h : preferred.h * m_layout.card_rows};
  prepare_display_art(full, fan_height);
  const auto id = static_cast<std::size_t>(found->id);
  const termforge::Image* base = nullptr;
  if (found->full) {
    base = found->face_up ? &m_front_display[id] : &m_back_display[id];
  } else {
    base = found->face_up ? &m_front_display_fan[id]
                          : &m_back_display_fan[id];
  }
  if (found->cue == PixelCue::None) return base;

  const auto cued = std::ranges::find_if(m_cued_images, [&](const CuedImage& c) {
    return c.id == found->id && c.face_up == found->face_up &&
           c.full == found->full && c.cue == found->cue;
  });
  if (cued != m_cued_images.end()) return &cued->image;

  CuedImage made{.id = found->id,
                 .face_up = found->face_up,
                 .full = found->full,
                 .cue = found->cue,
                 .image = *base};
  const termforge::Pixel cue =
      found->cue == PixelCue::Selected ? kSelectedPixel : kValidPixel;
  const int thickness = std::max(1, std::min(made.image.width(),
                                             made.image.height()) /
                                         24);
  made.image.fill({0, 0, made.image.width(), thickness}, cue);
  made.image.fill(
      {0, made.image.height() - thickness, made.image.width(), thickness}, cue);
  made.image.fill({0, 0, thickness, made.image.height()}, cue);
  made.image.fill(
      {made.image.width() - thickness, 0, thickness, made.image.height()}, cue);
  m_cued_images.push_back(std::move(made));
  return &m_cued_images.back().image;
}

auto Solitaire::pixel_placement(termforge::Rect) const noexcept
    -> termforge::ImagePlacementOptions {
  termforge::ImagePlacementOptions options{
      .fit = termforge::PlacementFit::Stretch,
      .layer = m_use_native ? termforge::ImageLayer::below_text()
                            : termforge::ImageLayer{}};
  return options;
}

auto Solitaire::draw_status(termforge::Screen& screen) -> void {
  std::string state = m_board.won() ? "YOU WIN" : "PLAYING";
  const std::array<std::string, 4> fields{
      "score " + number(m_board.score()), "best " + best_score(),
      "moves " + number(m_board.moves()), "time " + elapsed_text(m_elapsed)};
  hud::draw_status_row(
      screen, m_layout.status_y, fields, state, termforge::theme::kFg,
      m_board.won() ? termforge::Rgb{0xF5, 0x9E, 0x0B} : termforge::theme::kDim,
      termforge::theme::kBg, "   ", m_layout.frame_x + 1, m_layout.frame_w - 2);
}

auto Solitaire::draw_hints(termforge::Screen& screen) -> void {
  constexpr hud::Tier tiers[]{
      {70, "Arrows move  Enter select/drop  F foundation  A auto  U undo  N "
           "new  Esc menu"},
      {43, "Arrows  Enter  F foundation  A auto  U undo  N new"},
      {0, "Enter select  U undo  N new"},
  };
  static_assert(hud::tiers_are_total(tiers));
  screen.write_text(m_layout.frame_x + 1, m_layout.hint_y,
                    hud::pick_for_width(m_layout.frame_w - 2, tiers),
                    termforge::theme::kDim, termforge::theme::kBg);
}

auto Solitaire::draw_too_small(termforge::Screen& screen) -> void {
  screen.write_text(0, 0, "Solitaire needs 43x24", termforge::theme::kFg,
                    termforge::theme::kBg);
  if (screen.rows() > 1) {
    screen.write_text(0, 1, "Resize or Esc for menu", termforge::theme::kDim,
                      termforge::theme::kBg);
  }
}

} // namespace glyphcade
