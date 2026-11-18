// Event: platform input and window events (docs/architecture.md §8.2).
//
// Status: Phase 0.2 draft surface: key, mouse, resize, close. Text input,
// composition, focus, and scroll arrive in later phases; the flat field set
// is a draft — a proper sum type is planned before the API freezes.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace continui {

enum class EventType : std::uint8_t {
    none = 0,
    window_close_requested,
    window_resized,
    key_down,
    key_up,
    mouse_move,
    mouse_button_down,
    mouse_button_up,
    text_input,           // committed characters (WM_CHAR / TSF commit)
    composition_start,    // IME composition begins
    composition_update,   // IME preview string changed (not a key event)
    composition_end,      // IME composition committed/cancelled
};

// Draft keyboard codes: platform values (VK_*) are translated before they
// reach the public API. The set grows when text input and widgets need it.
// Letters and digits are contiguous so ranges can be mapped arithmetically.
enum class KeyCode : std::uint32_t {
    unknown = 0,
    backspace, tab, enter, escape, space, del,
    left, right, up, down,
    left_shift, right_shift, left_control, right_control, left_alt, right_alt,
    a, b, c, d, e, f, g, h, i, j, k, l, m,
    n, o, p, q, r, s, t, u, v, w, x, y, z,
    digit_0, digit_1, digit_2, digit_3, digit_4,
    digit_5, digit_6, digit_7, digit_8, digit_9,
};

enum class Modifiers : unsigned {
    none = 0,
    shift = 1,
    control = 2,
    alt = 4,
};

constexpr Modifiers operator|(Modifiers lhs, Modifiers rhs) noexcept {
    return static_cast<Modifiers>(static_cast<unsigned>(lhs) | static_cast<unsigned>(rhs));
}

constexpr Modifiers operator&(Modifiers lhs, Modifiers rhs) noexcept {
    return static_cast<Modifiers>(static_cast<unsigned>(lhs) & static_cast<unsigned>(rhs));
}

enum class MouseButton : std::uint8_t {
    none = 0,
    left, right, middle, x1, x2,
};

struct Point {
    int x = 0;
    int y = 0;
};

struct Size {
    int width = 0;
    int height = 0;
};

constexpr bool operator==(const Point& lhs, const Point& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

constexpr bool operator==(const Size& lhs, const Size& rhs) noexcept {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

// One platform event. Fields that do not apply to a type keep their default
// values (draft; a sum type is planned before the API freezes).
struct Event {
    EventType type{};
    Modifiers modifiers{};
    KeyCode key{};          // key_down / key_up
    MouseButton button{};   // mouse_button_down / mouse_button_up
    Point position{};       // mouse events: client-area pixels
    Size size{};            // window_resized: new client size
    std::string text;       // text_input / composition_*: UTF-8 payload
};

using EventHandler = std::function<void(const Event&)>;

}  // namespace continui
