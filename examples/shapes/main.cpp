// Shapes (Phase 0.4 acceptance): basic drawing — solid rects, rounded
// rects with per-corner radii, translucent alpha blending, a programmatic
// texture through painter.image(), and scissor clipping. The scene lays out
// relative to the client size, so resize keeps it meaningful. Escape or the
// title bar close button exits.
#include <continui/backend_dawn.hpp>
#include <continui/continui.hpp>

#include <cstdint>
#include <print>
#include <vector>

namespace {

// 8x8 checkerboard (16px cells): light cells are semi-transparent, dark
// cells opaque — one texture exercises both tint-free sampling and
// straight-alpha blending encoded in the data itself.
std::vector<std::uint8_t> make_checkerboard(const continui::Size size) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size.width)
                                     * static_cast<std::size_t>(size.height) * 4);
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            const bool light = ((x / 16) + (y / 16)) % 2 == 0;
            const auto shade = static_cast<std::uint8_t>(light ? 225 : 60);
            const auto alpha = static_cast<std::uint8_t>(light ? 190 : 255);
            const std::size_t index =
                (static_cast<std::size_t>(y) * size.width + x) * 4;
            pixels[index] = shade;
            pixels[index + 1] = shade;
            pixels[index + 2] = shade;
            pixels[index + 3] = alpha;
        }
    }
    return pixels;
}

}  // namespace

int main() {
    auto app = continui::App::create();
    if (!app) {
        std::println(stderr, "continui: platform init failed");
        return 1;
    }

    const continui::WindowDesc desc{
        .title = "Continui Shapes",
        .width = 960,
        .height = 640,
    };
    auto window = app->create_window(desc);
    if (!window) {
        std::println(stderr, "continui: window creation failed");
        return 1;
    }

    window->set_event_handler([&app](const continui::Event& event) {
        if (event.type == continui::EventType::key_down
            && event.key == continui::KeyCode::escape) {
            app->quit();
        }
    });

    window->show();

    auto renderer = continui::dawn::Renderer::create(*app, *window);
    if (!renderer) {
        std::println(stderr, "continui: gpu init failed ({})",
                     static_cast<int>(renderer.error()));
        return 1;
    }

    constexpr continui::Size kTextureSize{128, 128};
    const std::vector<std::uint8_t> checker = make_checkerboard(kTextureSize);
    const continui::Texture texture =
        renderer->create_texture(kTextureSize, checker.data(), checker.size());
    if (!texture.valid()) {
        std::println(stderr, "continui: texture upload failed");
        return 1;
    }

    renderer->set_paint_callback([&window, texture](continui::Painter& painter) {
        const continui::Size client = window->client_size();
        const float w = static_cast<float>(client.width);
        const float h = static_cast<float>(client.height);

        // Background fills the whole client area.
        painter.rect({0, 0, w, h}, {continui::Color::rgb(24, 26, 33)});

        // Row 1: solid rects.
        painter.rect({24, 24, 90, 60}, {continui::Color::rgb(230, 90, 70)});
        painter.rect({130, 24, 90, 60}, {continui::Color::rgb(70, 180, 120)});
        painter.rect({236, 24, 90, 60}, {continui::Color::rgb(80, 120, 230)});

        // Row 2: rounding combinations.
        painter.rounded_rect({24, 110, 90, 60}, continui::Radius::all(28),
                             {continui::Color::rgb(230, 170, 60)});
        painter.rounded_rect({130, 110, 90, 60}, continui::Radius::top(28),
                             {continui::Color::rgb(160, 90, 220)});
        painter.rounded_rect({236, 110, 90, 60}, continui::Radius::left(28),
                             {continui::Color::rgb(60, 190, 200)});
        painter.rounded_rect({342, 110, 90, 60}, continui::Radius{40, 10, 40, 10},
                             {continui::Color::rgb(220, 220, 120)});

        // Two translucent rects overlapping — the overlap darkens through
        // premultiplied alpha blending.
        painter.rect({24, 200, 140, 90}, {continui::Color::rgba(230, 60, 60, 140)});
        painter.rect({90, 230, 140, 90}, {continui::Color::rgba(60, 90, 230, 140)});

        // The checkerboard texture, straight-alpha in the data.
        painter.image(texture, {260, 200, 128, 128});

        // Clipped stripes: the striped band extends past its clip window on
        // every side, so the scissor does the visible cutting. The backdrop
        // is drawn before the clip so it is not striped.
        const continui::Rect clip_window{440, 200, 260, 180};
        painter.rounded_rect(clip_window, continui::Radius::all(12),
                             {continui::Color::rgb(40, 44, 54)});
        painter.clip(clip_window);
        for (int i = 0; i < 8; ++i) {
            painter.rect({clip_window.x + i * 36.0f - 10.0f, clip_window.y - 20.0f,
                          18.0f, clip_window.height + 40.0f},
                         {continui::Color::rgb(120, 200, 255)});
        }
        // Restore the full-viewport clip for anything that might come later.
        painter.clip({0, 0, w, h});
    });

    std::println("Shapes is up. Escape or the title bar close button exits.");

    // Embeddable loop: one frame per iteration; the paint callback runs
    // inside run_one_frame().
    while (app->running()) {
        app->run_one_frame();
    }
    return 0;
}
