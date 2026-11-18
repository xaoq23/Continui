// Explicit invalidation levels (architecture §3.3, constitution C3). A user
// callback mutates business state and calls UI::invalidate(level); there is
// no implicit dependency tracking anywhere in the framework.
#pragma once

#include <algorithm>
#include <cstdint>

namespace continui {

enum class Invalidation : std::uint8_t {
    none = 0,
    paint,   // repaint only
    layout,  // geometry changed
    view,    // view structure / content changed
    full,    // force every stage
};

// Levels form a total order: merge keeps the stronger request.
[[nodiscard]] constexpr Invalidation merge_invalidation(const Invalidation lhs,
                                                        const Invalidation rhs) noexcept {
    return static_cast<Invalidation>(
        std::max(static_cast<std::uint8_t>(lhs), static_cast<std::uint8_t>(rhs)));
}

[[nodiscard]] constexpr bool at_least(const Invalidation actual,
                                      const Invalidation required) noexcept {
    return static_cast<std::uint8_t>(actual) >= static_cast<std::uint8_t>(required);
}

}  // namespace continui
