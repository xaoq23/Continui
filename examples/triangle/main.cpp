// Triangle (Phase 0.3 acceptance): window + Dawn renderer, embeddable loop
// style. The renderer registers a frame callback, so each run_one_frame()
// pumps events and then presents a frame.
#include <continui/backend_dawn.hpp>
#include <continui/continui.hpp>

#include <print>

int main() {
    auto app = continui::App::create();
    if (!app) {
        std::println(stderr, "continui: platform init failed");
        return 1;
    }

    const continui::WindowDesc desc{
        .title = "Continui Triangle",
        .width = 640,
        .height = 480,
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
    std::println("Triangle is up. Escape or the title bar close button exits.");

    // Embeddable loop: one frame per iteration. The frame callback inside
    // run_one_frame() draws and presents via Dawn.
    while (app->running()) {
        app->run_one_frame();
    }
    return 0;
}
