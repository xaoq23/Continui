// UI orchestration implementation (Phase 0.6). Lives in render because it
// consumes layout results and emits Painter commands; it never sees Dawn or
// platform types. The internal layout tree is reached through the private
// view_tree.hpp header (same pattern the backend uses for batcher.hpp).
#include <continui/ui.hpp>

#include "view_tree.hpp"

#include <continui/font.hpp>

#include <algorithm>
#include <cmath>
#include <print>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace continui {

namespace {

// Real text measurement delegates shaping/metrics to the user's FontFace;
// the solver itself stays independent of the text module.
class FaceMeasurer final : public TextMeasurer {
public:
    explicit FaceMeasurer(const FontFace* face) noexcept : face_(face) {}
    [[nodiscard]] Size measure(const std::string_view text,
                               const float font_size) const override {
        if (face_ == nullptr || font_size <= 0.0f) {
            return {};
        }
        const GlyphRun run = face_->shape(text, font_size);
        const TextMetrics metrics = face_->metrics(font_size);
        return Size{static_cast<int>(std::ceil(run.width)),
                    static_cast<int>(std::ceil(metrics.ascent + metrics.descent))};
    }

private:
    const FontFace* face_ = nullptr;
};

}  // namespace

struct UI::Impl {
    explicit Impl(ViewFn build_fn) : build(std::move(build_fn)) {}

    ViewFn build;
    FrameArena arena;
    LayoutResult layout;
    std::vector<const detail::ViewNode*> nodes;
    std::unordered_map<Key, Callback> callbacks;
    // Diagnostics are one-shot per Key: evaluate() rebuilds callbacks every
    // frame, so without this a persistent duplicate would print each frame.
    std::unordered_set<Key> warned_duplicate_keys;
    std::unique_ptr<TextMeasurer> measurer;
    const FontFace* font = nullptr;
    Invalidation pending = Invalidation::view;
    Size viewport{800, 600};

    void evaluate() {
        arena.reset();
        callbacks.clear();
        nodes.clear();

        const View root = build(arena);
        detail::ViewNode* node = detail::ViewTreeAccess::node(root);
        if (node == nullptr) {
            layout = LayoutResult{};
            return;
        }
        // Iterative preorder: indices must match LayoutResult::boxes.
        struct Frame {
            detail::ViewNode* node;
            std::uint32_t next_child;
        };
        std::vector<Frame> stack{{node, 0}};
        while (!stack.empty()) {
            Frame& frame = stack.back();
            detail::ViewNode* current = frame.node;
            if (frame.next_child == 0) {
                current->index = static_cast<std::uint32_t>(nodes.size());
                nodes.push_back(current);
                if (current->type == detail::ViewNode::Type::button && current->callback) {
                    // A duplicate Key used to silently overwrite the first
                    // callback (0.6 audit MINOR). Keep the first registration
                    // and diagnose so routing stays deterministic.
                    if (!callbacks.emplace(current->key, current->callback).second
                        && warned_duplicate_keys.insert(current->key).second) {
                        std::println(stderr,
                                     "continui: duplicate Key {} on a button; "
                                     "keeping the first callback",
                                     current->key.value);
                    }
                }
            }
            if (frame.next_child < current->child_count) {
                detail::ViewNode* child = current->children[frame.next_child++];
                if (child != nullptr) {
                    stack.push_back({child, 0});
                }
            } else {
                stack.pop_back();
            }
        }
        if (measurer == nullptr) {
            measurer = std::make_unique<FaceMeasurer>(font);
        }
        layout = solve(root,
                       Constraints{0.0f, static_cast<float>(viewport.width),
                                   0.0f, static_cast<float>(viewport.height)},
                       *measurer);
    }

    void paint(Painter& painter) {
        for (std::size_t i = 0; i < layout.boxes.size() && i < nodes.size(); ++i) {
            const LayoutBox& box = layout.boxes[i];
            const detail::ViewNode& node = *nodes[i];
            if (box.bounds.empty()) {
                continue;
            }
            const Style& style = node.style;
            if (style.background.a > 0) {
                painter.rounded_rect(box.bounds, Radius::all(style.radius),
                                     Paint{style.background});
            }
            if (node.type != detail::ViewNode::Type::text
                && node.type != detail::ViewNode::Type::button) {
                continue;
            }
            if (font == nullptr || node.text.empty() || style.foreground.a == 0) {
                continue;
            }
            const GlyphRun run = font->shape(node.text, style.font_size);
            const TextMetrics metrics = font->metrics(style.font_size);
            const float text_height = metrics.ascent + metrics.descent;
            const float x = box.content.x
                            + std::max(0.0f, (box.content.width - run.width) * 0.5f);
            const float y = box.content.y
                            + std::max(0.0f, (box.content.height - text_height) * 0.5f)
                            + metrics.ascent;
            painter.text(run, {x, y}, Paint{style.foreground});
        }
    }
};

UI::UI(ViewFn build) : impl_(std::make_unique<Impl>(std::move(build))) {}
UI::~UI() = default;
UI::UI(UI&&) noexcept = default;
UI& UI::operator=(UI&&) noexcept = default;

void UI::invalidate(const Invalidation level) noexcept {
    impl_->pending = merge_invalidation(impl_->pending, level);
}

Invalidation UI::pending() const noexcept { return impl_->pending; }

void UI::set_font(const FontFace* face) {
    impl_->font = face;
    impl_->measurer = std::make_unique<FaceMeasurer>(face);
    // A different face changes text measurement -> geometry; the minimum
    // correct response is a layout invalidation (0.6 audit INFO).
    invalidate(Invalidation::layout);
}

void UI::set_text_measurer(std::unique_ptr<TextMeasurer> measurer) {
    impl_->measurer = std::move(measurer);
    invalidate(Invalidation::layout);
}

void UI::paint(Painter& painter, const Size viewport) {
    if (viewport != impl_->viewport) {
        // A resize changes the root constraints but neither business state
        // nor the view tree; the minimum correct response is a layout
        // invalidation (0.6 audit MINOR: resizes left stale hit geometry).
        invalidate(Invalidation::layout);
        impl_->viewport = viewport;
    }
    // Phase 0.6 keeps the levels for callers and merging, but every
    // non-none level re-evaluates the whole (tiny) tree; dirty-region and
    // partial recomputation arrive in Phase 0.8.
    if (impl_->pending != Invalidation::none) {
        impl_->evaluate();
    }
    impl_->pending = Invalidation::none;
    impl_->paint(painter);
}

bool UI::handle_event(const Event& event) {
    if (event.type != EventType::mouse_button_up
        || event.button != MouseButton::left) {
        return false;
    }
    const auto index = hit_test(impl_->layout,
                                PointF{static_cast<float>(event.position.x),
                                       static_cast<float>(event.position.y)});
    if (!index || *index >= impl_->nodes.size()) {
        return false;
    }
    const detail::ViewNode& node = *impl_->nodes[*index];
    if (node.type != detail::ViewNode::Type::button) {
        return false;
    }
    const auto it = impl_->callbacks.find(node.key);
    if (it == impl_->callbacks.end() || !it->second) {
        return false;
    }
    it->second();  // may mutate user state and call UI::invalidate()
    return true;
}

const LayoutResult& UI::layout() const noexcept { return impl_->layout; }

}  // namespace continui
