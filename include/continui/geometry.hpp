// Geometry value types for the drawing domain (docs/architecture.md §7.2).
//
// Status: Phase 0.4. These are the GPU-facing float-pixel types the Painter
// consumes. Event-domain Point/Size (events.hpp) are integers; the layout
// system converts between the two when it arrives (Phase 0.6).
#pragma once

#include <cmath>

namespace continui {

// Axis-aligned rectangle in pixel coordinates, y grows downward.
// Negative/zero extents are legal to construct; drawing them produces
// nothing (the batcher skips empty rects).
struct Rect {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;

    [[nodiscard]] constexpr float right() const noexcept { return x + width; }
    [[nodiscard]] constexpr float bottom() const noexcept { return y + height; }
    [[nodiscard]] constexpr bool empty() const noexcept {
        return !(width > 0) || !(height > 0);
    }
};

constexpr bool operator==(const Rect& lhs, const Rect& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width
           && lhs.height == rhs.height;
}

// Corner radii for rounded rectangles, one per corner so single-side
// rounding works without extra APIs. Values larger than half the rect's
// shortest extent are clamped by the batcher, not rejected.
struct Radius {
    float top_left = 0;
    float top_right = 0;
    float bottom_right = 0;
    float bottom_left = 0;

    [[nodiscard]] static constexpr Radius all(float r) noexcept {
        return Radius{r, r, r, r};
    }
    [[nodiscard]] static constexpr Radius top(float r) noexcept {
        return Radius{r, r, 0, 0};
    }
    [[nodiscard]] static constexpr Radius bottom(float r) noexcept {
        return Radius{0, 0, r, r};
    }
    [[nodiscard]] static constexpr Radius left(float r) noexcept {
        return Radius{r, 0, 0, r};
    }
    [[nodiscard]] static constexpr Radius right(float r) noexcept {
        return Radius{0, r, r, 0};
    }
};

constexpr bool operator==(const Radius& lhs, const Radius& rhs) noexcept {
    return lhs.top_left == rhs.top_left && lhs.top_right == rhs.top_right
           && lhs.bottom_right == rhs.bottom_right
           && lhs.bottom_left == rhs.bottom_left;
}

}  // namespace continui
