// Internal Win32 platform state (not part of the public API).
#pragma once

#include <continui/events.hpp>

#include "ime_tsf.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Must precede <windows.h>: the SDK version-gates the per-monitor DPI v2
// declarations on _WIN32_WINNT (older MinGW defaults low), and windows.h
// defines min/max macros unless suppressed.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>

namespace continui::detail {

inline constexpr wchar_t kWindowClassName[] = L"continui_window";

struct AppImpl;

struct WindowImpl {
    HWND hwnd = nullptr;    // null once the window is destroyed
    std::function<void(const Event&)> handler;
    bool visible = false;
    bool closed = false;
    std::uint32_t pending_high_surrogate = 0;  // WM_CHAR UTF-16 pair across messages
    std::unique_ptr<TsfInputContext> tsf;  // Windows IME (architecture §8.3)
};

struct AppImpl {
    HINSTANCE instance = nullptr;
    std::vector<std::unique_ptr<WindowImpl>> windows;
    std::function<void()> frame_callback;
    bool quit_requested = false;

    bool has_active_windows() const {
        for (const auto& window : windows) {
            if (!window->closed) return true;
        }
        return false;
    }
};

bool register_window_class(HINSTANCE instance);
void destroy_all_windows(AppImpl& app);
std::wstring utf8_to_utf16(const char* text);
LRESULT CALLBACK window_procedure(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

}  // namespace continui::detail
