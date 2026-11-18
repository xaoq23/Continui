// HarfBuzz shaping, isolated from the rest of the text module (ADR-010).
#pragma once

#include "font_face.hpp"

#include <continui/glyph_run.hpp>

#include <string_view>

namespace continui::detail {

// Builds an hb_face that reads SFNT tables through FreeType (no HarfBuzz
// FreeType integration: no reverse dependency, no linkage cycle).
[[nodiscard]] hb_face_t* make_shaping_face(FaceImpl& impl);

// Shapes UTF-8 into a GlyphRun. Phase 0.5: one latin LTR run, font units
// converted to y-down float pixels; no fallback (missing glyphs are 0).
[[nodiscard]] GlyphRun shape_text(const FontFace& public_face,
                                  const FaceImpl& impl, std::string_view utf8,
                                  float size_px);

}  // namespace continui::detail
