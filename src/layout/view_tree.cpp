#include "view_tree.hpp"

#include <continui/view.hpp>

namespace continui {

namespace {
using continui::detail::ViewNode;

View make(FrameArena& arena, ViewNode::Type type, Key key, Style style,
          std::initializer_list<View> children, std::string_view text,
          Callback callback, TextCallback text_callback = {}) {
    ViewNode* node = arena.create<ViewNode>();
    node->type = type;
    node->key = key;
    node->style = style;
    node->text = std::string(text);
    node->callback = std::move(callback);
    node->text_callback = std::move(text_callback);
    node->child_count = static_cast<std::uint32_t>(children.size());
    if (children.size() > 0) {
        node->children = arena.allocate_array<ViewNode*>(children.size());
        std::size_t i = 0;
        for (const View& child : children) {
            node->children[i++] = detail::ViewTreeAccess::node(child);
        }
    }
    return detail::ViewTreeAccess::wrap(node);
}

Key auto_key(FrameArena& arena) {
    return Key{arena.next_auto_key()};
}

}  // namespace

View Row(FrameArena& arena, std::initializer_list<View> children, Style style) {
    return make(arena, ViewNode::Type::row, auto_key(arena), style, children, {}, {});
}
View Column(FrameArena& arena, std::initializer_list<View> children, Style style) {
    return make(arena, ViewNode::Type::column, auto_key(arena), style, children, {}, {});
}
View Text(FrameArena& arena, std::string_view content, Style style) {
    return make(arena, ViewNode::Type::text, auto_key(arena), style, {}, content, {});
}
View Button(FrameArena& arena, std::string_view label, Callback callback, Style style) {
    return make(arena, ViewNode::Type::button, auto_key(arena), style, {}, label,
                std::move(callback));
}
View Input(FrameArena& arena, std::string_view text, TextCallback on_change, Style style) {
    return make(arena, ViewNode::Type::input, auto_key(arena), style, {}, text, {},
                std::move(on_change));
}
View Solid(FrameArena& arena, Style style) {
    return make(arena, ViewNode::Type::rect, auto_key(arena), style, {}, {}, {});
}

View Row(FrameArena& arena, Key key, std::initializer_list<View> children, Style style) {
    return make(arena, ViewNode::Type::row, key, style, children, {}, {});
}
View Column(FrameArena& arena, Key key, std::initializer_list<View> children, Style style) {
    return make(arena, ViewNode::Type::column, key, style, children, {}, {});
}
View Text(FrameArena& arena, Key key, std::string_view content, Style style) {
    return make(arena, ViewNode::Type::text, key, style, {}, content, {});
}
View Button(FrameArena& arena, Key key, std::string_view label, Callback callback,
            Style style) {
    return make(arena, ViewNode::Type::button, key, style, {}, label, std::move(callback));
}
View Input(FrameArena& arena, Key key, std::string_view text, TextCallback on_change,
           Style style) {
    return make(arena, ViewNode::Type::input, key, style, {}, text, {},
                std::move(on_change));
}
View Solid(FrameArena& arena, Key key, Style style) {
    return make(arena, ViewNode::Type::rect, key, style, {}, {}, {});
}

}  // namespace continui
