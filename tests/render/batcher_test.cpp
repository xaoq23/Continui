// Batcher unit tests: pure-CPU geometry, merge behavior, and value
// conversions. No GPU. Assertions use a plain check() (Phase 0 policy: the
// test framework choice is deferred; must not depend on <cassert> so a
// Release run stays meaningful).
#include "batcher.hpp"

#include <cmath>
#include <cstdint>
#include <print>
#include <vector>

using continui::detail::BatchBuilder;

namespace {

int failures = 0;

void check(const bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::println(stderr, "FAIL: {}", what);
    }
}

bool approx(const float got, const float want, const float eps = 1e-6f) {
    return std::fabs(got - want) <= eps;
}

bool same_values(const std::vector<std::uint16_t>& got,
                 const std::initializer_list<std::uint16_t> want) {
    return got == std::vector<std::uint16_t>{want};
}

}  // namespace

int main() {
    using continui::Color;
    using continui::Paint;
    using continui::Radius;
    using continui::Rect;
    using continui::Size;

    // --- a single rect: one batch, quad topology, and vertex payload ------
    {
        BatchBuilder b;
        b.begin_frame(Size{800, 600});
        b.rect(Rect{100, 50, 200, 80}, Paint{Color::rgb(255, 0, 0)});
        b.finish_frame();

        check(b.batches().size() == 1, "one rect -> one batch");
        check(b.vertices().size() == 4, "one rect -> 4 vertices");
        check(b.indices().size() == 6, "one rect -> 6 indices");
        check(same_values(b.indices(), {0, 1, 2, 0, 2, 3}),
              "quad indices are 0,1,2,0,2,3");

        const auto& batch = b.batches()[0];
        check(batch.texture_id == 0, "solid rect uses the white texture");
        check(batch.vertex_offset == 0 && batch.index_offset == 0,
              "first batch sits at zero offsets");
        check(batch.index_count == 6, "first batch covers its quad");
        check(batch.scissor == Rect{0, 0, 800, 600},
              "default scissor is the viewport");

        const auto& v = b.vertices();
        // Positions pad one pixel outward for the SDF fade.
        check(approx(v[0].x, 99) && approx(v[0].y, 49),
              "v0 is the padded top-left corner");
        check(approx(v[1].x, 301) && approx(v[1].y, 49),
              "v1 is the padded top-right corner");
        check(approx(v[2].x, 301) && approx(v[2].y, 131),
              "v2 is the padded bottom-right corner");
        check(approx(v[3].x, 99) && approx(v[3].y, 131),
              "v3 is the padded bottom-left corner");
        // Solid rects sample the single white texel.
        check(approx(v[0].u, 0.5f) && approx(v[0].v, 0.5f)
                  && approx(v[2].u, 0.5f) && approx(v[2].v, 0.5f),
              "solid rect uv is the texel center");
        // Premultiplied opaque red.
        check(approx(v[0].r, 1.0f) && v[0].g == 0.0f && v[0].b == 0.0f
                  && approx(v[0].a, 1.0f),
              "opaque red stays (1,0,0,1) after premultiplication");
        // SDF parameters describe the unpadded rect.
        check(approx(v[0].rect_cx, 200) && approx(v[0].rect_cy, 90),
              "sdf center is the rect center");
        check(approx(v[0].rect_hw, 100) && approx(v[0].rect_hh, 40),
              "sdf half extents are the rect's");
        check(v[0].radius_tl == 0.0f && v[0].radius_tr == 0.0f
                  && v[0].radius_br == 0.0f && v[0].radius_bl == 0.0f,
              "rect() has sharp corners");
    }

    // --- same texture + same clip keeps one batch -------------------------
    {
        BatchBuilder b;
        b.begin_frame(Size{800, 600});
        b.rect(Rect{0, 0, 10, 10}, Paint{});
        b.rect(Rect{20, 0, 10, 10}, Paint{Color::rgb(0, 255, 0)});
        b.finish_frame();

        check(b.batches().size() == 1, "rects with the same key merge");
        check(b.vertices().size() == 8, "merged quads keep all vertices");
        check(b.batches()[0].index_count == 12, "merged batch covers both quads");
    }

    // --- texture changes split; returning to solid splits again -----------
    {
        BatchBuilder b;
        b.begin_frame(Size{800, 600});
        b.rect(Rect{0, 0, 10, 10}, Paint{});
        b.image(7, Rect{0, 0, 10, 10});
        b.rect(Rect{0, 0, 10, 10}, Paint{});
        b.finish_frame();

        check(b.batches().size() == 3, "texture switches split into 3 batches");
        check(b.batches()[0].texture_id == 0 && b.batches()[1].texture_id == 7
                  && b.batches()[2].texture_id == 0,
              "batch textures appear in recording order");
        check(b.batches()[1].vertex_offset == 4 && b.batches()[2].vertex_offset == 8,
              "vertex offsets follow the quads");
        check(b.batches()[1].index_offset == 6 && b.batches()[2].index_offset == 12,
              "index offsets follow the quads");

        // Frame-absolute indexing: batch 2's indices count from 0 across the
        // whole frame's vertex list — the draw must use firstIndex and
        // baseVertex 0, never baseVertex = vertex_offset (a baseVertex
        // double-shift would fetch out-of-range vertices silently).
        check(same_values(b.indices(),
                          {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11}),
              "indices stay frame-absolute across batches");

        // Textured quads carry straight 0..1 uv over the rect itself.
        const auto& v = b.vertices();
        check(approx(v[4].u, -0.1f) && approx(v[4].v, -0.1f),
              "textured v0 maps to the padded rect corner (uv -1px/width)");
        check(approx(v[6].u, 1.1f) && approx(v[6].v, 1.1f),
              "textured v2 maps to the opposite padded corner");
        check(approx(v[4].r, 1.0f) && approx(v[4].a, 1.0f),
              "image() paints with opaque white tint");
    }

    // --- clip switches split; same clip is free; culled quads vanish ------
    {
        BatchBuilder b;
        b.begin_frame(Size{800, 600});
        b.rect(Rect{0, 0, 10, 10}, Paint{});
        b.clip(Rect{0, 0, 100, 100});
        check(b.batches().size() == 1, "a clip change flushes the previous batch");

        b.rect(Rect{0, 0, 10, 10}, Paint{});
        b.clip(Rect{0, 0, 100, 100});  // identical clip
        b.rect(Rect{0, 0, 10, 10}, Paint{});
        b.finish_frame();
        check(b.batches().size() == 2,
              "re-clipping to the same rect does not split");
        check(b.batches()[1].scissor == Rect{0, 0, 100, 100},
              "the batch carries its scissor");

        b.clip(Rect{200, 200, 100, 100});  // disjoint clip
        const std::size_t before = b.vertices().size();
        b.rect(Rect{0, 0, 10, 10}, Paint{});  // entirely outside the clip
        check(b.vertices().size() == before,
              "quads outside the clip record nothing");
        b.rect(Rect{250, 250, 10, 10}, Paint{});  // inside
        check(b.vertices().size() == before + 4,
              "quads inside the clip are recorded");
    }

    // --- clip bounds intersect the viewport -------------------------------
    {
        BatchBuilder b;
        b.begin_frame(Size{100, 100});
        b.clip(Rect{-50, -50, 300, 300});
        b.rect(Rect{0, 0, 10, 10}, Paint{});
        b.finish_frame();

        check(b.batches().size() == 1
                  && b.batches()[0].scissor == Rect{0, 0, 100, 100},
              "clip bounds intersect the viewport");
    }

    // --- premultiplication of a translucent color -------------------------
    {
        BatchBuilder b;
        b.begin_frame(Size{800, 600});
        b.rect(Rect{0, 0, 10, 10}, Paint{Color::rgba(255, 0, 0, 128)});
        b.finish_frame();

        const auto& v = b.vertices();
        check(approx(v[0].r, 128.0f / 255.0f) && v[0].g == 0.0f
                  && v[0].b == 0.0f && approx(v[0].a, 128.0f / 255.0f),
              "straight (255,0,0,128) premultiplies to r=a=128/255");
    }

    // --- radius clamping ---------------------------------------------------
    {
        BatchBuilder b;
        b.begin_frame(Size{800, 600});
        // Half the shortest extent is 5; a 100 radius clamps to 5.
        b.rounded_rect(Rect{0, 0, 10, 100}, Radius::all(100), Paint{});
        b.finish_frame();

        const auto& v = b.vertices();
        check(approx(v[0].radius_tl, 5.0f) && approx(v[0].radius_tr, 5.0f)
                  && approx(v[0].radius_br, 5.0f) && approx(v[0].radius_bl, 5.0f),
              "oversized radii clamp to half the shortest extent");

        b.begin_frame(Size{800, 600});
        b.rounded_rect(Rect{0, 0, 100, 100}, Radius::top(30), Paint{});
        b.finish_frame();
        const auto& w = b.vertices();
        check(approx(w[0].radius_tl, 30.0f) && approx(w[1].radius_tr, 30.0f)
                  && w[2].radius_br == 0.0f && w[3].radius_bl == 0.0f,
              "per-corner radii land on their corners");
    }

    // --- things that must record nothing ----------------------------------
    {
        BatchBuilder b;
        b.begin_frame(Size{800, 600});
        b.rect(Rect{0, 0, 0, 10}, Paint{});
        b.rect(Rect{0, 0, 10, -1}, Paint{});
        b.rect(Rect{0, 0, -5, 10}, Paint{});
        b.image(0, Rect{0, 0, 10, 10});  // white-texture id is not an image
        b.finish_frame();
        check(b.vertices().empty() && b.indices().empty() && b.batches().empty(),
              "empty rects and an invalid image record nothing");
    }

    // --- begin_frame resets everything ------------------------------------
    {
        BatchBuilder b;
        b.begin_frame(Size{800, 600});
        b.rect(Rect{0, 0, 10, 10}, Paint{});
        b.clip(Rect{0, 0, 50, 50});
        b.rect(Rect{0, 0, 10, 10}, Paint{});
        b.finish_frame();
        check(!b.batches().empty(), "sanity: frames have content");

        b.begin_frame(Size{1024, 768});
        check(b.vertices().empty() && b.indices().empty() && b.batches().empty(),
              "begin_frame clears all buffers");
        b.rect(Rect{0, 0, 10, 10}, Paint{});
        b.finish_frame();
        check(b.batches().size() == 1
                  && b.batches()[0].scissor == Rect{0, 0, 1024, 768},
              "begin_frame restores the full-viewport scissor");
    }

    if (failures != 0) {
        std::println(stderr, "batcher_test: {} check(s) failed", failures);
        return 1;
    }
    return 0;
}
