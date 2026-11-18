// Hello Continui.
//
// Phase 0.2 example: a real Win32 window with an event handler, driven by
// the managed run loop. Escape or the title bar close button exits.
#include <continui/continui.hpp>

#include <print>

namespace {

const char* event_type_name(continui::EventType type) {
    using ET = continui::EventType;
    switch (type) {
        case ET::none: return "none";
        case ET::window_close_requested: return "close";
        case ET::window_resized: return "resized";
        case ET::key_down: return "key_down";
        case ET::key_up: return "key_up";
        case ET::mouse_button_down: return "button_down";
        case ET::mouse_button_up: return "button_up";
        case ET::mouse_move: return "mouse_move";
    }
    return "unknown";
}

}  // namespace

int main() {
    auto app = continui::App::create();
    if (!app) {
        std::println(stderr, "continui: platform init failed");
        return 1;
    }

    const continui::WindowDesc desc{
        .title = "Continui Hello",
        .width = 480,
        .height = 320,
    };
    auto window = app->create_window(desc);
    if (!window) {
        std::println(stderr, "continui: window creation failed");
        return 1;
    }

    window->set_event_handler([&app](const continui::Event& event) {
        if (event.type == continui::EventType::mouse_move) {
            return;  // too chatty for a console demo
        }
        if (event.type == continui::EventType::key_down
            && event.key == continui::KeyCode::escape) {
            app->quit();
        }
        std::println("{}: key={} button={} pos=({},{}) size={}x{}",
                     event_type_name(event.type),
                     static_cast<unsigned>(event.key),
                     static_cast<unsigned>(event.button),
                     event.position.x, event.position.y,
                     event.size.width, event.size.height);
    });

    window->show();
    std::println("Window is up. Escape or the title bar close button exits.");

    return app->run();
}
