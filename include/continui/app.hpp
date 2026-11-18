// App: GUI runtime entry point (docs/architecture.md §4.7).
//
// Status: Phase 0.2. Owns the platform runtime and the windows it creates;
// does not own user business state. request_redraw forces a repaint of the
// current (background-only) content; full invalidation semantics arrive
// with the render system in Phase 0.3.
//
// Threading contract (architecture §9.1): all App and Window methods,
// including event delivery, are main-thread only. Not enforced in Phase 0.2.
#pragma once

#include <continui/window.hpp>

#include <expected>
#include <functional>
#include <memory>

namespace continui {

namespace detail {
struct AppImpl;
}

enum class AppError {
    platform_init_failed,
};

using FrameCallback = std::function<void()>;

class App {
public:
    // Creates the platform runtime. Per-monitor DPI awareness is set here
    // (best effort; per-monitor details are Phase 2).
    static std::expected<App, AppError> create();

    App(App&& other) noexcept;
    App& operator=(App&& other) noexcept;
    ~App();

    // Creates a hidden window; call Window::show() to display it. The App
    // owns the window; returned handles must not outlive the App.
    std::expected<Window, WindowError> create_window(const WindowDesc& desc);

    // Managed loop: pumps events until quit() is called or the last window
    // closes. Returns 0.
    int run();

    // Drives one non-blocking pump cycle (drains pending messages), then
    // invokes the frame callback once. Embed this in an external loop; keep
    // calling while running() is true.
    void run_one_frame();

    // Registers a callback invoked once per run_one_frame() call, after
    // events are pumped (one tick per frame). The GPU renderer registers
    // itself here. Phase 0.3 constraint: the managed run() loop does not
    // tick frame callbacks — GPU apps drive the loop via run_one_frame().
    void set_frame_callback(FrameCallback callback);

    // True while run()/run_one_frame() should keep going: at least one live
    // window and quit() not called. Main thread only.
    bool running() const;

    // Forces a repaint of every window's current content.
    void request_redraw();

    // Requests termination; takes effect at the end of the current pump.
    // Terminal for this App instance: a quit() app keeps reporting
    // running() == false even if new windows are created.
    //
    // Phase 0.2 constraint: PostQuitMessage is thread-queue-global. A quit()
    // that is never drained (no run()/run_one_frame() afterwards) leaves a
    // WM_QUIT in the thread queue, and the next App created on this thread
    // consumes it and exits immediately. Drain after quit() before creating
    // another App on the same thread.
    void quit();

private:
    App() = default;

    std::unique_ptr<detail::AppImpl> impl_;
};

}  // namespace continui
