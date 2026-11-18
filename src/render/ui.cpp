// UI orchestration implementation (Phase 0.6 vertical slice + Phase 0.7
// interaction). Consumes layout results and emits Painter commands; it never
// sees Dawn or platform types. Widget-local state lives across frames and is
// exposed read-only to ViewFn through LocalState (architecture §4.5).
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
    FaceMeasurer(const FontFace* face, std::vector<const FontFace*> fallbacks)
        : face_(face), fallbacks_(std::move(fallbacks)) {}
    [[nodiscard]] Size measure(const std::string_view text,
                               const float font_size) const override {
        if (face_ == nullptr || font_size <= 0.0f) {
            return {};
        }
        const GlyphRun run = fallbacks_.empty()
                                 ? face_->shape(text, font_size)
                                 : shape_with_fallback(face_, fallbacks_, text,
                                                       font_size);
        const TextMetrics metrics = face_->metrics(font_size);
        return Size{static_cast<int>(std::ceil(run.width)),
                    static_cast<int>(std::ceil(metrics.ascent + metrics.descent))};
    }

private:
    const FontFace* face_ = nullptr;
    std::vector<const FontFace*> fallbacks_;
};

std::size_t utf8_char_count(const std::string_view text) noexcept {
    std::size_t count = 0;
    for (const unsigned char c : text) {
        if ((c & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

// Byte offset of the n-th UTF-8 character boundary (carets use char indices).
std::size_t utf8_byte_offset(const std::string_view text,
                             const std::size_t char_index) noexcept {
    std::size_t seen = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0u) != 0x80u) {
            if (seen == char_index) {
                return i;
            }
            ++seen;
        }
    }
    return text.size();
}

// Value-type draw commands (0.8). The renderer clears its BatchBuilder every
// frame, so UI replays cached Painter commands into it each frame instead of
// re-shaping text. Commands contain no arena pointers, so they survive
// evaluate()'s arena.reset().
enum class DrawKind : std::uint8_t { rect, rounded_rect, text };

struct DrawCommand {
    DrawKind kind = DrawKind::rect;
    Rect bounds{};
    Radius radius{};
    Color color{};
    GlyphRun run{};     // only meaningful for DrawKind::text
    PointF origin{};    // only meaningful for DrawKind::text
};

// Everything that affects a node's appearance. Two nodes with the same
// visual signature reuse the cached draw commands (0.8b per-Key diff).
struct NodeVisual {
    Style style{};
    std::string text{};
    Rect bounds{};
    Rect content{};
    std::uint8_t type = 0;
    bool focused = false;
    bool composing = false;
    std::string composition{};
    std::size_t caret = 0;
    std::size_t selection_start = 0;
    std::size_t selection_end = 0;
    std::uint64_t font_epoch = 0;

    [[nodiscard]] bool same_as(const NodeVisual& other) const noexcept {
        return style == other.style && text == other.text && bounds == other.bounds
               && content == other.content && type == other.type
               && focused == other.focused && composing == other.composing
               && composition == other.composition && caret == other.caret
               && selection_start == other.selection_start
               && selection_end == other.selection_end
               && font_epoch == other.font_epoch;
    }
};

struct NodeSnapshot {
    NodeVisual visual{};
    std::vector<DrawCommand> commands{};
    Rect ime_caret{};
    Rect ime_text_bounds{};
};

}  // namespace

struct UI::Impl {
    explicit Impl(ViewFn build_fn) : build(std::move(build_fn)) {}

    ViewFn build;
    FrameArena arena;
    View root_view{};  // last evaluated root (arena-owned; valid until next evaluate)
    LayoutResult layout;
    std::vector<const detail::ViewNode*> nodes;
    std::unordered_map<Key, Callback> callbacks;
    std::unordered_map<Key, TextCallback> text_callbacks;
    std::unordered_map<Key, WidgetState> widget_states;
    std::unordered_set<Key> warned_duplicate_keys;
    std::unique_ptr<TextMeasurer> measurer;
    const FontFace* font = nullptr;
    std::vector<const FontFace*> fallback_fonts;
    Invalidation pending = Invalidation::view;
    Size viewport{800, 600};
    Rect ime_caret_{};        // last painted caret, client pixels (IME sync)
    Rect ime_text_bounds_{};  // last painted text extent, client pixels
    Key focused_key{};
    Key pressed_key{};

    // 0.8: retained, value-type caches. They survive arena.reset() and drive
    // per-level stage skipping plus per-Key command reuse.
    std::vector<DrawCommand> commands{};
    std::unordered_map<Key, NodeSnapshot> snapshots{};
    std::uint64_t font_epoch = 0;
    bool commands_dirty = true;
    // Diagnostic counters, exposed via UI::frame_stats() for tests.
    std::uint64_t evaluate_count = 0;
    std::uint64_t solve_count = 0;
    std::uint64_t command_generation_count = 0;
    std::uint64_t glyph_shape_count = 0;

    void mark(const Invalidation level) noexcept {
        pending = merge_invalidation(pending, level);
    }

    [[nodiscard]] bool focusable(const detail::ViewNode& node) const noexcept {
        return node.type == detail::ViewNode::Type::button
               || node.type == detail::ViewNode::Type::input;
    }

    [[nodiscard]] const detail::ViewNode* focused_input() const noexcept {
        if (focused_key.value == 0) {
            return nullptr;
        }
        for (const detail::ViewNode* node : nodes) {
            if (node->key == focused_key
                && node->type == detail::ViewNode::Type::input) {
                return node;
            }
        }
        return nullptr;
    }

    [[nodiscard]] WidgetState* focused_state() noexcept {
        if (focused_key.value == 0) {
            return nullptr;
        }
        const auto it = widget_states.find(focused_key);
        return it == widget_states.end() ? nullptr : &it->second;
    }

    void evaluate() {
        arena.reset();
        callbacks.clear();
        text_callbacks.clear();
        nodes.clear();

        const View root = build(arena, LocalState{&widget_states});
        root_view = root;
        detail::ViewNode* root_node = detail::ViewTreeAccess::node(root);
        if (root_node == nullptr) {
            layout = LayoutResult{};
            widget_states.clear();
            return;
        }

        // Iterative preorder: indices must match LayoutResult::boxes.
        struct Frame {
            detail::ViewNode* node;
            std::uint32_t next_child;
        };
        std::vector<Frame> stack{{root_node, 0}};
        while (!stack.empty()) {
            Frame& frame = stack.back();
            detail::ViewNode* current = frame.node;
            if (frame.next_child == 0) {
                current->index = static_cast<std::uint32_t>(nodes.size());
                nodes.push_back(current);
                if (current->type == detail::ViewNode::Type::button
                    && current->callback) {
                    if (!callbacks.emplace(current->key, current->callback).second
                        && warned_duplicate_keys.insert(current->key).second) {
                        std::println(stderr,
                                     "continui: duplicate Key {} on a button; "
                                     "keeping the first callback",
                                     current->key.value);
                    }
                }
                if (current->type == detail::ViewNode::Type::input
                    && current->text_callback) {
                    text_callbacks.emplace(current->key, current->text_callback);
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

        // Reclaim local state of widgets that disappeared this frame, and
        // initialise/refresh the ones that remain (architecture §4.3 rule 4).
        std::unordered_set<Key> alive;
        for (const detail::ViewNode* node : nodes) {
            if (focusable(*node)) {
                alive.insert(node->key);
            }
        }
        for (auto it = widget_states.begin(); it != widget_states.end();) {
            if (alive.find(it->first) == alive.end()) {
                it = widget_states.erase(it);
            } else {
                ++it;
            }
        }
        // A focused/pressed widget that disappeared must not leave a stale
        // identity behind (architecture §4.3 rule 4).
        if (focused_key.value != 0 && alive.find(focused_key) == alive.end()) {
            focused_key = Key{};
        }
        if (pressed_key.value != 0 && alive.find(pressed_key) == alive.end()) {
            pressed_key = Key{};
        }
        for (const detail::ViewNode* node : nodes) {
            if (!focusable(*node)) {
                continue;
            }
            const bool is_input = node->type == detail::ViewNode::Type::input;
            auto [it, inserted] = widget_states.try_emplace(node->key);
            WidgetState& state = it->second;
            if (inserted && is_input) {
                // A freshly created input starts with the caret at the end.
                const std::size_t chars = utf8_char_count(node->text);
                state.caret = chars;
                state.selection_start = chars;
                state.selection_end = chars;
            }
            state.kind = is_input ? ControlKind::input : ControlKind::button;
            state.focused = node->key == focused_key;
            if (is_input) {
                // The user State projection is authoritative on each frame.
                // Between frames, edited_text carries edits from event bursts.
                state.edited_text = node->text;
            }
            const std::size_t chars = utf8_char_count(state.edited_text);
            state.caret = std::min(state.caret, chars);
            state.selection_start = std::min(state.selection_start, chars);
            state.selection_end = std::min(state.selection_end, chars);
        }

        if (measurer == nullptr) {
            measurer = std::make_unique<FaceMeasurer>(font, fallback_fonts);
        }
        layout = solve(root,
                       Constraints{0.0f, static_cast<float>(viewport.width),
                                   0.0f, static_cast<float>(viewport.height)},
                       *measurer);
        ++evaluate_count;
        ++solve_count;
    }

    // 0.8a: layout-only re-solve without rebuilding the view tree. root_view
    // is still valid because the arena is only reset inside evaluate().
    void solve_only() {
        if (root_view.empty() || measurer == nullptr) {
            return;
        }
        layout = solve(root_view,
                       Constraints{0.0f, static_cast<float>(viewport.width),
                                   0.0f, static_cast<float>(viewport.height)},
                       *measurer);
        ++solve_count;
    }

    // 0.8b: regenerate draw commands for one node into `out`. No arena
    // pointers are stored — text runs and colors are value copies.
    void generate_commands(const LayoutBox& box, const detail::ViewNode& node,
                           const WidgetState* state, std::vector<DrawCommand>& out) {
        if (box.bounds.empty()) {
            return;
        }
        const Style& style = node.style;
        const bool is_interactive = node.type == detail::ViewNode::Type::button
                                    || node.type == detail::ViewNode::Type::input;
        const bool focused = state != nullptr && is_interactive && state->focused;
        if (focused) {
            constexpr Color kFocus = Color::rgb(120, 170, 255);
            constexpr float w = 2.0f;
            const Rect b = box.bounds;
            out.push_back({DrawKind::rect, {b.x - w, b.y - w, b.width + 2.0f * w, w},
                           {}, kFocus, {}, {}});
            out.push_back({DrawKind::rect, {b.x - w, b.bottom(), b.width + 2.0f * w, w},
                           {}, kFocus, {}, {}});
            out.push_back({DrawKind::rect, {b.x - w, b.y, w, b.height},
                           {}, kFocus, {}, {}});
            out.push_back({DrawKind::rect, {b.right(), b.y, w, b.height},
                           {}, kFocus, {}, {}});
        }
        if (style.background.a > 0) {
            out.push_back({DrawKind::rounded_rect, box.bounds,
                           Radius::all(style.radius), style.background, {}, {}});
        }
        if (node.type != detail::ViewNode::Type::text && !is_interactive) {
            return;
        }
        if (font == nullptr || node.text.empty() || style.foreground.a == 0) {
            return;
        }
        const GlyphRun run = fallback_fonts.empty()
                                 ? font->shape(node.text, style.font_size)
                                 : shape_with_fallback(font, fallback_fonts,
                                                       node.text, style.font_size);
        ++glyph_shape_count;
        const TextMetrics metrics = font->metrics(style.font_size);
        const float text_height = metrics.ascent + metrics.descent;
        const float x = box.content.x
                        + std::max(0.0f, (box.content.width - run.width) * 0.5f);
        const float y = box.content.y
                        + std::max(0.0f, (box.content.height - text_height) * 0.5f)
                        + metrics.ascent;
        out.push_back({DrawKind::text, {}, {}, style.foreground, run, {x, y}});

        if (node.type == detail::ViewNode::Type::input && focused && state != nullptr) {
            const auto x_at_char = [&](const std::size_t char_index) {
                float px = x;
                const std::size_t count = std::min(char_index, run.glyphs.size());
                for (std::size_t g = 0; g < count; ++g) {
                    px += run.glyphs[g].advance;
                }
                return px;
            };
            const float top = y - metrics.ascent;
            ime_text_bounds_ = Rect{x, top, run.width, text_height};
            if (state->selection_start != state->selection_end) {
                const float s0 = x_at_char(std::min(state->selection_start,
                                                    state->selection_end));
                const float s1 = x_at_char(std::max(state->selection_start,
                                                    state->selection_end));
                if (s1 > s0) {
                    out.push_back({DrawKind::rect, {s0, top, s1 - s0, text_height}, {},
                                   Color::rgba(80, 120, 200, 120), {}, {}});
                }
            }
            float caret_x = x_at_char(state->caret);
            if (state->composing && !state->composition.empty()) {
                const GlyphRun preview = fallback_fonts.empty()
                    ? font->shape(state->composition, style.font_size)
                    : shape_with_fallback(font, fallback_fonts,
                                          state->composition, style.font_size);
                ++glyph_shape_count;
                out.push_back({DrawKind::text, {}, {},
                               Color::rgba(255, 255, 120, 255), preview, {caret_x, y}});
                caret_x += preview.width;
            }
            const float caret_h = std::max(2.0f, text_height * 0.9f);
            ime_caret_ = Rect{caret_x, top, 1.5f, caret_h};
            out.push_back({DrawKind::rect, ime_caret_, {},
                           style.foreground, {}, {}});
        }
    }

    // 0.8a/0.8b: build/reuse commands, then replay them into the frame's
    // Painter. Only nodes whose visual signature changed regenerate commands.
    void build_commands() {
        commands.clear();
        ime_caret_ = Rect{};
        ime_text_bounds_ = Rect{};
        // Reclaim snapshots of nodes that disappeared this frame (0.8 audit
        // F2): without this, churning keys would grow the glyph cache forever.
        std::unordered_set<Key> alive;
        alive.reserve(nodes.size());
        for (const detail::ViewNode* node : nodes) {
            alive.insert(node->key);
        }
        for (auto it = snapshots.begin(); it != snapshots.end();) {
            if (alive.find(it->first) == alive.end()) {
                it = snapshots.erase(it);
            } else {
                ++it;
            }
        }
        for (std::size_t i = 0; i < layout.boxes.size() && i < nodes.size(); ++i) {
            const LayoutBox& box = layout.boxes[i];
            const detail::ViewNode& node = *nodes[i];
            const auto it = widget_states.find(node.key);
            const WidgetState* state = it == widget_states.end() ? nullptr : &it->second;
            NodeVisual visual;
            visual.style = node.style;
            visual.text = node.text;
            visual.bounds = box.bounds;
            visual.content = box.content;
            visual.type = static_cast<std::uint8_t>(node.type);
            visual.font_epoch = font_epoch;
            if (state != nullptr) {
                visual.focused = state->focused;
                visual.composing = state->composing;
                visual.composition = state->composition;
                visual.caret = state->caret;
                visual.selection_start = state->selection_start;
                visual.selection_end = state->selection_end;
            }

            const auto cached = snapshots.find(node.key);
            if (cached != snapshots.end()
                && cached->second.visual.same_as(visual)) {
                // Reuse: append cached commands, restore IME geometry.
                commands.insert(commands.end(), cached->second.commands.begin(),
                                cached->second.commands.end());
                if (visual.focused && visual.type
                        == static_cast<std::uint8_t>(detail::ViewNode::Type::input)) {
                    ime_caret_ = cached->second.ime_caret;
                    ime_text_bounds_ = cached->second.ime_text_bounds;
                }
                continue;
            }

            // Changed node: regenerate its commands and cache them by Key.
            const std::size_t begin = commands.size();
            generate_commands(box, node, state, commands);
            ++command_generation_count;
            NodeSnapshot snapshot;
            snapshot.visual = visual;
            snapshot.commands.assign(commands.begin() + static_cast<std::ptrdiff_t>(begin),
                                     commands.end());
            snapshot.ime_caret = ime_caret_;
            snapshot.ime_text_bounds = ime_text_bounds_;
            snapshots[node.key] = std::move(snapshot);
        }
        commands_dirty = false;
    }

    void replay(Painter& painter) {
        for (const DrawCommand& cmd : commands) {
            switch (cmd.kind) {
                case DrawKind::rect:
                    painter.rect(cmd.bounds, Paint{cmd.color});
                    break;
                case DrawKind::rounded_rect:
                    painter.rounded_rect(cmd.bounds, cmd.radius, Paint{cmd.color});
                    break;
                case DrawKind::text:
                    painter.text(cmd.run, cmd.origin, Paint{cmd.color});
                    break;
            }
        }
    }

    void set_focus(const Key key) noexcept {
        if (focused_key == key) {
            return;
        }
        if (focused_key.value != 0) {
            const auto it = widget_states.find(focused_key);
            if (it != widget_states.end()) {
                it->second.focused = false;
                it->second.composing = false;
            }
        }
        focused_key = key;
        if (key.value != 0) {
            const auto it = widget_states.find(key);
            if (it != widget_states.end()) {
                it->second.focused = true;
            }
        }
        // LocalState feeds ViewFn; C3 forbids implicit tracking, so re-evaluate.
        mark(Invalidation::view);
    }

    void move_focus(const int direction) noexcept {
        std::vector<Key> keys;
        for (const detail::ViewNode* node : nodes) {
            if (focusable(*node)) {
                keys.push_back(node->key);
            }
        }
        if (keys.empty()) {
            return;
        }
        const auto it = std::find(keys.begin(), keys.end(), focused_key);
        std::size_t next = 0;
        if (focused_key.value != 0 && it != keys.end()) {
            const std::size_t index = static_cast<std::size_t>(std::distance(keys.begin(), it));
            next = (index + static_cast<std::size_t>(direction) + keys.size()) % keys.size();
        } else if (direction < 0) {
            next = keys.size() - 1;
        }
        set_focus(keys[next]);
    }

    void insert_text(const std::string_view text) {
        const detail::ViewNode* node = focused_input();
        if (node == nullptr || text.empty()) {
            return;
        }
        const auto callback = text_callbacks.find(node->key);
        if (callback == text_callbacks.end() || !callback->second) {
            return;
        }
        WidgetState& state = widget_states[node->key];
        // Edit against the authoritative buffer, never the frame-stale arena
        // node (0.7 audit F2/F3): a burst of events before the next repaint
        // must accumulate rather than overwrite.
        std::string edited = state.edited_text;
        edited.insert(utf8_byte_offset(edited, state.caret), text);
        state.caret += utf8_char_count(text);
        state.selection_start = state.caret;
        state.selection_end = state.caret;
        state.edited_text = edited;
        callback->second(edited);  // user updates State and invalidates
        mark(Invalidation::view);
    }

    void delete_selection_or_char(const bool backspace) {
        const detail::ViewNode* node = focused_input();
        if (node == nullptr) {
            return;
        }
        const auto callback = text_callbacks.find(node->key);
        if (callback == text_callbacks.end() || !callback->second) {
            return;
        }
        WidgetState& state = widget_states[node->key];
        const std::string& base = state.edited_text;
        const std::size_t chars = utf8_char_count(base);
        std::size_t first_char = 0;
        std::size_t last_char = 0;
        if (state.selection_start != state.selection_end) {
            first_char = std::min(state.selection_start, state.selection_end);
            last_char = std::max(state.selection_start, state.selection_end);
        } else if (backspace) {
            if (state.caret == 0) {
                return;
            }
            first_char = state.caret - 1;
            last_char = state.caret;
        } else {
            if (state.caret >= chars) {
                return;
            }
            first_char = state.caret;
            last_char = state.caret + 1;
        }
        const std::size_t first_byte = utf8_byte_offset(base, first_char);
        const std::size_t last_byte = utf8_byte_offset(base, last_char);
        std::string edited = base;
        if (first_byte < last_byte && last_byte <= edited.size()) {
            edited.erase(first_byte, last_byte - first_byte);
        }
        state.edited_text = edited;
        state.caret = first_char;  // caret is a CHARACTER index (not bytes)
        state.selection_start = state.caret;
        state.selection_end = state.caret;
        callback->second(edited);
        mark(Invalidation::view);
    }

    void move_caret(const int direction) {
        WidgetState* state = focused_state();
        const detail::ViewNode* node = focused_input();
        if (state == nullptr || node == nullptr) {
            return;
        }
        const std::size_t chars = utf8_char_count(state->edited_text);
        if (direction < 0) {
            state->caret = state->caret == 0 ? 0 : state->caret - 1;
        } else {
            state->caret = std::min(chars, state->caret + 1);
        }
        state->selection_start = state->caret;
        state->selection_end = state->caret;
        // LocalState feeds ViewFn; C3 forbids implicit tracking, so re-evaluate.
        mark(Invalidation::view);
    }

    [[nodiscard]] Key hit_key(const Event& event) const noexcept {
        const auto index = hit_test(layout,
                                    PointF{static_cast<float>(event.position.x),
                                           static_cast<float>(event.position.y)});
        if (!index || *index >= nodes.size()) {
            return Key{};
        }
        return nodes[*index]->key;
    }

    bool handle_event(const Event& event) {
        switch (event.type) {
            case EventType::mouse_move: {
                const Key hit = hit_key(event);
                bool changed = false;
                for (auto& [key, state] : widget_states) {
                    const bool now = key == hit;
                    if (state.hovered != now) {
                        state.hovered = now;
                        changed = true;
                    }
                }
                if (changed) {
                    // LocalState feeds ViewFn; C3 forbids implicit tracking, so re-evaluate.
                    mark(Invalidation::view);
                }
                return changed;
            }
            case EventType::mouse_button_down: {
                if (event.button != MouseButton::left) {
                    return false;
                }
                const auto index = hit_test(layout,
                    PointF{static_cast<float>(event.position.x),
                           static_cast<float>(event.position.y)});
                if (!index || *index >= nodes.size()) {
                    return false;
                }
                const detail::ViewNode& node = *nodes[*index];
                if (!focusable(node)) {
                    return false;
                }
                pressed_key = node.key;
                const auto it = widget_states.find(node.key);
                if (it != widget_states.end()) {
                    it->second.pressed = true;
                }
                set_focus(node.key);
                return true;
            }
            case EventType::mouse_button_up: {
                if (event.button != MouseButton::left) {
                    return false;
                }
                const Key hit = hit_key(event);
                const Key was_pressed = pressed_key;
                pressed_key = Key{};
                for (auto& [key, state] : widget_states) {
                    state.pressed = false;
                }
                if (was_pressed.value != 0 && hit == was_pressed) {
                    const auto callback = callbacks.find(was_pressed);
                    if (callback != callbacks.end() && callback->second) {
                        callback->second();  // may mutate State + invalidate
                        return true;
                    }
                    if (text_callbacks.find(was_pressed) != text_callbacks.end()) {
                        return true;
                    }
                }
                // LocalState feeds ViewFn; C3 forbids implicit tracking, so re-evaluate.
                mark(Invalidation::view);
                return false;
            }
            case EventType::key_down: {
                switch (event.key) {
                    case KeyCode::tab:
                        move_focus(1);
                        return true;
                    case KeyCode::enter:
                    case KeyCode::space: {
                        if (focused_key.value != 0) {
                            const auto callback = callbacks.find(focused_key);
                            if (callback != callbacks.end() && callback->second) {
                                callback->second();
                                return true;
                            }
                        }
                        return false;
                    }
                    case KeyCode::backspace:
                    case KeyCode::del:
                        delete_selection_or_char(event.key == KeyCode::backspace);
                        return true;
                    case KeyCode::left:
                    case KeyCode::right:
                        move_caret(event.key == KeyCode::left ? -1 : 1);
                        return true;
                    default:
                        return false;
                }
            }
            case EventType::text_input:
                insert_text(event.text);
                return !event.text.empty();
            case EventType::composition_start: {
                WidgetState* state = focused_state();
                if (state == nullptr) {
                    return false;
                }
                state->composing = true;
                state->composition.clear();
                // LocalState feeds ViewFn; C3 forbids implicit tracking, so re-evaluate.
                mark(Invalidation::view);
                return true;
            }
            case EventType::composition_update: {
                WidgetState* state = focused_state();
                if (state == nullptr || !state->composing) {
                    return false;
                }
                state->composition = event.text;
                // LocalState feeds ViewFn; C3 forbids implicit tracking, so re-evaluate.
                mark(Invalidation::view);
                return true;
            }
            case EventType::composition_end: {
                WidgetState* state = focused_state();
                if (state == nullptr || !state->composing) {
                    return false;
                }
                state->composing = false;
                state->composition.clear();
                // Commit the event payload, NOT the preview: TSF sends an
                // empty payload when a text_input already committed this
                // composition (single-commit path, 0.7 audit F9).
                insert_text(event.text);
                return true;
            }
            default:
                return false;
        }
    }
};

UI::UI(ViewFn build) : impl_(std::make_unique<Impl>(std::move(build))) {}
UI::~UI() = default;
UI::UI(UI&&) noexcept = default;
UI& UI::operator=(UI&&) noexcept = default;

void UI::invalidate(const Invalidation level) noexcept {
    impl_->mark(level);
}

Invalidation UI::pending() const noexcept { return impl_->pending; }

void UI::set_font(const FontFace* face) {
    impl_->font = face;
    impl_->measurer = std::make_unique<FaceMeasurer>(face, impl_->fallback_fonts);
    ++impl_->font_epoch;  // invalidates cached text runs / glyph geometry
    impl_->mark(Invalidation::layout);
}

void UI::set_fallback_fonts(const std::span<const FontFace* const> faces) {
    impl_->fallback_fonts.assign(faces.begin(), faces.end());
    impl_->measurer = std::make_unique<FaceMeasurer>(impl_->font,
                                                     impl_->fallback_fonts);
    ++impl_->font_epoch;
    impl_->mark(Invalidation::layout);
}

void UI::set_text_measurer(std::unique_ptr<TextMeasurer> measurer) {
    impl_->measurer = std::move(measurer);
    ++impl_->font_epoch;
    impl_->mark(Invalidation::layout);
}

void UI::paint(Painter& painter, const Size viewport) {
    if (viewport != impl_->viewport) {
        impl_->mark(Invalidation::layout);
        impl_->viewport = viewport;
    }
    // 0.8a: the invalidation level decides which stages run. Counts reset each
    // frame so frame_stats() reports work done by THIS paint() call.
    impl_->evaluate_count = 0;
    impl_->solve_count = 0;
    impl_->command_generation_count = 0;
    impl_->glyph_shape_count = 0;
    const Invalidation level = impl_->pending;
    if (level == Invalidation::full) {
        // Full forces every stage including discarding possibly-stale caches.
        impl_->snapshots.clear();
    }
    if (at_least(level, Invalidation::view)) {
        impl_->evaluate();               // build + solve; arena reset here
        impl_->commands_dirty = true;
    } else if (at_least(level, Invalidation::layout)) {
        impl_->solve_only();             // re-solve existing tree, no rebuild
        impl_->commands_dirty = true;
    }
    // paint / none: reuse last frame's layout; commands may still be rebuilt
    // if a widget's visual signature changed (e.g. focus/caret).
    impl_->pending = Invalidation::none;
    if (impl_->commands_dirty || level == Invalidation::paint) {
        impl_->build_commands();
    }
    impl_->replay(painter);
}

bool UI::handle_event(const Event& event) { return impl_->handle_event(event); }

const LayoutResult& UI::layout() const noexcept { return impl_->layout; }

FrameStats UI::frame_stats() const noexcept {
    return FrameStats{impl_->evaluate_count, impl_->solve_count,
                      impl_->command_generation_count, impl_->glyph_shape_count};
}

ImeContext UI::ime_context() const noexcept {
    ImeContext context;
    context.caret = impl_->ime_caret_;
    context.text_bounds = impl_->ime_text_bounds_;
    for (const detail::ViewNode* node : impl_->nodes) {
        if (node->key == impl_->focused_key
            && node->type == detail::ViewNode::Type::input) {
            context.text = node->text;
            break;
        }
    }
    return context;
}

}  // namespace continui
