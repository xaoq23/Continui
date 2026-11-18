// App lifecycle and the message pump for the Win32 backend (Phase 0.2).
//
// Structure note: App/Window are public contracts declared in include/, but
// in Phase 0.2 their implementation lives in the platform module because
// the runtime orchestration is platform-specific. When a second platform
// arrives, this split needs a platform-neutral App core with per-platform
// ports (ADR candidate).

#include "internal.hpp"

#include <continui/app.hpp>

#include <expected>

namespace continui {
namespace detail {

bool register_window_class(HINSTANCE instance) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &window_procedure;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    const HBRUSH background = CreateSolidBrush(RGB(30, 30, 40));
    window_class.hbrBackground = background;
    window_class.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&window_class) == 0) {
        const DWORD error = GetLastError();
        if (background != nullptr) {
            DeleteObject(background);  // the registered class keeps its own brush
        }
        // A second App in the same process reuses the registered class.
        return error == ERROR_CLASS_ALREADY_EXISTS;
    }
    return true;
}

void destroy_all_windows(AppImpl& app) {
    for (auto& window : app.windows) {
        if (window->hwnd != nullptr) {
            DestroyWindow(window->hwnd);
        }
    }
}

std::wstring utf8_to_utf16(const char* text) {
    if (text == nullptr) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 1) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
    return result;
}

}  // namespace detail

namespace {

// Per-monitor DPI awareness from day one (docs/architecture.md §8.4). The
// flag is set best-effort; resolving per-monitor details is Phase 2.
void make_process_dpi_aware() {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return;
    }
    using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const auto set_context = reinterpret_cast<SetContextFn>(reinterpret_cast<void*>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
    if (set_context != nullptr) {
        set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

}  // namespace

std::expected<App, AppError> App::create() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (instance == nullptr || !detail::register_window_class(instance)) {
        return std::unexpected(AppError::platform_init_failed);
    }
    make_process_dpi_aware();

    App app;
    app.impl_ = std::make_unique<detail::AppImpl>();
    app.impl_->instance = instance;
    return app;
}

App::App(App&& other) noexcept = default;
App& App::operator=(App&& other) noexcept = default;

App::~App() {
    if (impl_ == nullptr) {
        return;
    }
    detail::destroy_all_windows(*impl_);
    impl_.reset();
}

std::expected<Window, WindowError> App::create_window(const WindowDesc& desc) {
    if (impl_ == nullptr) {
        return std::unexpected(WindowError::app_invalid);
    }
    if (desc.width <= 0 || desc.height <= 0) {
        return std::unexpected(WindowError::invalid_desc);
    }

    auto impl = std::make_unique<detail::WindowImpl>();

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!desc.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    // The requested size is the client area; grow the window bounds to match.
    // AdjustWindowRect is not DPI-aware (Phase 2: AdjustWindowRectExForDpi);
    // frame sizes may deviate on monitors scaled away from 100%.
    RECT bounds{0, 0, desc.width, desc.height};
    AdjustWindowRect(&bounds, style, FALSE);

    const std::wstring title = detail::utf8_to_utf16(desc.title);
    impl->hwnd = CreateWindowExW(0, detail::kWindowClassName, title.c_str(), style,
                                 CW_USEDEFAULT, CW_USEDEFAULT,
                                 bounds.right - bounds.left, bounds.bottom - bounds.top,
                                 nullptr, nullptr, impl_->instance, impl.get());
    if (impl->hwnd == nullptr) {
        return std::unexpected(WindowError::creation_failed);
    }

    impl_->windows.push_back(std::move(impl));
    return Window{impl_->windows.back().get()};
}

int App::run() {
    if (impl_ == nullptr) {
        return 0;
    }
    while (running()) {
        MSG message{};
        const int result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {  // WM_QUIT or error
            impl_->quit_requested = true;
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

void App::run_one_frame() {
    if (impl_ == nullptr || !running()) {
        return;
    }
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            impl_->quit_requested = true;
            return;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
        if (!running()) {
            return;  // the last window may have closed mid-drain
        }
    }
}

bool App::running() const {
    return impl_ != nullptr && !impl_->quit_requested && impl_->has_active_windows();
}

void App::request_redraw() {
    if (impl_ == nullptr) {
        return;
    }
    for (auto& window : impl_->windows) {
        if (window->hwnd != nullptr) {
            InvalidateRect(window->hwnd, nullptr, FALSE);
        }
    }
}

void App::quit() {
    if (impl_ == nullptr || impl_->quit_requested) {
        return;
    }
    impl_->quit_requested = true;
    PostQuitMessage(0);  // wake a blocking run()
}

}  // namespace continui
