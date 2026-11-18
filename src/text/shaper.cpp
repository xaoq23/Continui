#include "shaper.hpp"

#include <ft2build.h>
#include FT_TRUETYPE_TABLES_H  // FT_Load_Sfnt_Table: HarfBuzz reads tables via FreeType
#include <hb.h>

#include <cstdlib>

namespace continui::detail {
namespace {

// HarfBuzz owns the returned blob, so the table bytes are copied into an
// independently owned buffer (they must outlive this callback).
hb_blob_t* reference_table(hb_face_t*, const hb_tag_t tag, void* userdata) {
    FT_Face face = static_cast<FT_Face>(userdata);
    FT_ULong length = 0;
    if (face == nullptr || FT_Load_Sfnt_Table(face, tag, 0, nullptr, &length) != 0
        || length == 0) {
        return hb_blob_get_empty();
    }
    auto* bytes = static_cast<std::uint8_t*>(std::malloc(length));
    if (bytes == nullptr) {
        return hb_blob_get_empty();
    }
    FT_ULong copied = length;
    if (FT_Load_Sfnt_Table(face, tag, 0, bytes, &copied) != 0) {
        std::free(bytes);
        return hb_blob_get_empty();
    }
    return hb_blob_create(reinterpret_cast<const char*>(bytes),
                          static_cast<unsigned>(copied), HB_MEMORY_MODE_READONLY,
                          bytes, [](void* p) { std::free(p); });
}

}  // namespace

hb_face_t* make_shaping_face(FaceImpl& impl) {
    hb_face_t* face = hb_face_create_for_tables(&reference_table, impl.face, nullptr);
    // Use FreeType's upem so shaping and metrics agree even for fonts whose
    // head table is damaged or missing.
    hb_face_set_upem(face, static_cast<unsigned>(impl.units_per_em));
    return face;
}

GlyphRun shape_text(const FontFace& public_face, const FaceImpl& impl,
                    const std::string_view utf8, const float size_px) {
    GlyphRun run;
    run.face = &public_face;
    run.size_px = size_px;
    if (size_px <= 0.0f || utf8.empty() || impl.hb_face == nullptr) {
        return run;
    }

    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, utf8.data(), static_cast<int>(utf8.size()), 0, -1);
    hb_buffer_guess_segment_properties(buffer);  // latin => LTR

    hb_font_t* font = hb_font_create(impl.hb_face);
    // Scale 1 font unit -> 1 hb unit: all shaping arithmetic then happens
    // in font units, converted to pixels once, at the end.
    hb_font_set_scale(font, impl.units_per_em, impl.units_per_em);
    hb_shape(font, buffer, nullptr, 0);

    unsigned count = 0;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
    const float px_per_unit = size_px / static_cast<float>(impl.units_per_em);
    run.glyphs.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        Glyph glyph;
        glyph.id = infos[i].codepoint;
        glyph.advance = static_cast<float>(positions[i].x_advance) * px_per_unit;
        glyph.offset_x = static_cast<float>(positions[i].x_offset) * px_per_unit;
        glyph.offset_y = -static_cast<float>(positions[i].y_offset) * px_per_unit;
        run.width += glyph.advance;
        run.glyphs.push_back(glyph);
    }

    hb_font_destroy(font);
    hb_buffer_destroy(buffer);
    return run;
}

}  // namespace continui::detail
