#include <glyphcade/assets/png.hpp>

#include <climits>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <vector>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace glyphcade::assets {
namespace {

auto failure(std::string message)
    -> std::expected<termforge::Image, termforge::ErrorEvent> {
  return std::unexpected{termforge::ErrorEvent{
      termforge::Severity::Warning, "png_decoder", std::move(message)}};
}

struct StbiFree {
  auto operator()(stbi_uc *pixels) const noexcept -> void {
    stbi_image_free(pixels);
  }
};

} // namespace

auto decode_png(std::span<const std::byte> bytes)
    -> std::expected<termforge::Image, termforge::ErrorEvent> {
  if (bytes.empty())
    return failure("empty PNG data");
  if (bytes.size() > static_cast<std::size_t>(INT_MAX)) {
    return failure(
        std::format("PNG data is too large: {} bytes", bytes.size()));
  }

  const auto *input =
      reinterpret_cast<const stbi_uc *>(bytes.data()); // stb's byte currency
  const int length = static_cast<int>(bytes.size());

  int width = 0;
  int height = 0;
  int channels = 0;
  if (stbi_info_from_memory(input, length, &width, &height, &channels) == 0) {
    const char *reason = stbi_failure_reason();
    return failure(std::format("invalid PNG: {}",
                               reason != nullptr ? reason : "unknown error"));
  }
  if (width <= 0 || height <= 0) {
    return failure(std::format("invalid PNG dimensions: {}x{}", width, height));
  }
  if (width > kMaxPngDimension || height > kMaxPngDimension) {
    return failure(std::format("PNG dimensions too large: {}x{} (max {})",
                               width, height, kMaxPngDimension));
  }

  std::unique_ptr<stbi_uc, StbiFree> decoded{stbi_load_from_memory(
      input, length, &width, &height, &channels, STBI_rgb_alpha)};
  if (!decoded) {
    const char *reason = stbi_failure_reason();
    return failure(std::format("PNG decode failed: {}",
                               reason != nullptr ? reason : "unknown error"));
  }

  const auto count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<termforge::Pixel> pixels;
  pixels.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t p = i * 4;
    pixels.push_back(termforge::Pixel{decoded.get()[p], decoded.get()[p + 1],
                                      decoded.get()[p + 2],
                                      decoded.get()[p + 3]});
  }

  return termforge::Image{width, height, std::move(pixels)};
}

} // namespace glyphcade::assets
