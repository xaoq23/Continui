// Window: top-level surface (docs/architecture.md §4.8).
//
// Status: Phase 0.2 (Win32 backend). Creation, show/hide, size queries, and
// the draft event surface land here; per-window invalidation context, focus
// chain, and DPI state arrive with later phases.
#pragma once

#include <continui/events.hpp>
#include <continui/geometry.hpp>

#include <string_view>

namespace continui {

namespace detail {
struct WindowImpl;
}

// Forward declarations so the platform-layer accessor can be declared
// before the class definition and friended inside it.
class Window;

// Defined in the platform layer (platform_window.cpp); the controlled
// consumer-facing declaration is <continui/platform_windows.hpp>. Window
// grants this function friendship so it can return the native handle
// without widening the public Window API.
namespace platform_windows {
void* native_window_handle(const Window& window);
}

enum class WindowError {
    invalid_desc,       // non-positive client width/height
    creation_failed,    // the platform rejected the window
    app_invalid,        // the owning App was moved-from
};

struct WindowDesc {
    // Draft fields; sizes are client-area pixels (physical; the logical
    // coordinate system arrives with layout in Phase 0.6).
    const char* title = "";
    int width = 800;
    int height = 600;
    bool resizable = true;
};

// Non-owning handle to a window owned by an App. Handles are cheap and
// copyable; they must not outlive their App.
class Window {
public:
    Window() = default;

    // Invoked on the pumping thread (main thread) for every event of this
    // window. Handler exceptions propagate to the caller of App::run() /
    // run_one_frame(); the pump performs no catch. This is the Phase 0.2
    // exception boundary (docs/architecture.md §10.3, ADR-019).
    void set_event_handler(EventHandler handler);

    void show();
    void hide();
    bool visible() const;

    // Current client-area size in pixels.
    Size client_size() const;

    // Feeds the platform IME (Windows TSF) the focused input's text and caret
    // rectangle in client pixels, for candidate positioning and text sync.
    // No-op when the window has no IME context.
    void set_ime_context(std::string_view text, Rect caret, Rect text_bounds);

    // False only for a default-constructed handle.
    bool valid() const;

private:
    friend class App;
    friend void* platform_windows::native_window_handle(const Window&);
    explicit Window(detail::WindowImpl* impl) : impl_(impl) {}

    detail::WindowImpl* impl_ = nullptr;
};

}  // namespace continui
