#pragma once

// glyphcade -- PNG decode at the application boundary.
//
// TermForge deliberately accepts RGBA pixels rather than depending on an image
// codec. glyphcade is the application and owns that policy decision: committed
// PNG art is embedded as bytes, decoded once outside the render loop, and then
// handed to TermForge as an Image. No filesystem or network access is hidden in
// this function.

#include <cstddef>
#include <expected>
#include <span>

#include <termforge/core/types.hpp>

namespace glyphcade::assets {

// Match TermForge's raw image loader ceiling. The shared bound means a PNG and
// its decoded/raw equivalent are accepted or rejected on the same dimensions.
inline constexpr int kMaxPngDimension = 4096;

// Decode one complete PNG datastream from memory into 8-bit RGBA pixels.
//
// Every data/format failure is an ErrorEvent{Warning, "png_decoder", ...}:
// empty or oversized input, non-PNG/corrupt data, invalid dimensions, or an
// image beyond kMaxPngDimension. Allocation failure remains the ordinary C++
// exception it is everywhere else in the project.
[[nodiscard]] auto decode_png(std::span<const std::byte> bytes)
    -> std::expected<termforge::Image, termforge::ErrorEvent>;

} // namespace glyphcade::assets
