// Layout: pure CPU conversion of a view tree into geometry (Phase 0.6,
// docs/architecture.md §6). No GPU, platform, or business-state access;
// identical inputs always produce identical outputs.
#pragma once

#include <continui/events.hpp>
#include <continui/geometry.hpp>
#include <continui/key.hpp>
#include <continui/view.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace continui {

struct Constraints {
    float min_width = 0.0f;
    float max_width = std::numeric_limits<float>::infinity();
    float min_height = 0.0f;
    float max_height = std::numeric_limits<float>::infinity();
};

// One resolved node box. `bounds` is the border box (padding included,
// margin excluded); `content` is the inner area children are placed in.
struct LayoutBox {
    Rect bounds{};
    Rect content{};
    Key key{};
    std::uint32_t node_index = 0;
    bool is_button = false;
};

struct LayoutResult {
    std::vector<LayoutBox> boxes;  // preorder; index == node_index
    Rect root_bounds{};

    [[nodiscard]] const LayoutBox* find(Key key) const noexcept;
};

// Injected so the solver has no dependency on the text module. The real
// implementation wraps FontFace::shape/metrics; tests use a fake measurer.
class TextMeasurer {
public:
    virtual ~TextMeasurer() = default;
    [[nodiscard]] virtual Size measure(std::string_view text, float font_size) const = 0;
};

[[nodiscard]] LayoutResult solve(const View& root, const Constraints& constraints,
                                 const TextMeasurer& measurer);

// Deepest box whose bounds contain the point (pixels, y-down). Preorder
// guarantees descendants are visited after ancestors, so the last match wins.
// Bounds are inclusive; equal-depth overlapping boxes have no priority rule.
[[nodiscard]] std::optional<std::uint32_t> hit_test(const LayoutResult& result,
                                                     PointF point);

}  // namespace continui
