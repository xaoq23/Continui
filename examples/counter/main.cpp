// Interactive Counter (Phase 0.6 vertical slice): user-owned business state,
// View = render(State), layout, Painter, mouse hit on a Button, a callback
// that mutates state and calls UI::invalidate(), and a re-evaluated next
// frame. Escape or the title bar close button exits.
#include <continui/backend_dawn.hpp>
#include <continui/continui.hpp>

#include <array>
#include <filesystem>
#include <print>
#include <string>

int main() {
    auto app = continui::App::create();
    if (!app) {
        std::println(stderr, "continui: platform init failed");
        return 1;
    }
    const continui::WindowDesc desc{.title = "Continui Counter",
                                    .width = 520,
                                    .height = 320};
    auto window = app->create_window(desc);
    if (!window) {
        std::println(stderr, "continui: window creation failed");
        return 1;
    }
    window->show();

    auto renderer = continui::dawn::Renderer::create(*app, *window);
    if (!renderer) {
        std::println(stderr, "continui: gpu init failed ({})",
                     static_cast<int>(renderer.error()));
        return 1;
    }

#ifdef _WIN32
    const std::filesystem::path font_path = "C:/Windows/Fonts/segoeui.ttf";
#else
    const std::filesystem::path font_path =
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
    continui::FontManager fonts;
    const continui::FontFace* face = nullptr;
    if (std::filesystem::exists(font_path)) {
        const auto loaded = fonts.load_file(font_path);
        if (loaded) {
            face = *loaded;
        }
    }
    // Chinese fallback so IME commits in the text input render as real glyphs.
    const continui::FontFace* cjk_face = nullptr;
#ifdef _WIN32
    const std::filesystem::path cjk_path = "C:/Windows/Fonts/msyh.ttc";
    if (std::filesystem::exists(cjk_path)) {
        const auto loaded = fonts.load_file(cjk_path);
        if (loaded) {
            cjk_face = *loaded;
        }
    }
#endif

    // 1) Business state is owned by the user, not by the framework.
    struct State {
        int count = 42;
        std::string text;
    } state;

    // 2) View is a pure projection of that state. The callbacks capture the
    // state by reference and call invalidate() explicitly (C3).
    // Declared before UI so button callbacks can invalidate it (MSVC).
    continui::UI* ui_ptr = nullptr;
    continui::UI ui([&](continui::FrameArena& arena,
                        const continui::LocalState& local) {
        continui::Style panel;
        panel.width = 460;
        panel.height = 290;
        panel.background = continui::Color::rgb(30, 33, 42);
        panel.padding = {24, 22, 24, 22};
        panel.gap = 14;
        panel.align_items = continui::Align::center;

        continui::Style number;
        number.font_size = 52;
        number.foreground = continui::Color::rgb(160, 225, 200);
        number.height = 70;

        continui::Style row;
        row.width = 412;
        row.height = 52;
        row.gap = 14;

        continui::Style button;
        button.width = 199;
        button.height = 48;
        button.background = continui::Color::rgb(70, 105, 230);
        button.foreground = continui::Color::rgb(255, 255, 255);
        button.radius = 8;
        button.font_size = 18;

        // Stable keys let LocalState report hover/pressed/focus across frames;
        // the user decides appearance, the framework only tracks state.
        const continui::Key minus_key{100};
        const continui::Key plus_key{101};
        continui::Style minus_style = button;
        if (local.hovered(minus_key) || local.pressed(minus_key)) {
            minus_style.background = continui::Color::rgb(95, 135, 255);
        }
        continui::Style plus_style = button;
        if (local.hovered(plus_key) || local.pressed(plus_key)) {
            plus_style.background = continui::Color::rgb(95, 135, 255);
        }
        const auto label = continui::Text(arena, std::to_string(state.count), number);
        const auto minus = continui::Button(arena, minus_key, "- Decrement", [&] {
            --state.count;
            ui_ptr->invalidate();
        }, minus_style);
        const auto plus = continui::Button(arena, plus_key, "+ Increment", [&] {
            ++state.count;
            ui_ptr->invalidate();
        }, plus_style);
        continui::Style input_style;
        input_style.width = 412;
        input_style.height = 40;
        input_style.background = continui::Color::rgb(38, 42, 54);
        input_style.foreground = continui::Color::rgb(230, 235, 245);
        input_style.padding = {12, 8, 12, 8};
        input_style.font_size = 18;
        input_style.radius = 6;
        const auto input = continui::Input(
            arena, continui::Key{102}, state.text,
            [&](std::string_view s) { state.text = std::string(s); }, input_style);
        const auto buttons = continui::Row(arena, {minus, plus}, row);
        return continui::Column(arena, {label, buttons, input}, panel);
    });
    ui_ptr = &ui;
    ui.set_font(face);
    if (cjk_face != nullptr) {
        const std::array<const continui::FontFace* const, 1> fallbacks{cjk_face};
        ui.set_fallback_fonts(fallbacks);
    }

    // 5-7) Events are pumped before paint; handle_event uses the previous
    // frame's layout, and a consumed click invalidates the view.
    window->set_event_handler([&app, &ui](const continui::Event& event) {
        if (event.type == continui::EventType::key_down
            && event.key == continui::KeyCode::escape) {
            app->quit();
        } else {
            ui.handle_event(event);
        }
    });

    renderer->set_paint_callback([&window, &ui](continui::Painter& painter) {
        const continui::Size client = window->client_size();
        painter.rect({0, 0, static_cast<float>(client.width),
                      static_cast<float>(client.height)},
                     {continui::Color::rgb(18, 20, 27)});
        ui.paint(painter, client);
        const continui::ImeContext ime = ui.ime_context();
        window->set_ime_context(ime.text, ime.caret, ime.text_bounds);
    });

    std::println("Counter is up. Click the buttons; Escape or the close button exits.");
    while (app->running()) {
        app->run_one_frame();
    }
    return 0;
}
