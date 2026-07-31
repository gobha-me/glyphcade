#include <termgame/arcade/options_screen.hpp>

#include <algorithm>
#include <string>
#include <variant>

#include <termforge/widgets/detail/width.hpp>
#include <termforge/widgets/glyphs.hpp>
#include <termforge/widgets/theme.hpp>

#include <termgame/arcade/hud.hpp>

namespace termgame {
namespace {

// ⚠ Widest first, zero floor last — hud::tiers_are_total() is what enforces
// that, and it is static_asserted below rather than trusted.
//
// ⚠ The narrowest tier has to fit in 20 columns, which is Shell::kMinCols and
// therefore the smallest screen draw() can be handed. "Enter start" is 11.
constexpr hud::Tier kHintTiers[]{
    {56, "Up/Down pick  Left/Right change  Enter start  Esc menu"},
    {34, "Up/Down  Left/Right  Enter start"},
    {0, "Enter start"},
};
static_assert(hud::tiers_are_total(kHintTiers),
              "the hint tiers must be ordered widest-first and end at a zero "
              "floor, or pick_for_width can return an empty row");

constexpr hud::Tier kListHintTiers[]{
    {56, "Up/Down pick  Enter start  Esc menu"},
    {34, "Up/Down  Enter start"},
    {0, "Enter start"},
};
static_assert(hud::tiers_are_total(kListHintTiers), "same rule");

// Rows the screen spends on chrome before the first option: the title and one
// blank. Kept as a name because draw() and draw_list() must agree on it.
constexpr int kHeaderRows = 2;

}  // namespace

auto OptionsScreen::open(std::string_view title,
                         std::span<const OptionSpec> options, GameContext* ctx)
    -> void {
  m_title = title;
  m_options = options;
  m_ctx = ctx;
  m_row = 0;
  m_scroll = 0;

  for (std::size_t i = 0; i < options.size() && i < kMaxGameOptions; ++i) {
    m_choice[i] = options[i].default_index;
  }

  // ⚠ Empty span means no screen, not an empty screen. A game may call open()
  // unconditionally; 2048 does not call it at all, which is cheaper still.
  m_open = !options.empty();
}

auto OptionsScreen::preselect(std::size_t option, int choice) -> void {
  if (option >= m_options.size() || option >= kMaxGameOptions) return;
  const int last = static_cast<int>(m_options[option].choices.size()) - 1;
  m_choice[option] = std::clamp(choice, 0, last);
}

auto OptionsScreen::selected(std::size_t option) const noexcept -> int {
  if (option >= m_options.size() || option >= kMaxGameOptions) return 0;
  return m_choice[option];
}

auto OptionsScreen::is_list_mode() const noexcept -> bool {
  return m_options.size() == 1 &&
         m_options[0].choices.size() > kInlineChoiceMax;
}

auto OptionsScreen::click() -> void {
  if (m_ctx != nullptr) m_ctx->audio().play(audio::SfxId::MenuMove);
}

auto OptionsScreen::move_row(int delta) -> Reply {
  // ⚠ In list mode there is exactly one option, and Up/Down move the CHOICE
  // rather than the row — otherwise the twenty-level picker would have no way
  // to pick anything, since its rows are the choices.
  if (is_list_mode()) return move_choice(delta);

  const int count = static_cast<int>(m_options.size());
  const int next = std::clamp(static_cast<int>(m_row) + delta, 0, count - 1);
  // ⚠ Edge-detected: only a move that actually moved makes a sound. Holding
  // Down at the last option otherwise machine-guns the sfx ring, which is the
  // same rule Shell::handle_selector_key already applies to the menu.
  if (next == static_cast<int>(m_row)) return Reply::Consumed;
  m_row = static_cast<std::size_t>(next);
  click();
  return Reply::Consumed;
}

auto OptionsScreen::move_choice(int delta) -> Reply {
  if (m_row >= m_options.size() || m_row >= kMaxGameOptions) {
    return Reply::Consumed;
  }
  const int last = static_cast<int>(m_options[m_row].choices.size()) - 1;
  const int next = std::clamp(m_choice[m_row] + delta, 0, last);
  if (next == m_choice[m_row]) return Reply::Consumed;
  m_choice[m_row] = next;
  click();
  return Reply::Consumed;
}

auto OptionsScreen::on_event(const termforge::Event& ev) -> Reply {
  if (!m_open) return Reply::Ignored;

  const auto* key = std::get_if<termforge::KeyEvent>(&ev);
  if (key == nullptr) return Reply::Ignored;

  // ⚠ See the header. This is the Tetris/kitty trap, and it is unreachable in
  // this container — do not remove it because everything is green.
  if (key->action == termforge::KeyAction::Release) return Reply::Ignored;

  switch (key->key) {
    case termforge::Key::Up:
      return move_row(-1);
    case termforge::Key::Down:
      return move_row(+1);
    case termforge::Key::Left:
      return move_choice(-1);
    case termforge::Key::Right:
      return move_choice(+1);
    case termforge::Key::Enter:
      m_open = false;
      return Reply::Dismissed;
    // ⚠ Escape is NOT handled — it falls through to Ignored so the Shell's
    // quit-to-menu still works from here.
    default:
      break;
  }

  if (key->key != termforge::Key::Char) return Reply::Ignored;

  switch (key->ch) {
    case U'k':
    case U'K':
      return move_row(-1);
    case U'j':
    case U'J':
      return move_row(+1);
    case U'h':
    case U'H':
      return move_choice(-1);
    case U'l':
    case U'L':
      return move_choice(+1);
    case U' ':
      m_open = false;
      return Reply::Dismissed;
    default:
      // ⚠ Everything else — including 'p' — goes back to the caller, which
      // returns false, which lets the Shell pause. A game's own in-game keys
      // are not bound here either: they belong to a game that has not started.
      return Reply::Ignored;
  }
}

auto OptionsScreen::draw_cycler(termforge::Screen& screen, std::size_t i, int y,
                               int cols, bool ascii) -> void {
  const OptionSpec& opt = m_options[i];
  const bool here = (i == m_row);
  const auto fg = here ? termforge::theme::kFg : termforge::theme::kDim;
  const auto bg = termforge::theme::kBg;

  const auto marks = termforge::mark_glyphs(
      ascii ? termforge::BorderStyle::Ascii : termforge::BorderStyle::Rounded);
  // ⚠ The marker, not colour, is what says which row is live. FallbackDriver
  // discards colour, so at the bottom tier a highlighted row that is only
  // highlighted by fg is not highlighted at all.
  const std::string_view mark = here ? marks.selector : " ";

  const std::string_view value = opt.choices[static_cast<std::size_t>(
      std::clamp(m_choice[i], 0,
                 static_cast<int>(opt.choices.size()) - 1))];

  // `< value >` — and the arrows are only drawn where there is somewhere to go,
  // so the row also says whether the value is at an end. Clamping is invisible
  // otherwise.
  const bool can_left = m_choice[i] > 0;
  const bool can_right =
      m_choice[i] < static_cast<int>(opt.choices.size()) - 1;

  std::string row;
  row += mark;
  row += " ";
  row += std::string(opt.label);
  row += ": ";
  row += can_left ? "<" : " ";
  row += " ";
  row += std::string(value);
  row += " ";
  row += can_right ? ">" : " ";

  // ⚠ truncate_to_width, not substr: `mark` may be a two-column glyph at the
  // Unicode tier, so byte length and column count are different numbers. This
  // is the same sanctioned helper icon_is_safe() uses.
  screen.write_text(0, y, termforge::detail::truncate_to_width(row, cols), fg,
                    bg);
}

auto OptionsScreen::draw_list(termforge::Screen& screen, int top, int rows,
                              int cols, bool ascii) -> void {
  const OptionSpec& opt = m_options[0];
  const auto bg = termforge::theme::kBg;
  const int count = static_cast<int>(opt.choices.size());

  const auto marks = termforge::mark_glyphs(
      ascii ? termforge::BorderStyle::Ascii : termforge::BorderStyle::Rounded);

  screen.write_text(0, top, termforge::detail::truncate_to_width(
                                std::string(opt.label) + ":", cols),
                    termforge::theme::kDim, bg);

  const int list_top = top + 1;
  const int visible = std::max(0, rows - (list_top - top) - 1);
  if (visible <= 0) return;

  // Keep the cursor inside the window. Scrolling is recomputed from the cursor
  // every frame rather than tracked as its own state — one number that can be
  // wrong instead of two that can disagree.
  const int cur = std::clamp(m_choice[0], 0, count - 1);
  m_scroll = std::clamp(m_scroll, std::max(0, cur - visible + 1), cur);
  m_scroll = std::clamp(m_scroll, 0, std::max(0, count - visible));

  for (int r = 0; r < visible && m_scroll + r < count; ++r) {
    const int idx = m_scroll + r;
    const bool here = (idx == cur);
    std::string row;
    row += here ? std::string(marks.selector) : std::string(" ");
    row += " ";
    row += std::string(opt.choices[static_cast<std::size_t>(idx)]);
    screen.write_text(
        0, list_top + r, termforge::detail::truncate_to_width(row, cols),
        here ? termforge::theme::kFg : termforge::theme::kDim, bg);
  }
}

auto OptionsScreen::draw(termforge::Screen& screen) -> void {
  if (!m_open) return;

  const auto bg = termforge::theme::kBg;
  const int cols = screen.cols();
  const int rows = screen.rows();
  const bool ascii =
      m_ctx == nullptr || termforge::is_ascii(m_ctx->border_style());

  // ⚠ An EMPTY text, not " ". Cell::blank() keys off text.empty(), and a screen
  // full of spaces is not blank — it is a screen the diffing renderer must
  // repaint every frame.
  screen.clear(
      termforge::Cell{.text = "", .fg = termforge::theme::kFg, .bg = bg});

  screen.write_text(0, 0, termforge::detail::truncate_to_width(m_title, cols),
                    termforge::theme::kFg, bg);

  // ⚠ The hint row is the LAST row and is drawn whatever else fits, because it
  // is the only thing telling the player how to leave. It is placed before the
  // options are laid out so that a screen too short for any option still says
  // "Enter start" rather than nothing.
  const int hint_y = rows - 1;
  const std::span<const hud::Tier> tiers =
      is_list_mode() ? std::span<const hud::Tier>{kListHintTiers}
                     : std::span<const hud::Tier>{kHintTiers};
  if (hint_y > 0) {
    screen.write_text(0, hint_y, hud::pick_for_width(cols, tiers),
                      termforge::theme::kDim, bg);
  }

  // Rows between the header and the hint row. On a 20x8 screen that is five,
  // which is more than kMaxGameOptions — but a two-row terminal is not, and the
  // clamp below is what stops the options being written over the hint.
  const int body_rows = std::max(0, hint_y - kHeaderRows);
  if (body_rows <= 0) return;

  if (is_list_mode()) {
    draw_list(screen, kHeaderRows, body_rows, cols, ascii);
    return;
  }

  const int shown =
      std::min(body_rows, static_cast<int>(m_options.size()));
  for (int i = 0; i < shown; ++i) {
    draw_cycler(screen, static_cast<std::size_t>(i), kHeaderRows + i, cols,
                ascii);
  }
}

}  // namespace termgame
