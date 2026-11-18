// Style: value-type styling (docs/architecture.md §5).
//
// Status: Phase 0.6 minimal draft. Plain float pixels (no Length units,
// inheritance, themes — those remain Phase 1). The field set covers the
// box model and the basic Flex subset used by the layout solver.
#pragma once

#include <continui/geometry.hpp>

#include <cstdint>
#include <limits>

namespace continui {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    static constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
        return Color{r, g, b, 255};
    }

    static constexpr Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                std::uint8_t a) noexcept {
        return Color{r, g, b, a};
    }
};

constexpr bool operator==(const Color& lhs, const Color& rhs) noexcept {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

enum class Align : std::uint8_t { start, center, end, stretch };
enum class Justify : std::uint8_t { start, center, end, space_between };

// Layout-relevant style values. `width`/`height` < 0 means "auto" (measure
// content); min/max bound the resolved box. Padding/margin/gap are pixels.
struct Style {
    Color background{0, 0, 0, 0};
    Color foreground{255, 255, 255, 255};
    Edges padding{};
    Edges margin{};
    float width = -1.0f;
    float height = -1.0f;
    float min_width = 0.0f;
    float min_height = 0.0f;
    float max_width = std::numeric_limits<float>::infinity();
    float max_height = std::numeric_limits<float>::infinity();
    float gap = 0.0f;
    float flex_grow = 0.0f;
    float flex_shrink = 0.0f;
    float flex_basis = -1.0f;  // < 0 = use the measured main-axis size
    Align align_items = Align::stretch;
    Justify justify_content = Justify::start;
    float font_size = 16.0f;
    float radius = 0.0f;  // corner radius for painted rects/buttons
};

constexpr bool operator==(const Style& lhs, const Style& rhs) noexcept {
    return lhs.background == rhs.background && lhs.foreground == rhs.foreground
           && lhs.padding == rhs.padding && lhs.margin == rhs.margin
           && lhs.width == rhs.width && lhs.height == rhs.height
           && lhs.min_width == rhs.min_width && lhs.min_height == rhs.min_height
           && lhs.max_width == rhs.max_width && lhs.max_height == rhs.max_height
           && lhs.gap == rhs.gap && lhs.flex_grow == rhs.flex_grow
           && lhs.flex_shrink == rhs.flex_shrink && lhs.flex_basis == rhs.flex_basis
           && lhs.align_items == rhs.align_items
           && lhs.justify_content == rhs.justify_content
           && lhs.font_size == rhs.font_size && lhs.radius == rhs.radius;
}

}  // namespace continui
