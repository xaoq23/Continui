// Painter: the public drawing abstraction (docs/architecture.md §7.2).
//
// Status: Phase 0.4. Records geometry into the frame's draw batches; the
// backend consumes those batches in one or few draw calls (ADR-007). Only
// the Phase 0.4 subset is real: rect / rounded_rect / image / clip. text
// arrives with the text system (Phase 0.5); line / path / transform when a
// consumer needs them.
//
// A Painter is only usable inside a paint phase — the one the renderer's
// paint callback receives. Records made outside a paint phase draw nothing.
// Never exposes backend (Dawn) types.
#pragma once

#include <continui/events.hpp>
#include <continui/geometry.hpp>
#include <continui/style.hpp>
#include <continui/texture.hpp>

namespace continui {

namespace detail {
class BatchBuilder;
}

// How a shape is filled: a solid color. Colors are straight (non-
// premultiplied) alpha here; the batcher converts to premultiplied floats
// because that is what the GPU blends with.
struct Paint {
    Color color = Color::rgb(255, 255, 255);
};

constexpr bool operator==(const Paint& lhs, const Paint& rhs) noexcept {
    return lhs.color == rhs.color;
}

class Painter {
public:
    explicit Painter(detail::BatchBuilder& batches) noexcept
        : batches_(&batches) {}

    // Filled axis-aligned rectangle. Empty bounds draw nothing.
    void rect(Rect bounds, Paint paint);

    // Rectangle with independently rounded corners. Radii larger than half
    // of the rect's shortest extent are clamped down, not rejected.
    void rounded_rect(Rect bounds, Radius radius, Paint paint);

    // Draws the whole texture stretched into bounds (anti-aliased corners
    // are squared unless combined with rounded_rect logic — textures have
    // no radius in Phase 0.4). Texture pixels are straight-alpha RGBA8 and
    // are drawn as-is; pass semi-transparency in the texture data itself.
    // An invalid handle or empty bounds draw nothing.
    void image(const Texture& texture, Rect bounds);

    // Replaces the pixel clip with bounds (intersected with the viewport).
    // Everything recorded afterwards is scissored to it. This is a flat
    // clip: there is no save/restore stack yet — nested clips with restore
    // arrive with the view system. Switching clips flushes pending batches
    // (draw calls cannot straddle a scissor change); clipping to the same
    // rect repeatedly is free.
    void clip(Rect bounds);

private:
    detail::BatchBuilder* batches_;  // never null while a paint phase runs
};

}  // namespace continui
