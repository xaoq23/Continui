// Style: value-type styling (docs/architecture.md §5).
//
// Status: skeleton (Phase 0.1). Only a minimal draft subset now; the full
// value-type design (units, inheritance, themes) lands in Phase 1.
#pragma once

#include <cstdint>

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

// Minimal draft style value. Fields are intentionally few; the set only
// grows against a defined evaluation rule (constitution C6).
struct Style {
    Color background{0, 0, 0, 0};
};

constexpr bool operator==(const Style& lhs, const Style& rhs) noexcept {
    return lhs.background == rhs.background;
}

}  // namespace continui
