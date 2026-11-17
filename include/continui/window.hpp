// Window: top-level surface (docs/architecture.md §4.8).
//
// Status: skeleton (Phase 0.1). Creation via App, per-window invalidation
// context, focus chain, and DPI state arrive with Phase 0.2.
#pragma once

namespace continui {

struct WindowDesc {
    // Draft fields; extended in Phase 0.2. Sizes are logical pixels.
    const char* title = "";
    int width = 800;
    int height = 600;
    bool resizable = true;
};

class Window {
public:
    Window() = default;
};

}  // namespace continui
