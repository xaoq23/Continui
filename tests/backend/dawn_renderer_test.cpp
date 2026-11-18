// Dawn backend test: GPU bring-up on a real window, presented frames,
// texture upload with rejected inputs, resize, clean teardown, and GDI pixel
// readback assertions (solid fill, two scissored batches, glyph pixels) that
// catch silent drawing regressions programmatically.
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
#include <filesystem>
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

            // Phase 0.5: shape a few latin runs on a real font (skip the
            // text path on machines without a system font). The many sizes
            // below overflow the 256px atlas, exercising texture replacement
            // and full re-upload on growth through the GPU path.
            FontManager fonts;
            std::vector<GlyphRun> runs;
#ifdef _WIN32
            const std::filesystem::path font_path = "C:/Windows/Fonts/segoeui.ttf";
#else
            const std::filesystem::path font_path =
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
            const FontFace* face = nullptr;
            if (std::filesystem::exists(font_path)) {
                const auto loaded = fonts.load_file(font_path);
                if (loaded) {
                    face = *loaded;
                    runs.push_back(face->shape("Hello, glyph atlas!", 18.0f));
                    for (int size = 12; size <= 24; ++size) {
                        runs.push_back(face->shape("ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                                                   static_cast<float>(size)));
                    }
                }
            }

            // Painted frames: shapes, a translucent overlap, the texture,
            // text runs, and a clip switch — the batched pipeline must
            // swallow all of it. Programmatic error check: any uncaptured
            // GPU error (validation, shader, atlas upload) fails.
            renderer->set_paint_callback([&window, texture, face, &runs](Painter& painter) {
                const Size client = window->client_size();
                painter.rect({0, 0, static_cast<float>(client.width),
                              static_cast<float>(client.height)},
                             {Color::rgb(24, 26, 33)});
                painter.rect({10, 10, 120, 80}, {Color::rgb(230, 90, 70)});
                painter.rounded_rect({150, 10, 120, 80}, Radius::top(24),
                                     {Color::rgba(60, 90, 230, 160)});
                painter.image(texture, {10, 100, 80, 40});

                if (face != nullptr) {
                    painter.text(runs[0], {20, 150}, {Color::rgb(245, 245, 245)});
                    for (std::size_t i = 1; i < runs.size(); ++i) {
                        painter.text(runs[i],
                                     {20.0f + static_cast<float>(i % 4) * 0.25f,
                                      static_cast<float>(60 + (i - 1) * 12)},
                                     {Color::rgb(210, 220, 240)});
                    }
                }

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

struct Pixel {
    int r = 0;
    int g = 0;
    int b = 0;
};

// One PrintWindow(PW_RENDERFULLCONTENT) copies the presented swapchain into a
// memory DC; GetDC(hwnd)/GetPixel only sees the DWM backdrop and is not usable
// here. Keep the snapshot alive while probing several client-area points.
class WindowSnapshot {
public:
    explicit WindowSnapshot(HWND hwnd) {
        GetWindowRect(hwnd, &window_rect_);
        POINT origin{0, 0};
        ClientToScreen(hwnd, &origin);
        offset_x_ = origin.x - window_rect_.left;
        offset_y_ = origin.y - window_rect_.top;
        screen_ = GetDC(nullptr);
        mem_ = CreateCompatibleDC(screen_);
        bitmap_ = CreateCompatibleBitmap(screen_, width(), height());
        old_ = SelectObject(mem_, bitmap_);
        PrintWindow(hwnd, mem_, PW_RENDERFULLCONTENT);
    }

    ~WindowSnapshot() {
        SelectObject(mem_, old_);
        DeleteObject(bitmap_);
        DeleteDC(mem_);
        ReleaseDC(nullptr, screen_);
    }

    WindowSnapshot(const WindowSnapshot&) = delete;
    WindowSnapshot& operator=(const WindowSnapshot&) = delete;

    [[nodiscard]] Pixel client(int x, int y) const noexcept {
        const COLORREF c = GetPixel(mem_, offset_x_ + x, offset_y_ + y);
        return Pixel{GetRValue(c), GetGValue(c), GetBValue(c)};
    }

private:
    [[nodiscard]] int width() const noexcept { return window_rect_.right - window_rect_.left; }
    [[nodiscard]] int height() const noexcept { return window_rect_.bottom - window_rect_.top; }

    RECT window_rect_{};
    int offset_x_ = 0;
    int offset_y_ = 0;
    HDC screen_ = nullptr;
    HDC mem_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_ = nullptr;
};

// DWM compositing can lag Present() by one or two frames, so a snapshot may
// still hold the previous scene. Pump an identical frame and retry, bounded.
template <typename Predicate>
bool readback_until(continui::App& app, HWND hwnd, Predicate predicate) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        app.run_one_frame();
        const WindowSnapshot frame{hwnd};
        if (predicate(frame)) {
            return true;
        }
    }
    return false;
}

// Programmatic pixel verification of the GPU path. Targets the two silent
// failures seen earlier: 0.4 (baseVertex shifted all but the first batch)
// and 0.5 (normalized UV used as a quad size -> invisible text).
int check_pixel_readback() {
    using namespace continui;
    auto app = App::create();
    check(app.has_value(), "readback: App::create succeeds");
    if (!app) {
        return failures;
    }
    const WindowDesc desc{.title = "continui-pixel-readback",
                          .width = 320,
                          .height = 240};
    auto window = app->create_window(desc);
    check(window.has_value(), "readback: create_window succeeds");
    if (!window) {
        return failures;
    }
    window->show();
    const HWND hwnd = FindWindowW(nullptr, L"continui-pixel-readback");
    check(hwnd != nullptr, "readback: window found by title");

    {
        auto renderer = dawn::Renderer::create(*app, *window);
        check(renderer.has_value(), "readback: Dawn renderer created");
        if (!renderer || hwnd == nullptr) {
            // Do not quit() here either: see the lifetime note at the end of
            // this function. Let destructors tear the window down naturally.
            return failures;
        }

        // (1) Solid full-window fill: the presented surface holds the tint.
        renderer->set_paint_callback([&window](Painter& painter) {
            const Size c = window->client_size();
            painter.rect({0, 0, static_cast<float>(c.width), static_cast<float>(c.height)},
                         {Color::rgb(255, 0, 0)});
        });
        check(readback_until(*app, hwnd, [](const WindowSnapshot& frame) {
                  const Pixel p = frame.client(160, 120);
                  return p.r > 240 && p.g < 16 && p.b < 16;
              }),
              "readback: solid red fill reaches the presented surface");

        // (2) Two scissor-separated batches. A wrong baseVertex loses the
        // second batch, leaving the right half at the clear color.
        renderer->set_paint_callback([&window](Painter& painter) {
            const Size c = window->client_size();
            const float half = static_cast<float>(c.width) * 0.5f;
            const float full_w = static_cast<float>(c.width);
            const float full_h = static_cast<float>(c.height);
            painter.clip({0, 0, half, full_h});
            painter.rect({0, 0, full_w, full_h}, {Color::rgb(255, 0, 0)});
            painter.clip({half, 0, half, full_h});
            painter.rect({0, 0, full_w, full_h}, {Color::rgb(0, 0, 255)});
            painter.clip({0, 0, full_w, full_h});
        });
        check(readback_until(*app, hwnd, [](const WindowSnapshot& frame) {
                  const Pixel left = frame.client(80, 120);
                  const Pixel right = frame.client(240, 120);
                  return left.r > 240 && left.b < 16
                         && right.b > 240 && right.r < 16;
              }),
              "readback: both scissored batches (red left, blue right) are visible");

        // (3) Text: bright glyph pixels must appear. A zero-sized text quad
        // leaves only the dark background, which is exactly the 0.5 failure.
        FontManager fonts;
        const std::filesystem::path font_path = "C:/Windows/Fonts/segoeui.ttf";
        const FontFace* face = nullptr;
        if (std::filesystem::exists(font_path)) {
            const auto loaded = fonts.load_file(font_path);
            if (loaded) {
                face = *loaded;
            }
        }
        if (face != nullptr) {
            const GlyphRun run = face->shape("HELLO", 48.0f);
            renderer->set_paint_callback([&window, &run](Painter& painter) {
                const Size c = window->client_size();
                painter.rect({0, 0, static_cast<float>(c.width),
                              static_cast<float>(c.height)},
                             {Color::rgb(24, 26, 33)});
                painter.text(run, {20, 100}, {Color::rgb(255, 255, 255)});
            });
            check(readback_until(*app, hwnd, [](const WindowSnapshot& frame) {
                      int bright = 0;
                      for (int y = 70; y < 140; ++y) {
                          for (int x = 20; x < 300; ++x) {
                              const Pixel p = frame.client(x, y);
                              if (p.r > 200 && p.g > 200 && p.b > 200) {
                                  ++bright;
                              }
                          }
                      }
                      return bright > 50;
                  }),
                  "readback: white glyph pixels are present");
        } else {
            std::printf("readback: skipped text probe (no system font)\n");
        }
        check(!renderer->had_uncaptured_errors(),
              "readback: pixel probes produce no uncaptured GPU errors");
    }

    // Do NOT call quit(): quit() posts a thread-global WM_QUIT that later
    // App::run_one_frame() calls would consume as their own quit (and the
    // posting App's own quit_requested short-circuits any drain). Let the
    // App/Window destructors tear the window down naturally instead.
    return failures;
}

}  // namespace

int main() {
    check_pixel_readback();
    check_renderer_lifecycle();
    check_invalid_window();

    if (failures == 0) {
        std::printf("backend_dawn_renderer_test: all checks passed\n");
        return 0;
    }
    std::printf("backend_dawn_renderer_test: %d check(s) failed\n", failures);
    return 1;
}
