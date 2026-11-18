#include <continui/layout.hpp>

#include "view_tree.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace continui {
namespace {

using detail::ViewNode;

float clamp_value(const float value, const float lo, const float hi) noexcept {
    return std::clamp(value, lo, std::max(lo, hi));
}

Rect inset(const Rect& rect, const Edges& edges) noexcept {
    return Rect{rect.x + edges.left, rect.y + edges.top,
                std::max(0.0f, rect.width - edges.horizontal()),
                std::max(0.0f, rect.height - edges.vertical())};
}

bool is_row(const ViewNode& node) noexcept {
    return node.type == ViewNode::Type::row;
}

bool is_container(const ViewNode& node) noexcept {
    return node.type == ViewNode::Type::row || node.type == ViewNode::Type::column;
}

struct SizeF {
    float width = 0;
    float height = 0;
};

SizeF measure(const ViewNode& node, const Constraints& constraints,
              const TextMeasurer& measurer);

SizeF measure_children(const ViewNode& node, const Constraints& constraints,
                       const TextMeasurer& measurer) noexcept {
    float main = 0;
    float cross = 0;
    const float gap = std::max(0.0f, node.style.gap);
    const bool row = is_row(node);
    for (std::uint32_t i = 0; i < node.child_count; ++i) {
        const ViewNode* child = node.children[i];
        if (child == nullptr) {
            continue;
        }
        // Children are measured intrinsically; final sizes are resolved by
        // the flex distribution in place_children against the content box.
        // A child's margin consumes layout space and must be included here.
        // place_children builds its slots from the same margin-inclusive
        // basis, so intrinsic sizing and placement agree (0.6 audit MINOR).
        const SizeF child_size = measure(*child, constraints, measurer);
        const float child_main = row ? child_size.width : child_size.height;
        const float child_cross = row ? child_size.height : child_size.width;
        const float main_margin = row ? child->style.margin.horizontal()
                                      : child->style.margin.vertical();
        const float cross_margin = row ? child->style.margin.vertical()
                                       : child->style.margin.horizontal();
        main += child_main + main_margin;
        cross = std::max(cross, child_cross + cross_margin);
        if (i + 1 < node.child_count) {
            main += gap;
        }
    }
    return row ? SizeF{main, cross} : SizeF{cross, main};
}

SizeF measure(const ViewNode& node, const Constraints& constraints,
              const TextMeasurer& measurer) {
    SizeF intrinsic{0.0f, 0.0f};
    if (node.type == ViewNode::Type::text || node.type == ViewNode::Type::button
        || node.type == ViewNode::Type::input) {
        const Size measured = measurer.measure(node.text, node.style.font_size);
        intrinsic = SizeF{static_cast<float>(measured.width),
                          static_cast<float>(measured.height)};
    } else if (is_container(node)) {
        intrinsic = measure_children(node, constraints, measurer);
    }
    // Explicit width/height are border-box sizes. Auto dimensions describe
    // the content box, so add padding to reach the border box. Clamp min/max
    // and parent constraints exactly once, on the final border-box size:
    // clamping before padding double-applies min/max to auto sizes (0.6 audit
    // MINOR, e.g. min_width=100 + padding 40 used to produce 140).
    SizeF result;
    result.width = node.style.width >= 0.0f
                       ? node.style.width
                       : intrinsic.width + node.style.padding.horizontal();
    result.height = node.style.height >= 0.0f
                        ? node.style.height
                        : intrinsic.height + node.style.padding.vertical();
    result.width = clamp_value(result.width, node.style.min_width, node.style.max_width);
    result.height = clamp_value(result.height, node.style.min_height, node.style.max_height);
    result.width = clamp_value(result.width, constraints.min_width, constraints.max_width);
    result.height = clamp_value(result.height, constraints.min_height, constraints.max_height);
    return result;
}

void collect_preorder(ViewNode& node, std::vector<const ViewNode*>& out) {
    // Iterative traversal: deep trees cannot overflow the stack.
    struct Frame {
        ViewNode* node;
        std::uint32_t next_child;
    };
    std::vector<Frame> stack{{&node, 0}};
    while (!stack.empty()) {
        Frame& frame = stack.back();
        if (frame.next_child == 0) {
            frame.node->index = static_cast<std::uint32_t>(out.size());
            out.push_back(frame.node);
        }
        if (frame.next_child < frame.node->child_count) {
            ViewNode* child = frame.node->children[frame.next_child++];
            if (child != nullptr) {
                stack.push_back({child, 0});
            }
        } else {
            stack.pop_back();
        }
    }
}

void place(const ViewNode& node, const Rect& bounds, const Constraints& constraints,
           const TextMeasurer& measurer, std::vector<LayoutBox>& boxes);

void place_children(const ViewNode& node, const Rect& content,
                    const Constraints& constraints, const TextMeasurer& measurer,
                    std::vector<LayoutBox>& boxes) {
    const bool row = is_row(node);
    const float main_span = row ? content.width : content.height;
    const float cross_span = row ? content.height : content.width;
    const float gap = std::max(0.0f, node.style.gap);
    const std::uint32_t count = node.child_count;

    std::vector<SizeF> sizes(count);
    std::vector<float> bases(count, 0.0f);
    float used_main = 0.0f;
    for (std::uint32_t i = 0; i < count; ++i) {
        const ViewNode* child = node.children[i];
        if (child == nullptr) {
            continue;
        }
        sizes[i] = measure(*child, constraints, measurer);
        // flex_basis is a main-axis size: a column's main axis is height, so
        // clamp against min/max_height (0.6 audit MINOR: width fields were
        // used unconditionally).
        const float basis_min = row ? child->style.min_width : child->style.min_height;
        const float basis_max = row ? child->style.max_width : child->style.max_height;
        const float main_margin = row ? child->style.margin.horizontal()
                                      : child->style.margin.vertical();
        // The slot includes the child's margin so the border box placed below
        // resolves to the measured size (not measured - margin, which clipped
        // the content and left trailing slack).
        bases[i] = (child->style.flex_basis >= 0.0f
                        ? clamp_value(child->style.flex_basis, basis_min, basis_max)
                        : (row ? sizes[i].width : sizes[i].height)) + main_margin;
        used_main += bases[i];
        if (i + 1 < count) {
            used_main += gap;
        }
    }

    // Flex grow/shrink on the main axis; leftover free space is distributed
    // by justify_content only when nobody claims it via flex_grow.
    float free = main_span - used_main;
    float total_grow = 0.0f;
    float total_shrink = 0.0f;
    for (std::uint32_t i = 0; i < count; ++i) {
        const ViewNode* child = node.children[i];
        if (child == nullptr) {
            continue;
        }
        total_grow += std::max(0.0f, child->style.flex_grow);
        total_shrink += std::max(0.0f, child->style.flex_shrink) * bases[i];
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const ViewNode* child = node.children[i];
        if (child == nullptr) {
            continue;
        }
        if (free > 0.0f && total_grow > 0.0f) {
            bases[i] += free * std::max(0.0f, child->style.flex_grow) / total_grow;
        } else if (free < 0.0f && total_shrink > 0.0f) {
            bases[i] += free * (std::max(0.0f, child->style.flex_shrink) * bases[i])
                        / total_shrink;
        }
    }

    float justify_offset = 0.0f;
    float extra_gap = 0.0f;
    if (total_grow == 0.0f && free > 0.0f && count > 0) {
        switch (node.style.justify_content) {
            case Justify::center:
                justify_offset = free * 0.5f;
                break;
            case Justify::end:
                justify_offset = free;
                break;
            case Justify::space_between:
                if (count > 1) {
                    extra_gap = free / static_cast<float>(count - 1);
                }
                break;
            case Justify::start:
                break;
        }
    }

    float main_cursor = (row ? content.x : content.y) + justify_offset;
    for (std::uint32_t i = 0; i < count; ++i) {
        const ViewNode* child = node.children[i];
        if (child == nullptr) {
            continue;
        }
        const float child_main = std::max(0.0f, bases[i]);
        const SizeF measured = sizes[i];
        const float cross_margin = row ? child->style.margin.vertical()
                                       : child->style.margin.horizontal();
        float child_cross = (row ? measured.height : measured.width) + cross_margin;
        float cross_offset = 0.0f;
        switch (node.style.align_items) {
            case Align::stretch:
                child_cross = cross_span;
                break;
            case Align::center:
                cross_offset = (cross_span - child_cross) * 0.5f;
                break;
            case Align::end:
                cross_offset = cross_span - child_cross;
                break;
            case Align::start:
                break;
        }
        // The child's own margin insets it inside its allocated main slot.
        const Rect child_bounds = row
            ? Rect{main_cursor + child->style.margin.left,
                   content.y + cross_offset + child->style.margin.top,
                   std::max(0.0f, child_main - child->style.margin.horizontal()),
                   std::max(0.0f, child_cross - child->style.margin.vertical())}
            : Rect{content.x + cross_offset + child->style.margin.left,
                   main_cursor + child->style.margin.top,
                   std::max(0.0f, child_cross - child->style.margin.horizontal()),
                   std::max(0.0f, child_main - child->style.margin.vertical())};
        place(*child, child_bounds, constraints, measurer, boxes);
        main_cursor += child_main + gap + extra_gap;
    }
}

void place(const ViewNode& node, const Rect& bounds, const Constraints& constraints,
           const TextMeasurer& measurer, std::vector<LayoutBox>& boxes) {
    LayoutBox& box = boxes[node.index];
    box.bounds = bounds;
    box.content = inset(bounds, node.style.padding);
    box.key = node.key;
    box.node_index = node.index;
    box.is_button = node.type == ViewNode::Type::button;
    if (is_container(node) && node.child_count > 0) {
        place_children(node, box.content, constraints, measurer, boxes);
    }
}

}  // namespace

const LayoutBox* LayoutResult::find(const Key key) const noexcept {
    for (const LayoutBox& box : boxes) {
        if (box.key == key) {
            return &box;
        }
    }
    return nullptr;
}

LayoutResult solve(const View& root, const Constraints& constraints,
                   const TextMeasurer& measurer) {
    LayoutResult result;
    ViewNode* node = detail::ViewTreeAccess::node(root);
    if (node == nullptr) {
        return result;
    }
    std::vector<const ViewNode*> nodes;
    collect_preorder(*node, nodes);
    result.boxes.resize(nodes.size());

    const SizeF size = measure(*node, constraints, measurer);
    const Rect bounds{0.0f, 0.0f, size.width, size.height};
    result.root_bounds = bounds;
    place(*node, bounds, constraints, measurer, result.boxes);
    return result;
}

std::optional<std::uint32_t> hit_test(const LayoutResult& result, const PointF point) {
    // Preorder means a descendant always appears after its ancestors, so the
    // last containing box is the deepest one under the cursor.
    for (auto it = result.boxes.rbegin(); it != result.boxes.rend(); ++it) {
        const Rect& box = it->bounds;
        if (point.x >= box.x && point.x <= box.right()
            && point.y >= box.y && point.y <= box.bottom()) {
            return static_cast<std::uint32_t>(std::distance(result.boxes.begin(), it.base()) - 1);
        }
    }
    return std::nullopt;
}

}  // namespace continui
