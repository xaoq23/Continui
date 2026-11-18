// Internal FreeType/HarfBuzz face state (Phase 0.5, ADR-010). Not a public
// header: FT/hb types stay behind FontFace's pimpl.
#pragma once

#include <continui/font.hpp>

#include "glyph_bitmap.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>

#include <cstdint>
#include <optional>
#include <string>

namespace continui::detail {

// Everything third-party behind the public FontFace. The FreeType library
// is borrowed from FontManager, which destroys all faces before the library.
struct FaceImpl {
    FaceImpl(FT_Library library, FT_Face face);
    ~FaceImpl();
    FaceImpl(const FaceImpl&) = delete;
    FaceImpl& operator=(const FaceImpl&) = delete;

    // Created on first shape() from the font's SFNT tables.
    hb_face_t* harfbuzz_face();

    FT_Library library = nullptr;
    FT_Face face = nullptr;
    hb_face_t* hb_face = nullptr;
    std::string family;
    bool bold = false;
    int units_per_em = 0;
};

// Rasterizes a glyph at size_px with the outline shifted by phase_x px
// (a quarter-pixel bucket). Unhinted outlines keep advances and positions
// exact; the phase lives in the coverage and is compensated in `left`.
// Blank glyphs return an empty bitmap; failures return nullopt.
[[nodiscard]] std::optional<GlyphBitmap> rasterize_glyph(
    FaceImpl& impl, std::uint32_t glyph_id, float size_px, float phase_x);

}  // namespace continui::detail
