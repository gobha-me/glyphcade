// The complete first asset path: committed PNG bytes -> glyphcade decoder ->
// TermForge Image -> the same App frame over Baseline, ANSI raster and Kitty
// native graphics. The test owns the proof widget because #8 is infrastructure,
// not permission to add selector chrome or a placeholder game.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <glyphcade/assets/png.hpp>
#include <glyphcade/generated_assets/proof_card_back.hpp>

#include <termforge/core/app.hpp>
#include <termforge/core/screen.hpp>
#include <termforge/core/types.hpp>
#include <termforge/drivers/ansi_rgb_driver.hpp>
#include <termforge/drivers/kitty_driver.hpp>
#include <termforge/widgets/theme.hpp>
#include <termforge/widgets/widget.hpp>

namespace {

using glyphcade::assets::decode_png;
using glyphcade::assets::embedded::proof_card_back;
using termforge::AnsiRgbDriver;
using termforge::App;
using termforge::ErrorEvent;
using termforge::Extent;
using termforge::Image;
using termforge::KittyDriver;
using termforge::Pixel;
using termforge::Rect;
using termforge::Rgb;
using termforge::Screen;
using termforge::Widget;

constexpr std::array<std::byte, 8> kPngSignature{
    std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47},
    std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};

auto bytes_of(std::string_view text) -> std::span<const std::byte> {
  return std::as_bytes(std::span{text.data(), text.size()});
}

struct ProofWidget final : Widget {
  explicit ProofWidget(Image image)
      : m_image(std::move(image)),
        m_ansi_image(m_image.width(), m_image.height(),
                     std::vector<Pixel>(m_image.pixels().size(),
                                        Pixel{1, 19, 29, 255})) {
    // ANSI's half-block transport has no alpha channel. Composite the same
    // decoded art over its authored navy surround before handing it to that
    // tier; Kitty keeps the original transparent RGBA.
    m_ansi_image.blend(m_image, 0, 0);
  }

  auto draw(Screen &screen) -> void override {
    ++cell_calls;
    screen.write_text(0, 0, "+---+", termforge::theme::kFg,
                      termforge::theme::kBg);
    screen.write_text(0, 1, "|###|", termforge::theme::kFg,
                      termforge::theme::kBg);
    screen.write_text(0, 2, "+---+", termforge::theme::kFg,
                      termforge::theme::kBg);
  }

  auto pixel_regions() -> std::vector<Rect> override { return {kRegion}; }

  auto draw_pixels(Rect region, Extent preferred) -> const Image * override {
    ++pixel_calls;
    last_region = region;
    last_preferred = preferred;
    if (preferred == Extent{5, 6})
      return &m_ansi_image;
    return &m_image;
  }

  static constexpr Rect kRegion{0, 0, 5, 3};
  int cell_calls{0};
  int pixel_calls{0};
  Rect last_region{};
  Extent last_preferred{};

private:
  Image m_image;
  Image m_ansi_image;
};

class ProofApp final : public App {
public:
  explicit ProofApp(Image image) : proof(std::move(image)) {}

  auto run_baseline() -> void { test_run_frames(1, 8, 5, &m_wire); }
  auto run_ansi() -> void {
    test_run_frames(1, 8, 5, &m_wire, std::make_unique<AnsiRgbDriver>());
  }
  auto run_kitty() -> void {
    test_run_frames(1, 8, 5, &m_wire, std::make_unique<KittyDriver>());
  }

  [[nodiscard]] auto wire() const noexcept -> const std::string & {
    return m_wire;
  }

  ProofWidget proof;

protected:
  auto on_render(Screen &screen) -> void override {
    screen.clear();
    proof.draw(screen);
    render_pixel_regions(proof);
  }

  auto on_event(const termforge::Event &event) -> void override {
    if (const auto *error = std::get_if<ErrorEvent>(&event)) {
      errors.push_back(*error);
      return;
    }
    App::on_event(event);
  }

  [[nodiscard]] auto now_steady() const
      -> std::chrono::steady_clock::time_point override {
    return m_now;
  }
  auto wait_readable(int timeout_ms) -> bool override {
    m_now += std::chrono::milliseconds(timeout_ms);
    return false;
  }
  auto read_available(char *, int) -> int override { return 0; }

private:
  std::chrono::steady_clock::time_point m_now{};
  std::string m_wire;
  std::vector<ErrorEvent> errors;
};

auto decoded_proof() -> Image {
  auto result = decode_png(proof_card_back());
  REQUIRE(result.has_value());
  return std::move(*result);
}

} // namespace

TEST_CASE("the embedded proof preserves the complete PNG datastream",
          "[assets][embed]") {
  const auto bytes = proof_card_back();
  REQUIRE(bytes.size() == 1597673);
  CHECK(std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin()));
}

TEST_CASE("the proof PNG decodes to its authored RGBA pixels",
          "[assets][png]") {
  const Image image = decoded_proof();
  CHECK(image.width() == 1024);
  CHECK(image.height() == 1536);
  CHECK(image.pixels().size() == 1024U * 1536U);
  // Fully transparent pixels retain authored RGB; alpha is the contract.
  CHECK(image.at(0, 0) == Pixel{1, 19, 29, 0});
  CHECK(image.at(512, 768) == Pixel{123, 53, 239, 253});
}

TEST_CASE("transparent and patterned pixels compose over an opaque table",
          "[assets][png][blend]") {
  const Image image = decoded_proof();
  Image table{1, 1, std::vector<Pixel>{Pixel{10, 20, 30, 255}}};

  table.blend(image, Rect{0, 0, 1, 1}, 0, 0);
  CHECK(table.at(0, 0) == Pixel{10, 20, 30, 255});

  table.blend(image, Rect{512, 768, 1, 1}, 0, 0);
  CHECK(table.at(0, 0) == Pixel{122, 53, 237, 255});
}

TEST_CASE("PNG failures are events rather than partial images",
          "[assets][png]") {
  SECTION("empty") {
    const auto result = decode_png({});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().source == "png_decoder");
    CHECK(result.error().severity == termforge::Severity::Warning);
    CHECK(result.error().message.find("empty") != std::string::npos);
  }

  SECTION("not a PNG") {
    const auto result = decode_png(bytes_of("not a png"));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().source == "png_decoder");
    CHECK(result.error().message.find("invalid PNG") != std::string::npos);
  }

  SECTION("truncated") {
    const auto bytes = proof_card_back().first(24);
    const auto result = decode_png(bytes);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().source == "png_decoder");
  }

  SECTION("dimension ceiling") {
    std::vector<std::byte> bytes(proof_card_back().begin(),
                                 proof_card_back().end());
    // PNG IHDR width is a big-endian u32 at bytes 16..19. stb_info does not
    // validate CRCs, which lets this fixture reach OUR ceiling without needing
    // a second binary blob or a PNG encoder in the test.
    bytes[16] = std::byte{0x00};
    bytes[17] = std::byte{0x00};
    bytes[18] = std::byte{0x10};
    bytes[19] = std::byte{0x01}; // 4097
    const auto result = decode_png(bytes);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("too large") != std::string::npos);
  }
}

TEST_CASE("Baseline keeps the authored information-complete card",
          "[assets][fidelity]") {
  ProofWidget proof{decoded_proof()};
  Screen screen{5, 3};
  proof.draw(screen);

  CHECK(screen.text_at(0, 0) == "+");
  CHECK(screen.text_at(2, 1) == "#");
  CHECK(screen.text_at(4, 2) == "+");

  ProofApp app{decoded_proof()};
  app.run_baseline();
  CHECK(app.proof.cell_calls == 1);
  CHECK(app.proof.pixel_calls == 0);
  CHECK(std::all_of(app.wire().begin(), app.wire().end(),
                    [](unsigned char ch) { return ch < 0x80; }));
  CHECK(app.wire().find("\x1b_G") == std::string::npos);
}

TEST_CASE("ANSI truecolour rasters the same decoded card through half-blocks",
          "[assets][fidelity]") {
  ProofApp app{decoded_proof()};
  app.run_ansi();

  CHECK(app.proof.cell_calls == 1);
  CHECK(app.proof.pixel_calls == 1);
  CHECK(app.proof.last_region == ProofWidget::kRegion);
  CHECK(app.proof.last_preferred == Extent{5, 6});
  CHECK(app.wire().find("\x1b[38;2;") != std::string::npos);
  CHECK(app.wire().find("\x1b[48;2;") != std::string::npos);
  CHECK(app.wire().find("\xE2\x96\x80") != std::string::npos); // U+2580
  CHECK(app.wire().find("\x1b_G") == std::string::npos);
}

TEST_CASE("Kitty receives a native graphics transmission and placement",
          "[assets][fidelity]") {
  ProofApp app{decoded_proof()};
  app.run_kitty();

  CHECK(app.proof.cell_calls == 1);
  CHECK(app.proof.pixel_calls == 1);
  CHECK(app.proof.last_region == ProofWidget::kRegion);
  CHECK(app.proof.last_preferred.w > ProofWidget::kRegion.w);
  CHECK(app.proof.last_preferred.h > ProofWidget::kRegion.h);
  CHECK(app.wire().find("\x1b_G") != std::string::npos);
  CHECK(app.wire().find("f=32") != std::string::npos);
  CHECK(app.wire().find("a=t") != std::string::npos);
  CHECK(app.wire().find("c=5") != std::string::npos);
  CHECK(app.wire().find("r=3") != std::string::npos);
}
