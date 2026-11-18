// Dawn backend test: GPU bring-up on a real window, a few presented frames
// (empty and painted), texture upload with rejected inputs, resize without
// crashing, and clean teardown. Visual correctness of the drawn scene is
// verified by the interactive example, not here.
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

            // Phase 0.4: texture upload, including synchronously rejected
            // inputs. Byte count must equal width*height*4 exactly.
            constexpr Size kTextureSize{16, 8};
            std::vector<std::uint8_t> texels(kTextureSize.width * kTextureSize.height * 4);
            for (std::size_t i = 0; i < texels.size(); i += 4) {
                texels[i] = 80;
                texels[i + 1] = 160;
                texels[i + 2] = 230;
                texels[i + 3] = 128;  // semi-transparent on purpose
            }
            const Texture texture = renderer->create_texture(
                kTextureSize, texels.data(), texels.size());
            check(texture.valid(), "texture created from RGBA8 data");
            check(texture.size() == kTextureSize, "texture size round-trips");
            check(!renderer->create_texture({0, 8}, texels.data(), texels.size()).valid(),
                  "zero-width texture rejected");
            check(!renderer->create_texture(kTextureSize, nullptr, 0).valid(),
                  "null texture data rejected");
            check(!renderer
                       ->create_texture(kTextureSize, texels.data(), texels.size() - 1)
                       .valid(),
                  "wrong byte count rejected");

            // Painted frames: shapes, a translucent overlap, the texture,
            // and a clip switch — the batched pipeline must swallow all of
            // it. Programmatic error check: any uncaptured GPU error
            // (validation, shader) since create() fails these assertions.
            renderer->set_paint_callback([&window, texture](Painter& painter) {
                const Size client = window->client_size();
                painter.rect({0, 0, static_cast<float>(client.width),
                              static_cast<float>(client.height)},
                             {Color::rgb(24, 26, 33)});
                painter.rect({10, 10, 120, 80}, {Color::rgb(230, 90, 70)});
                painter.rounded_rect({150, 10, 120, 80}, Radius::top(24),
                                     {Color::rgba(60, 90, 230, 160)});
                painter.image(texture, {10, 100, 80, 40});

                // Clipped stripes must not crash or leak outside the clip.
                painter.clip({300, 10, 100, 100});
                painter.rect({250, 30, 200, 14}, {Color::rgb(120, 200, 255)});
                painter.rect({250, 60, 200, 14}, {Color::rgb(120, 200, 255)});
                painter.clip({0, 0, static_cast<float>(client.width),
                              static_cast<float>(client.height)});
            });
            for (int frame = 0; frame < 3; ++frame) {
                app->run_one_frame();
            }
            check(!renderer->had_uncaptured_errors(),
                  "painted frames produce no uncaptured GPU errors");

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
