// Interactive Counter (Phase 0.6 vertical slice): user-owned business state,
// View = render(State), layout, Painter, mouse hit on a Button, a callback
// that mutates state and calls UI::invalidate(), and a re-evaluated next
// frame. Escape or the title bar close button exits.
#include <continui/backend_dawn.hpp>
#include <continui/continui.hpp>

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

    // 1) Business state is owned by the user, not by the framework.
    struct State {
        int count = 42;
    } state;

    // 2) View is a pure projection of that state. The callbacks capture the
    // state by reference and call invalidate() explicitly (C3).
    // Declared before UI so button callbacks can invalidate it (MSVC).
    continui::UI* ui_ptr = nullptr;
    continui::UI ui([&](continui::FrameArena& arena) {
        continui::Style panel;
        panel.width = 460;
        panel.height = 250;
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

        const auto label = continui::Text(arena, std::to_string(state.count), number);
        const auto minus = continui::Button(arena, "- Decrement", [&] {
            --state.count;
            ui_ptr->invalidate();
        }, button);
        const auto plus = continui::Button(arena, "+ Increment", [&] {
            ++state.count;
            ui_ptr->invalidate();
        }, button);
        const auto buttons = continui::Row(arena, {minus, plus}, row);
        return continui::Column(arena, {label, buttons}, panel);
    });
    ui_ptr = &ui;
    ui.set_font(face);

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
    });

    std::println("Counter is up. Click the buttons; Escape or the close button exits.");
    while (app->running()) {
        app->run_one_frame();
    }
    return 0;
}
