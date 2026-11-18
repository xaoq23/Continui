// Internal view tree definition (Phase 0.6). Nodes live only in the frame
// arena; View is their public value handle, ViewTreeAccess is the sanctioned
// internal bridge (the public API does not expose the tree).
#pragma once

#include <continui/view.hpp>

#include <cstdint>
#include <string>

namespace continui::detail {

struct ViewTreeAccess;

struct ViewNode {
    enum class Type : std::uint8_t { row, column, text, button, rect };

    Type type = Type::rect;
    Key key{};
    Style style{};
    std::string text;
    Callback callback;
    ViewNode** children = nullptr;  // arena array of non-owning pointers
    std::uint32_t child_count = 0;
    std::uint32_t index = 0;  // preorder index, matches LayoutResult::boxes
};

struct ViewTreeAccess {
    [[nodiscard]] static ViewNode* node(const View& view) noexcept {
        return view.node_;
    }
    [[nodiscard]] static View wrap(ViewNode* node) noexcept {
        return View(node);
    }
};

}  // namespace continui::detail
