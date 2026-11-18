// View: the public declarative UI description (Phase 0.6, architecture §4.1).
//
// `View = render(State, Style)`: a View is a cheap value handle to a node
// living in the current frame's FrameArena. Rebuild the tree every frame from
// the user's state; never retain a View (or any node pointer) past arena
// reset(). The full reconciliation/diff arrives in Phase 0.8.
#pragma once

#include <continui/arena.hpp>
#include <continui/key.hpp>
#include <continui/style.hpp>

#include <functional>
#include <initializer_list>
#include <string_view>

namespace continui {

namespace detail {
struct ViewNode;     // internal, defined in src/layout/view_tree.hpp
struct ViewTreeAccess;  // internal bridge for solver/orchestration
}

using Callback = std::function<void()>;
// Text Input reports the edited UTF-8 string back to the user; the user owns
// the business text and updates State inside this callback.
using TextCallback = std::function<void(std::string_view)>;

class View {
public:
    View() = default;  // empty: paints nothing

    // Copyable value handle; the pointee is owned by the frame arena and is
    // not exposed through the public API (internal code uses ViewTreeAccess).
    [[nodiscard]] bool empty() const noexcept { return node_ == nullptr; }

private:
    friend struct detail::ViewTreeAccess;
    friend View Row(FrameArena&, std::initializer_list<View>, Style);
    friend View Column(FrameArena&, std::initializer_list<View>, Style);
    friend View Text(FrameArena&, std::string_view, Style);
    friend View Button(FrameArena&, std::string_view, Callback, Style);
    friend View Input(FrameArena&, std::string_view, TextCallback, Style);
    friend View Solid(FrameArena&, Style);
    friend View Row(FrameArena&, Key, std::initializer_list<View>, Style);
    friend View Column(FrameArena&, Key, std::initializer_list<View>, Style);
    friend View Text(FrameArena&, Key, std::string_view, Style);
    friend View Button(FrameArena&, Key, std::string_view, Callback, Style);
    friend View Input(FrameArena&, Key, std::string_view, TextCallback, Style);
    friend View Solid(FrameArena&, Key, Style);

    explicit View(detail::ViewNode* node) noexcept : node_(node) {}
    detail::ViewNode* node_ = nullptr;
};

// Position-ordered auto keys (arena.next_auto_key()) are stable only while
// the tree structure does not reorder; use the explicit-Key overloads for
// lists whose identity must outlive a reorder.
View Row(FrameArena& arena, std::initializer_list<View> children, Style style = {});
View Column(FrameArena& arena, std::initializer_list<View> children, Style style = {});
View Text(FrameArena& arena, std::string_view content, Style style = {});
View Button(FrameArena& arena, std::string_view label, Callback callback, Style style = {});
// Single-line text input. `text` is the current value from user state;
// `on_change` is called with the edited value when keyboard/IME input occurs.
View Input(FrameArena& arena, std::string_view text, TextCallback on_change,
           Style style = {});
View Solid(FrameArena& arena, Style style = {});

View Row(FrameArena& arena, Key key, std::initializer_list<View> children, Style style = {});
View Column(FrameArena& arena, Key key, std::initializer_list<View> children, Style style = {});
View Text(FrameArena& arena, Key key, std::string_view content, Style style = {});
View Button(FrameArena& arena, Key key, std::string_view label, Callback callback, Style style = {});
View Input(FrameArena& arena, Key key, std::string_view text, TextCallback on_change,
           Style style = {});
View Solid(FrameArena& arena, Key key, Style style = {});

}  // namespace continui
