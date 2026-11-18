// Dawn backend test: GPU bring-up on a real window, a few presented frames,
// resize without crashing, and clean teardown. Visual correctness of the
// triangle is verified by the interactive example, not here.
// Runtime checks are NDEBUG-independent (see tests/core/key_test.cpp).
#include <continui/backend_dawn.hpp>
#include <continui/continui.hpp>

// Direct Win32 use is this test's explicit choice (drive a resize the way a
// user's platform code would); umbrella headers never expose HWND.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

int check_renderer_lifecycle() {
    using namespace continui;

    auto app = App::create();
    check(app.has_value(), "App::create succeeds");

    const WindowDesc desc{.title = "continui-dawn-test",
                          .width = 320,
                          .height = 240,
                          .resizable = true};
    auto window = app->create_window(desc);
    check(window.has_value(), "create_window succeeds");
    if (!window) {
        return failures;
    }
    window->show();

    // Renderer lifetime is scoped so the teardown order below is real:
    // renderer destroyed first, then the app. Resources must release
    // cleanly (crash here means lifetime handling is wrong).
    {
        auto renderer = dawn::Renderer::create(*app, *window);
        check(renderer.has_value(), "Dawn renderer created");
        if (renderer) {
            for (int frame = 0; frame < 3; ++frame) {
                app->run_one_frame();  // must not block; each call presents a frame
            }
            check(app->running(), "app keeps running while a window is open");

            // Real resize: SetWindowPos changes the actual client area, so
            // the renderer's per-frame GetClientRect poll sees a new size
            // and reconfigures the surface. A synthetic WM_SIZE would not
            // do that.
            const HWND hwnd = FindWindowW(nullptr, L"continui-dawn-test");
            check(hwnd != nullptr, "window found by title");
            if (hwnd != nullptr) {
                if (!SetWindowPos(hwnd, nullptr, 0, 0, 540, 420,
                                  SWP_NOMOVE | SWP_NOZORDER)) {
                    check(false, "SetWindowPos resize accepted");
                }
                // The resize must actually reach the window: the renderer
                // polls this value per frame to reconfigure the surface.
                const Size client = window->client_size();
                check(client.width != 320 || client.height != 240,
                      "resize reached the window client area");
                for (int frame = 0; frame < 3; ++frame) {
                    app->run_one_frame();
                }
            }
        }
    }  // renderer destroyed here, before the quit() checks

    check(!app->running() == false, "app still running before quit");

    app->quit();
    check(!app->running(), "quit() stops the app");
    for (int frame = 0; frame < 2; ++frame) {
        app->run_one_frame();  // safe after quit
    }

    return failures;
}

int check_invalid_window() {
    using namespace continui;

    auto app = App::create();
    check(app.has_value(), "second App::create succeeds");

    Window no_window;
    auto renderer = dawn::Renderer::create(*app, no_window);
    check(!renderer.has_value(), "renderer rejects a default-constructed window");
    check(renderer.error() == GpuError::window_invalid,
          "renderer reports window_invalid");

    return failures;
}

}  // namespace

int main() {
    check_renderer_lifecycle();
    check_invalid_window();

    if (failures == 0) {
        std::printf("backend_dawn_renderer_test: all checks passed\n");
        return 0;
    }
    std::printf("backend_dawn_renderer_test: %d check(s) failed\n", failures);
    return 1;
}
