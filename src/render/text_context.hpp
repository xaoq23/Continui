// Narrow, render-internal interface between the batcher and the backend's
// glyph atlas (Phase 0.5, ADR-010). The render module never sees FreeType,
// HarfBuzz, or Dawn: it only asks for a glyph's cached placement. The
// backend implements this interface and owns both atlas CPU data and GPU
// texture. Located beside batcher.hpp (same private include path).
#pragma once

#include <continui/geometry.hpp>

#include <cstdint>

namespace continui {
class FontFace;
}

namespace continui::detail {

// One glyph's placement in the atlas texture. UVs are NORMALIZED (0..1 over
// the current atlas texture), so the batcher needs no atlas-size knowledge.
// `left` is the pen-to-bitmap-left distance in pixels with the subpixel
// rasterization phase removed (the caller adds it back via the pen position);
// `top` is the baseline-to-bitmap-top distance, positive upward.
// width_px/height_px are the bitmap's size in PIXELS — deliberately separate
// from the NORMALIZED uv extents: a 10px glyph in a 256px atlas has uv.height
// ~0.04, which is NOT a usable quad size.
struct GlyphSlot {
    Rect uv{};           // normalized atlas rect: (u0, v0, width, height)
    float left = 0;      // px from the pen to the bitmap's left edge
    float top = 0;       // px from the baseline up to the bitmap's top edge
    float width_px = 0;  // bitmap width in pixels (never derive from uv!)
    float height_px = 0;

    [[nodiscard]] bool empty() const noexcept { return uv.empty(); }
};

class TextRasterContext {
public:
    virtual ~TextRasterContext() = default;

    // Resolves (face, size, x-subpixel bucket [0..3], glyph id) to an atlas
    // slot, rasterizing and packing on a miss. An empty slot means "draw
    // nothing" (blank glyph, missing glyph, or rasterization failure).
    [[nodiscard]] virtual GlyphSlot glyph(const FontFace& face, float size_px,
                                          int x_bucket,
                                          std::uint32_t glyph_id) = 0;

    // Texture id of the current atlas. Re-read after each glyph(): growth
    // rebuilds the atlas and may swap the id between draws.
    [[nodiscard]] virtual std::uint64_t atlas_texture() const = 0;
};

}  // namespace continui::detail
