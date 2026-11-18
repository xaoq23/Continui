// Platform (Win32) tests: App and Window lifecycle plus event delivery.
//
// Events are injected with SendMessage so assertions are deterministic.
// shift/ctrl modifiers depend on the real thread key state, which injected
// messages do not update; the alt path is tested via the lParam context
// bit. Runtime checks are NDEBUG-independent (see tests/core/key_test.cpp).
#include <continui/continui.hpp>

#include <windows.h>

#include <cstdio>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

const char* event_type_name(continui::EventType type) {
    using ET = continui::EventType;
    switch (type) {
        case ET::none: return "none";
        case ET::window_close_requested: return "window_close_requested";
        case ET::window_resized: return "window_resized";
        case ET::key_down: return "key_down";
        case ET::key_up: return "key_up";
        case ET::mouse_move: return "mouse_move";
        case ET::mouse_button_down: return "mouse_button_down";
        case ET::mouse_button_up: return "mouse_button_up";
    }
    return "unknown";
}

int check_lifecycle_and_events() {
    using namespace continui;

    auto app = App::create();
    check(app.has_value(), "App::create succeeds");

    const WindowDesc desc{.title = "continui-platform-test",
                          .width = 320,
                          .height = 240,
                          .resizable = true};
    auto window = app->create_window(desc);
    check(window.has_value(), "create_window succeeds");
    if (!window) {
        return failures;
    }
    check(window->valid(), "window handle valid after creation");
    check(!window->visible(), "window starts hidden");
    check(window->client_size() == Size{320, 240},
          "client size matches the requested size");

    std::vector<Event> events;
    window->set_event_handler([&events](const Event& event) { events.push_back(event); });

    window->show();
    check(window->visible(), "window visible after show()");

    for (int frame = 0; frame < 5; ++frame) {
        app->run_one_frame();  // must not block
    }
    check(app->running(), "app keeps running while a window is open");

    events.clear();  // drop incidental size/paint events from creation and show

    // Deterministic event injection: find the window by its unique title.
    const HWND hwnd = FindWindowW(nullptr, L"continui-platform-test");
    check(hwnd != nullptr, "window found by title");
    if (hwnd == nullptr) {
        return failures;
    }

    SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0x00000001);
    SendMessageW(hwnd, WM_KEYUP, VK_ESCAPE, 0xC0000001);
    SendMessageW(hwnd, WM_SYSKEYDOWN, VK_MENU, 0x21000001);  // context bit -> alt
    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(10, 20));
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 20));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(10, 20));
    SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(640, 480));

    check(events.size() == 7, "all injected messages became events");
    if (events.size() < 7) {
        for (const auto& event : events) {
            std::printf("      got: %s\n", event_type_name(event.type));
        }
        return failures;
    }

    check(events[0].type == EventType::key_down && events[0].key == KeyCode::escape,
          "key_down escape");
    check(events[1].type == EventType::key_up && events[1].key == KeyCode::escape,
          "key_up escape");
    check(events[2].type == EventType::key_down && events[2].key == KeyCode::left_alt
              && (events[2].modifiers & Modifiers::alt) != Modifiers::none,
          "alt syskeydown carries the alt modifier");
    check(events[3].type == EventType::mouse_move && events[3].position == Point{10, 20},
          "mouse_move position");
    check(events[4].type == EventType::mouse_button_down
              && events[4].button == MouseButton::left,
          "left button down");
    check(events[5].type == EventType::mouse_button_up
              && events[5].button == MouseButton::left,
          "left button up");
    check(events[6].type == EventType::window_resized && events[6].size == Size{640, 480},
          "resize carries the new client size");

    SendMessageW(hwnd, WM_CLOSE, 0, 0);
    check(events.size() == 8, "close requested delivered");
    check(events.back().type == EventType::window_close_requested,
          "close event delivered before destruction");
    check(!app->running(), "app stops running after the last window closes");
    check(!window->visible(), "closed window reports not visible");

    // A closed window's handle stays safe: queries return empty values.
    check(window->client_size() == Size{0, 0}, "closed window reports empty client size");

    return failures;
}

int check_quit() {
    using namespace continui;

    auto app = App::create();
    check(app.has_value(), "second App::create succeeds");
    check(!app->running(), "app without windows is not running");

    auto window = app->create_window(WindowDesc{.title = "continui-quit-test"});
    check(window.has_value(), "quit-test window created");
    check(app->running(), "app running with a window");

    app->quit();
    check(!app->running(), "quit() stops the app");

    app->run_one_frame();  // returns immediately, no crash

    // Invalid descriptor is rejected.
    auto bad = app->create_window(WindowDesc{.title = "bad", .width = 0});
    check(!bad.has_value(), "non-positive width rejected");

    return failures;
}

int check_invalid_app() {
    using namespace continui;

    auto app = App::create();
    check(app.has_value(), "third App::create succeeds");
    auto moved_from = std::move(app);  // moves the App inside the expected
    check(moved_from.has_value(), "moved-into expected keeps value");
    // The moved-from expected still holds its App object; its impl is null.
    check(!app->running(), "moved-from app reports not running");

    return failures;
}

}  // namespace

int main() {
    check_lifecycle_and_events();
    check_quit();
    check_invalid_app();

    if (failures == 0) {
        std::printf("platform_window_test: all checks passed\n");
        return 0;
    }
    std::printf("platform_window_test: %d check(s) failed\n", failures);
    return 1;
}
