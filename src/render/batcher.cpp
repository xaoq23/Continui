#include "batcher.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace continui::detail {
namespace {

// Four x-subpixel buckets (ADR-010): the phase baked into a cached glyph.
int subpixel_bucket(const float x) {
    const float fraction = x - std::floor(x);
    return std::clamp(static_cast<int>(fraction * 4.0f), 0, 3);
}

// Impeller (text_contents.cc + glyph_atlas.frag): contrast ramps from 1.0
// for black text to 1.0 + max for white by Rec.709 luminance; the shader
// applies coverage = 1 - (1 - coverage)^contrast. 1.2 is Impeller's
// FreeType coefficient; its 1.6 Windows value is for DirectWrite and
// belongs to a future native FontFace backend.
float text_contrast(const Color& color) {
    const float luma = (static_cast<float>(color.r) * 0.2126f
                        + static_cast<float>(color.g) * 0.7152f
                        + static_cast<float>(color.b) * 0.0722f) / 255.0f;
    constexpr float kMaxGammaCorrection = 1.2f;
    return 1.0f + luma * kMaxGammaCorrection;
}

}  // namespace

void BatchBuilder::begin_frame(const Size viewport) {
    viewport_ = viewport;
    scissor_ = Rect{0, 0, static_cast<float>(viewport.width),
                    static_cast<float>(viewport.height)};
    vertices_.clear();
    indices_.clear();
    batches_.clear();
    open_ = false;
    batch_kind_ = BatchKind::shape;
    index_count_ = 0;
}

void BatchBuilder::rect(const Rect bounds, const Paint& paint) {
    push_quad(bounds, Radius{}, paint, 0);
}

void BatchBuilder::rounded_rect(const Rect bounds, const Radius& radius,
                                const Paint& paint) {
    push_quad(bounds, radius, paint, 0);
}

void BatchBuilder::image(const std::uint64_t texture_id, const Rect bounds) {
    if (texture_id == 0) {
        return;
    }
    push_quad(bounds, Radius{}, Paint{}, texture_id);
}

void BatchBuilder::text(const GlyphRun& run, const PointF origin,
                        const Paint& paint, TextRasterContext& atlas) {
    if (run.face == nullptr || run.glyphs.empty() || scissor_.empty()) {
        return;
    }

    const float alpha = static_cast<float>(paint.color.a) / 255.0f;
    BatchVertex tint{};
    tint.r = static_cast<float>(paint.color.r) / 255.0f * alpha;
    tint.g = static_cast<float>(paint.color.g) / 255.0f * alpha;
    tint.b = static_cast<float>(paint.color.b) / 255.0f * alpha;
    tint.a = alpha;
    // The text fragment stage reads radius_tl as the contrast exponent;
    // the remaining SDF fields are unused by it (same 64-byte vertex).
    tint.radius_tl = text_contrast(paint.color);

    std::uint64_t texture_id = atlas.atlas_texture();
    float pen_x = origin.x;
    for (const Glyph& glyph : run.glyphs) {
        const float glyph_x = pen_x + glyph.offset_x;
        const float glyph_y = origin.y + glyph.offset_y;
        // A fallback run gives each glyph its own face; the batcher only ever
        // needs the face to resolve the atlas strike (0.7 fallback chain).
        const FontFace& face = glyph.face != nullptr ? *glyph.face : *run.face;
        const GlyphSlot slot = atlas.glyph(face, run.size_px,
                                           subpixel_bucket(glyph_x), glyph.id);
        // Growth can swap the atlas mid-run: re-read the id on every glyph.
        // Quads recorded before the swap keep the OLD id and UVs, which are
        // still valid in the old GPU texture (lifetime = renderer).
        const std::uint64_t current = atlas.atlas_texture();
        if (!slot.empty() && current != 0) {
            texture_id = current;
            const Rect bounds{glyph_x + slot.left, glyph_y - slot.top,
                              slot.width_px, slot.height_px};
            push_text_quad(bounds, slot.uv, tint, texture_id);
        }
        pen_x += glyph.advance;
    }
}

void BatchBuilder::clip(const Rect bounds) {
    // Intersect with the viewport: no batch may carry a scissor the backend
    // would reject for exceeding the render target.
    const float x0 = std::max(bounds.x, 0.0f);
    const float y0 = std::max(bounds.y, 0.0f);
    const float x1 = std::min(bounds.right(), static_cast<float>(viewport_.width));
    const float y1 = std::min(bounds.bottom(), static_cast<float>(viewport_.height));
    const Rect clamped{x0, y0, std::max(x1 - x0, 0.0f), std::max(y1 - y0, 0.0f)};

    if (clamped == scissor_) {
        return;  // same clip: the current batch may continue untouched
    }
    flush();
    scissor_ = clamped;
}

void BatchBuilder::push_quad(const Rect& bounds, const Radius& radius,
                             const Paint& paint, const std::uint64_t texture_id) {
    // Nothing to record for empty geometry or a fully closed clip.
    if (bounds.empty() || scissor_.empty()) {
        return;
    }
    // Cull against the clip: the scissor would discard the quad anyway, so
    // recording it would only spend vertices (the one-pixel SDF pad is
    // ignored — a pixel of slack is not worth a second test).
    if (!(bounds.x < scissor_.right() && bounds.right() > scissor_.x
          && bounds.y < scissor_.bottom() && bounds.bottom() > scissor_.y)) {
        return;
    }
    // uint16 indices cap one frame at 16384 quads. Known Phase 0.4 limit,
    // deliberately accepted: beyond the cap, further quads are dropped
    // silently (offsets stay consistent, so frames render fewer quads but
    // never corrupt). Real 2D-UI workloads sit orders of magnitude below
    // it; the first workload that approaches the cap is the signal to add
    // u32 indices, not to raise it here.
    if (vertices_.size() + 4 > 65536) {
        return;
    }

    open_batch(texture_id, BatchKind::shape);

    // Radii past half the shortest extent would deform the distance field;
    // clamping keeps every corner well-formed (documented on Painter).
    const float hw = bounds.width * 0.5f;
    const float hh = bounds.height * 0.5f;
    const float max_r = std::min(hw, hh);
    const Radius r{
        std::clamp(radius.top_left, 0.0f, max_r),
        std::clamp(radius.top_right, 0.0f, max_r),
        std::clamp(radius.bottom_right, 0.0f, max_r),
        std::clamp(radius.bottom_left, 0.0f, max_r),
    };

    // Straight -> premultiplied float color, matching the GPU blend state.
    const float alpha = static_cast<float>(paint.color.a) / 255.0f;
    const float r_p = static_cast<float>(paint.color.r) / 255.0f * alpha;
    const float g_p = static_cast<float>(paint.color.g) / 255.0f * alpha;
    const float b_p = static_cast<float>(paint.color.b) / 255.0f * alpha;

    // One pixel of outward padding gives the SDF edge room to fade; the SDF
    // parameters below stay the unpadded rect's, so the distance field is
    // unaffected and the pad only extends coverage.
    constexpr float kPad = 1.0f;
    const float x0 = bounds.x - kPad;
    const float y0 = bounds.y - kPad;
    const float x1 = bounds.right() + kPad;
    const float y1 = bounds.bottom() + kPad;

    // UV spans the rect itself, not the padded quad. Solid rects sample the
    // single white texel of texture 0.
    const bool solid = texture_id == 0;
    const float inv_w = solid ? 0.0f : 1.0f / bounds.width;
    const float inv_h = solid ? 0.0f : 1.0f / bounds.height;
    const float base_u = solid ? 0.5f : -bounds.x * inv_w;
    const float base_v = solid ? 0.5f : -bounds.y * inv_h;

    const auto push_vertex = [&](const float x, const float y, const float u,
                                 const float v) {
        vertices_.push_back(BatchVertex{
            x, y, u, v,
            r_p, g_p, b_p, alpha,
            bounds.x + hw, bounds.y + hh, hw, hh,
            r.top_left, r.top_right, r.bottom_right, r.bottom_left,
        });
    };

    const std::uint16_t base = static_cast<std::uint16_t>(vertices_.size());
    push_vertex(x0, y0, base_u + x0 * inv_w, base_v + y0 * inv_h);  // top-left
    push_vertex(x1, y0, base_u + x1 * inv_w, base_v + y0 * inv_h);  // top-right
    push_vertex(x1, y1, base_u + x1 * inv_w, base_v + y1 * inv_h);  // bottom-right
    push_vertex(x0, y1, base_u + x0 * inv_w, base_v + y1 * inv_h);  // bottom-left

    const std::array<std::uint16_t, 6> quad{
        base, static_cast<std::uint16_t>(base + 1), static_cast<std::uint16_t>(base + 2),
        base, static_cast<std::uint16_t>(base + 2), static_cast<std::uint16_t>(base + 3)};
    indices_.insert(indices_.end(), quad.begin(), quad.end());
    index_count_ += 6;
}

void BatchBuilder::open_batch(const std::uint64_t texture_id,
                              const BatchKind kind) {
    if (open_ && (texture_id != texture_ || scissor_ != batch_scissor_
                  || kind != batch_kind_)) {
        flush();  // a draw call cannot straddle a texture/clip/pipeline change
    }
    if (!open_) {
        texture_ = texture_id;
        batch_scissor_ = scissor_;
        batch_kind_ = kind;
        vertex_offset_ = static_cast<std::uint32_t>(vertices_.size());
        index_offset_ = static_cast<std::uint32_t>(indices_.size());
        index_count_ = 0;
        open_ = true;
    }
}

void BatchBuilder::push_text_quad(const Rect& bounds, const Rect& uv,
                                  const BatchVertex& tint,
                                  const std::uint64_t texture_id) {
    if (bounds.empty() || scissor_.empty()) {
        return;
    }
    if (!(bounds.x < scissor_.right() && bounds.right() > scissor_.x
          && bounds.y < scissor_.bottom() && bounds.bottom() > scissor_.y)) {
        return;
    }
    // Same uint16 cap as shape quads (documented Phase 0.4 limit).
    if (vertices_.size() + 4 > 65536) {
        return;
    }
    open_batch(texture_id, BatchKind::text);

    // No outward pad: the atlas packs cells with a 1px transparent gutter,
    // and the quad spans exactly the glyph bitmap. Fractional positions
    // sample into that gutter under the linear filter, never into a
    // neighboring glyph (unlike a shape quad, which needs SDF room).
    const float x0 = bounds.x;
    const float y0 = bounds.y;
    const float x1 = bounds.right();
    const float y1 = bounds.bottom();
    const float u0 = uv.x;
    const float v0 = uv.y;
    const auto push_vertex = [&](const float x, const float y, const float u,
                                 const float v) {
        BatchVertex vertex = tint;  // premul ink + contrast in radius_tl
        vertex.x = x;
        vertex.y = y;
        vertex.u = u;
        vertex.v = v;
        vertices_.push_back(vertex);
    };

    const std::uint16_t base = static_cast<std::uint16_t>(vertices_.size());
    push_vertex(x0, y0, u0, v0);
    push_vertex(x1, y0, uv.right(), v0);
    push_vertex(x1, y1, uv.right(), uv.bottom());
    push_vertex(x0, y1, u0, uv.bottom());

    const std::array<std::uint16_t, 6> quad{
        base, static_cast<std::uint16_t>(base + 1), static_cast<std::uint16_t>(base + 2),
        base, static_cast<std::uint16_t>(base + 2), static_cast<std::uint16_t>(base + 3)};
    indices_.insert(indices_.end(), quad.begin(), quad.end());
    index_count_ += 6;
}

void BatchBuilder::finish_frame() {
    flush();
}

void BatchBuilder::flush() {
    if (!open_) {
        return;
    }
    open_ = false;
    if (index_count_ == 0) {
        return;  // clipped to nothing: never emit an empty draw call
    }
    batches_.push_back(Batch{texture_, batch_scissor_, batch_kind_, vertex_offset_,
                             index_offset_, index_count_});
}

}  // namespace continui::detail
