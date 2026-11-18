// Rasterized glyph value type shared by the rasterizer and the cache.
// FreeType-free so consumers of the strike cache never see FT headers.
#pragma once

#include <cstdint>
#include <vector>

namespace continui::detail {

// One rasterized glyph: straight-alpha white mask bytes, ready to blit into
// the RGBA atlas. `left` is the pen-to-bitmap-left distance with the
// subpixel phase removed; `top` is baseline-to-bitmap-top (upward).
struct GlyphBitmap {
    int width = 0;
    int rows = 0;
    float left = 0;
    float top = 0;
    std::vector<std::uint8_t> rgba;  // width * rows * 4: (255,255,255,coverage)

    [[nodiscard]] bool empty() const noexcept {
        return width <= 0 || rows <= 0 || rgba.empty();
    }
};

}  // namespace continui::detail
