// Controlled access to the Win32 native window handle
// (docs/architecture.md §2.2 platform extension API).
//
// Opt-in and Windows-only by definition: including this header couples your
// build to the Win32 backend. The umbrella headers never expose HWND.
#pragma once

#include <continui/window.hpp>

namespace continui::platform_windows {

// Returns the Win32 HWND of window as a void*, or nullptr when the window
// was default-constructed or already destroyed.
[[nodiscard]] void* native_window_handle(const Window& window);

}  // namespace continui::platform_windows
