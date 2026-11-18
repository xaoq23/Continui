// Text (Phase 0.5 acceptance): FreeType glyphs shaped by HarfBuzz, drawn
// through the batched painter. The scene shows sizes, normal/bold weights,
// fractional (subpixel) positions, and enough distinct strikes to grow the
// glyph atlas from 256px to 512px in the first frames. Resize keeps it
// meaningful. Escape or the title bar close button exits.
#include <continui/backend_dawn.hpp>
#include <continui/continui.hpp>

#include <filesystem>
#include <print>
#include <vector>

namespace {

struct Sample {
    continui::GlyphRun run;
    float x = 0;
    float y = 0;
    continui::Color color{};
};

}  // namespace

int main() {
    auto app = continui::App::create();
    if (!app) {
        std::println(stderr, "continui: platform init failed");
        return 1;
    }

    const continui::WindowDesc desc{
        .title = "Continui Text",
        .width = 1060,
        .height = 720,
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

    // System fonts keep the example self-contained (no assets to ship).
#ifdef _WIN32
    const std::filesystem::path regular = "C:/Windows/Fonts/segoeui.ttf";
    const std::filesystem::path bold = "C:/Windows/Fonts/segoeuib.ttf";
#else
    const std::filesystem::path regular =
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    const std::filesystem::path bold =
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
#endif
    if (!std::filesystem::exists(regular) || !std::filesystem::exists(bold)) {
        std::println(stderr, "continui: system fonts not found; text example needs them");
        return 1;
    }
    continui::FontManager fonts;
    const auto normal = fonts.load_file(regular);
    const auto heavy = fonts.load_file(bold);
    if (!normal || !heavy) {
        std::println(stderr, "continui: font loading failed");
        return 1;
    }

    // Shaping happens once; every frame only re-resolves cached glyph slots.
    using continui::Color;
    std::vector<Sample> samples;
    const auto add = [&samples](const continui::FontFace& face, std::string_view text,
                                float size, float x, float y, Color color) {
        samples.push_back({face.shape(text, size), x, y, color});
    };
    add(**normal, "Continui text at 12px, normal weight: 0123456789", 12, 40, 40,
        Color::rgb(235, 235, 238));
    add(**normal, "Continui text at 16px, normal weight: ABC abc", 16, 40, 70,
        Color::rgb(235, 235, 238));
    add(**heavy, "Continui text at 20px, BOLD weight: ABC abc", 20, 40, 105,
        Color::rgb(255, 210, 120));
    add(**normal, "Kerning pairs: AV To Wa LT - latin single run", 24, 40, 145,
        Color::rgb(140, 220, 255));
    add(**heavy, "Glyph atlas growth", 32, 40, 195, Color::rgb(180, 235, 170));
    add(**normal, "48px", 48, 40, 245, Color::rgb(255, 150, 170));

    // Quarter-pixel positions + a wide range of sizes force all four
    // subpixel buckets and overflow the initial 256px atlas.
    for (int size = 13; size <= 27; size += 2) {
        for (int bucket = 0; bucket < 4; ++bucket) {
            add(**normal, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
                static_cast<float>(size), 40.0f + static_cast<float>(bucket) * 0.25f,
                310.0f + static_cast<float>(size - 13) * 16.0f,
                Color::rgb(190, 200, 215));
        }
    }

    renderer->set_paint_callback([&window, &samples](continui::Painter& painter) {
        const continui::Size client = window->client_size();
        painter.rect({0, 0, static_cast<float>(client.width),
                      static_cast<float>(client.height)},
                     {Color::rgb(24, 26, 33)});
        for (const Sample& sample : samples) {
            painter.text(sample.run, {sample.x, sample.y}, {sample.color});
        }
    });

    std::println("Text is up. Escape or the title bar close button exits.");
    while (app->running()) {
        app->run_one_frame();
    }
    return 0;
}
