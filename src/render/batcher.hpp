// BatchBuilder: turns recorded draw commands into vertex/index batches.
//
// Pure CPU data — no GPU types — so the Dawn backend is just one consumer
// and unit tests exercise the geometry directly. All quads that share a
// texture and a clip merge into one batch (one draw call); a texture or
// clip change starts a new one. Buffers are cleared per frame and keep
// their capacity (reuse-buffer interim; the frame arena arrives with the
// view system, ADR-005).
#pragma once

#include "text_context.hpp"

#include <continui/events.hpp>
#include <continui/geometry.hpp>
#include <continui/glyph_run.hpp>
#include <continui/painter.hpp>
#include <continui/style.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace continui::detail {

// Vertex layout consumed by the Dawn pipeline (64 bytes = 16 floats).
// Position/uv/color are what you would expect; the rect center/half-extents
// and per-corner radii feed the fragment-stage rounded-box SDF, so rounded
// corners cost zero extra vertices.
struct BatchVertex {
    float x, y;                       // pixel position
    float u, v;                       // texture coordinates
    float r, g, b, a;                 // premultiplied paint color
    float rect_cx, rect_cy, rect_hw, rect_hh;
    float radius_tl, radius_tr, radius_br, radius_bl;
};
static_assert(sizeof(BatchVertex) == 64);

// Which pipeline a batch is drawn with. Text quads must not run the shape
// SDF: sampling a white-mask glyph through a rounded-box distance field
// would fade the outermost glyph pixels twice. A kind change splits batches
// just like a texture/scissor change; pipeline switching costs one call.
enum class BatchKind : std::uint8_t { shape, text };

// One draw call worth of geometry: same texture, same scissor, same
// pipeline. Indices are frame-absolute vertex ids (0-based across the whole
// frame's vertex list), so a draw must pass firstIndex = index_offset and
// baseVertex = 0.
struct Batch {
    std::uint64_t texture_id = 0;     // 0 = the backend's solid-white texture
    Rect scissor{};                    // pixel clip active for this batch
    BatchKind kind = BatchKind::shape;
    std::uint32_t vertex_offset = 0;
    std::uint32_t index_offset = 0;
    std::uint32_t index_count = 0;
};

class BatchBuilder {
public:
    // Clears all buffers and resets the clip to the full viewport.
    void begin_frame(Size viewport);

    // Records one filled quad: 4 vertices + 6 indices (0,1,2 / 0,2,3).
    void rect(Rect bounds, const Paint& paint);
    void rounded_rect(Rect bounds, const Radius& radius, const Paint& paint);

    // Draws the whole texture stretched into bounds (no tint, no radius).
    // texture_id 0 is the backend's solid-white texture and is rejected
    // here; callers pass real handles only.
    void image(std::uint64_t texture_id, Rect bounds);

    // Records one quad per glyph of a shaped run, all sampling the same
    // glyph atlas (so a run usually becomes one draw call). The context
    // resolves glyph placements; runs with no face/glyphs record nothing.
    void text(const GlyphRun& run, PointF origin, const Paint& paint,
              TextRasterContext& atlas);

    // Closes the batch in progress so batches() covers the whole frame.
    // Call exactly once after the paint callback returns, before reading
    // the output — the last batch stays invisible until then.
    void finish_frame();

    // Replaces the clip (intersected with the viewport). Flushing happens
    // only when the clip actually changes; clipping to the same rect again
    // keeps the current batch running.
    void clip(Rect bounds);

    // Frame output, valid until the next begin_frame(). Batches never have
    // zero indices.
    [[nodiscard]] const std::vector<BatchVertex>& vertices() const noexcept {
        return vertices_;
    }
    [[nodiscard]] const std::vector<std::uint16_t>& indices() const noexcept {
        return indices_;
    }
    [[nodiscard]] const std::vector<Batch>& batches() const noexcept {
        return batches_;
    }

private:
    // Opens a batch on demand (merge key: texture + scissor + kind) and
    // appends one quad. Fully clipped or empty quads record nothing.
    void open_batch(std::uint64_t texture_id, BatchKind kind);
    void push_quad(const Rect& bounds, const Radius& radius, const Paint& paint,
                   std::uint64_t texture_id);
    void push_text_quad(const Rect& bounds, const Rect& uv,
                        const BatchVertex& tint_and_gamma,
                        std::uint64_t texture_id);
    // Closes the current batch at the current buffer positions.
    void flush();

    Size viewport_{};
    Rect scissor_{};

    // Batch in progress; the merge key change or end of frame flushes it.
    bool open_ = false;
    std::uint64_t texture_ = 0;
    Rect batch_scissor_{};
    BatchKind batch_kind_ = BatchKind::shape;
    std::uint32_t vertex_offset_ = 0;
    std::uint32_t index_offset_ = 0;
    std::uint32_t index_count_ = 0;

    std::vector<BatchVertex> vertices_;  // cleared per frame, capacity kept
    std::vector<std::uint16_t> indices_;
    std::vector<Batch> batches_;
};

}  // namespace continui::detail
