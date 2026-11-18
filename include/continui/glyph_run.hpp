// Shaped glyph runs: the text system's hand-off to the painter (ADR-010).
//
// Status: Phase 0.5. Plain value type — no FreeType/HarfBuzz/GPU types leak
// through this header. Shaping happens once (FontFace::shape); drawing a run
// is read-only and may happen every frame.
#pragma once

#include <cstdint>
#include <vector>

namespace continui {

class FontFace;  // defined in <continui/font.hpp>; owned by a FontManager

// One glyph placed by the shaper. All positions are float pixels in the
// drawing domain (x right, y down), already converted from font units at the
// requested pixel size. The id is a glyph index in the shaping face.
struct Glyph {
    std::uint32_t id = 0;
    float advance = 0;   // pen advance after drawing this glyph (px)
    float offset_x = 0;  // pen offset while drawing this glyph (px)
    float offset_y = 0;  // y-down convention (HarfBuzz's y-up is flipped)
};

// A shaped run of glyphs in visual order (Phase 0.5: single-run latin, LTR).
// The face pointer must stay alive while the run is drawn; faces returned by
// a FontManager have the manager's lifetime. A null face or empty glyph list
// draws nothing.
struct GlyphRun {
    const FontFace* face = nullptr;
    float size_px = 0;
    std::vector<Glyph> glyphs;
    float width = 0;  // sum of advances, px
};

}  // namespace continui
