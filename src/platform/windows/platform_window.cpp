// Window implementation, event translation, and the Win32 window procedure
// (Phase 0.2).

#include "internal.hpp"

#include <continui/app.hpp>
#include <continui/platform_windows.hpp>
#include <continui/window.hpp>

#include <cstdint>

namespace continui {
namespace detail {

namespace {

KeyCode key_code_from_vk(WPARAM vk) {
    using KC = KeyCode;
    switch (vk) {
        case VK_BACK: return KC::backspace;
        case VK_TAB: return KC::tab;
        case VK_RETURN: return KC::enter;
        case VK_ESCAPE: return KC::escape;
        case VK_SPACE: return KC::space;
        case VK_DELETE: return KC::del;
        case VK_LEFT: return KC::left;
        case VK_RIGHT: return KC::right;
        case VK_UP: return KC::up;
        case VK_DOWN: return KC::down;
        // Generic VKs cannot distinguish keyboard sides; approximate as left.
        case VK_SHIFT:
        case VK_LSHIFT: return KC::left_shift;
        case VK_RSHIFT: return KC::right_shift;
        case VK_CONTROL:
        case VK_LCONTROL: return KC::left_control;
        case VK_RCONTROL: return KC::right_control;
        case VK_MENU:
        case VK_LMENU: return KC::left_alt;
        case VK_RMENU: return KC::right_alt;
        default: break;
    }
    if (vk >= 'A' && vk <= 'Z') {
        return static_cast<KeyCode>(static_cast<std::uint32_t>(KC::a) + (vk - 'A'));
    }
    if (vk >= '0' && vk <= '9') {
        return static_cast<KeyCode>(static_cast<std::uint32_t>(KC::digit_0) + (vk - '0'));
    }
    return KC::unknown;
}

// GetKeyState reflects the thread's key state as of the current message;
// SendMessage-injected keys do not update it (tested via the Alt context bit).
Modifiers modifiers_now() {
    unsigned value = 0;
    if (GetKeyState(VK_SHIFT) < 0) value |= static_cast<unsigned>(Modifiers::shift);
    if (GetKeyState(VK_CONTROL) < 0) value |= static_cast<unsigned>(Modifiers::control);
    if (GetKeyState(VK_MENU) < 0) value |= static_cast<unsigned>(Modifiers::alt);
    return static_cast<Modifiers>(value);
}

MouseButton mouse_button_from_message(UINT message, WPARAM w_param) {
    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP: return MouseButton::left;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: return MouseButton::right;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP: return MouseButton::middle;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            return GET_XBUTTON_WPARAM(w_param) == XBUTTON1 ? MouseButton::x1 : MouseButton::x2;
        default: return MouseButton::none;
    }
}

bool is_button_down_message(UINT message) {
    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN: return true;
        default: return false;
    }
}

std::string utf8_encode_codepoint(std::uint32_t cp) {
    std::string out;
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
    return out;
}

void deliver(WindowImpl& window, const Event& event) {
    if (window.handler) {
        window.handler(event);
    }
}

// GET_X_LPARAM/GET_Y_LPARAM equivalent; the cast sign-extends coordinates.
Point client_position(LPARAM l_param) {
    return Point{static_cast<short>(LOWORD(l_param)), static_cast<short>(HIWORD(l_param))};
}

}  // namespace

LRESULT CALLBACK window_procedure(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }

    auto* impl = reinterpret_cast<WindowImpl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (impl == nullptr) {
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }

    switch (message) {
        case WM_CLOSE:
            deliver(*impl, Event{.type = EventType::window_close_requested,
                                 .text = {}});
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            impl->closed = true;
            impl->visible = false;
            impl->hwnd = nullptr;
            return 0;

        case WM_SIZE:
            if (w_param != SIZE_MINIMIZED) {
                Event event{.type = EventType::window_resized, .text = {}};
                event.size = Size{static_cast<int>(LOWORD(l_param)), static_cast<int>(HIWORD(l_param))};
                deliver(*impl, event);
            }
            return 0;

        case WM_SETFOCUS:
            if (impl->tsf) {
                impl->tsf->on_focus(true);
            }
            return 0;

        case WM_KILLFOCUS:
            if (impl->tsf) {
                impl->tsf->on_focus(false);
            }
            return 0;

        case WM_CHAR: {
            // Plain characters arrive here; IME composition is handled by TSF
            // and must NOT be converted into key events (architecture §8.3).
            if (w_param == VK_BACK || w_param == VK_TAB
                || w_param == VK_RETURN || w_param == VK_ESCAPE) {
                break;
            }
            const wchar_t ch = static_cast<wchar_t>(w_param);
            std::uint32_t codepoint = static_cast<std::uint32_t>(ch);
            // WM_CHAR delivers UTF-16 surrogate pairs as two messages; buffer
            // a high surrogate so non-BMP codepoints form valid UTF-8 (F10).
            if (impl->pending_high_surrogate != 0) {
                if (ch >= 0xDC00 && ch <= 0xDFFF) {
                    codepoint = 0x10000u
                                + ((impl->pending_high_surrogate - 0xD800u) << 10)
                                + static_cast<std::uint32_t>(ch - 0xDC00u);
                }
                impl->pending_high_surrogate = 0;
            } else if (ch >= 0xD800 && ch <= 0xDBFF) {
                impl->pending_high_surrogate = static_cast<std::uint32_t>(ch);
                return 0;
            }
            Event event{.type = EventType::text_input, .text = {}};
            event.text = utf8_encode_codepoint(codepoint);
            deliver(*impl, event);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd, &paint);  // validate; real painting is Phase 0.3
            EndPaint(hwnd, &paint);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            Event event{};
            event.type = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
                             ? EventType::key_down
                             : EventType::key_up;
            event.key = key_code_from_vk(w_param);
            event.modifiers = modifiers_now();
            // Context bit: Alt held. Read from lParam so injected messages
            // (which skip the thread key state) still carry it.
            if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) && (l_param & (1u << 29))) {
                event.modifiers = event.modifiers | Modifiers::alt;
            }
            deliver(*impl, event);
            break;  // DefWindowProc handles menu/Alt semantics
        }

        case WM_MOUSEMOVE: {
            Event event{.type = EventType::mouse_move, .text = {}};
            event.position = client_position(l_param);
            event.modifiers = modifiers_now();
            deliver(*impl, event);
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            Event event{};
            event.type = is_button_down_message(message) ? EventType::mouse_button_down
                                                         : EventType::mouse_button_up;
            event.button = mouse_button_from_message(message, w_param);
            event.position = client_position(l_param);
            event.modifiers = modifiers_now();
            deliver(*impl, event);
            // XBUTTON messages fall through so DefWindowProc posts them to
            // sibling windows (documented Win32 requirement).
            if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP) {
                break;
            }
            return 0;
        }

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, w_param, l_param);
}

}  // namespace detail

// ---- Public Window methods (out-of-line to keep Win32 out of headers) ----

void Window::set_event_handler(EventHandler handler) {
    if (impl_ != nullptr) {
        impl_->handler = std::move(handler);
    }
}

void Window::set_ime_context(const std::string_view text, const Rect caret,
                             const Rect text_bounds) {
    if (impl_ != nullptr && impl_->tsf != nullptr) {
        impl_->tsf->sync_state(text, caret, text_bounds);
    }
}

void Window::show() {
    if (impl_ != nullptr && impl_->hwnd != nullptr) {
        ShowWindow(impl_->hwnd, SW_SHOWNORMAL);
        impl_->visible = true;
    }
}

void Window::hide() {
    if (impl_ != nullptr && impl_->hwnd != nullptr) {
        ShowWindow(impl_->hwnd, SW_HIDE);
        impl_->visible = false;
    }
}

bool Window::visible() const {
    return impl_ != nullptr && impl_->visible;
}

Size Window::client_size() const {
    if (impl_ == nullptr || impl_->hwnd == nullptr) {
        return Size{};
    }
    RECT client{};
    if (!GetClientRect(impl_->hwnd, &client)) {
        return Size{};
    }
    return Size{client.right - client.left, client.bottom - client.top};
}

bool Window::valid() const {
    return impl_ != nullptr;
}

}  // namespace continui

namespace continui::platform_windows {

void* native_window_handle(const Window& window) {
    return window.impl_ != nullptr ? static_cast<void*>(window.impl_->hwnd) : nullptr;
}

}  // namespace continui::platform_windows
