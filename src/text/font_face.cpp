#include "font_face.hpp"

#include "shaper.hpp"

#include <cmath>

namespace continui::detail {

FaceImpl::FaceImpl(const FT_Library library, const FT_Face face)
    : library(library), face(face) {
    family = face->family_name != nullptr ? face->family_name : "";
    bold = (face->style_flags & FT_STYLE_FLAG_BOLD) != 0;
    units_per_em = face->units_per_EM > 0 ? face->units_per_EM : 1000;
}

FaceImpl::~FaceImpl() {
    if (hb_face != nullptr) {
        hb_face_destroy(hb_face);
    }
    if (face != nullptr) {
        FT_Done_Face(face);
    }
}

hb_face_t* FaceImpl::harfbuzz_face() {
    if (hb_face == nullptr) {
        hb_face = make_shaping_face(*this);
    }
    return hb_face;
}

std::optional<GlyphBitmap> rasterize_glyph(FaceImpl& impl,
                                            const std::uint32_t glyph_id,
                                            const float size_px,
                                            const float phase_x) {
    if (impl.face == nullptr || !(size_px > 0.0f) || size_px > 512.0f) {
        return std::nullopt;
    }
    // Fractional ppem via 26.6 (char size), not the integer-only pixel-size
    // API: font sizes in styles are not necessarily whole pixels.
    if (FT_Set_Char_Size(impl.face, 0,
                         static_cast<FT_F26Dot6>(std::lround(size_px * 64.0f)),
                         0, 0) != 0) {
        return std::nullopt;
    }
    // Translating the outline before rendering puts the rasterization at the
    // right subpixel phase. No hinting: hinted advances would fight the
    // unhinted shaping advances, and hinted bitmaps would ignore the phase.
    FT_Vector delta{static_cast<FT_Pos>(std::lround(phase_x * 64.0f)), 0};
    FT_Set_Transform(impl.face, nullptr, &delta);  // null matrix = unchanged
    constexpr FT_Int32 kLoadFlags = FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP
                                    | FT_LOAD_NO_SVG;
    if (FT_Load_Glyph(impl.face, glyph_id, kLoadFlags) != 0) {
        return std::nullopt;
    }
    const FT_GlyphSlot slot = impl.face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE) {
        return std::nullopt;
    }
    if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
        return std::nullopt;
    }

    GlyphBitmap out;
    out.width = static_cast<int>(slot->bitmap.width);
    out.rows = static_cast<int>(slot->bitmap.rows);
    if (out.width <= 0 || out.rows <= 0) {
        return out;  // blank glyph (e.g. space): cached as "draw nothing"
    }
    if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
        return std::nullopt;
    }
    // FreeType reports bitmap_left in pen space for the TRANSFORMED outline
    // (i.e. floor(bearing + phase)). Removing the phase we injected yields
    // the exact pen-relative edge: the caller places the quad at
    // pen_x + left, while the coverage keeps the subpixel phase.
    out.left = static_cast<float>(slot->bitmap_left) - phase_x;
    out.top = static_cast<float>(slot->bitmap_top);

    out.rgba.resize(static_cast<std::size_t>(out.width) * out.rows * 4);
    for (int y = 0; y < out.rows; ++y) {
        const std::uint8_t* src = slot->bitmap.buffer
                                  + static_cast<std::size_t>(y) * slot->bitmap.pitch;
        std::uint8_t* dst = out.rgba.data()
                            + static_cast<std::size_t>(y) * out.width * 4;
        for (int x = 0; x < out.width; ++x) {
            dst[x * 4 + 0] = 255;
            dst[x * 4 + 1] = 255;
            dst[x * 4 + 2] = 255;
            dst[x * 4 + 3] = src[x];
        }
    }
    return out;
}

}  // namespace continui::detail
