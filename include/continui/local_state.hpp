// LocalState: framework-owned, per-widget transient state (architecture §4.5).
//
// Values are keyed by the stable Key and survive across frames while the
// widget exists. Business state must NOT be stored here — ViewFn reads this
// (read-only) to decide appearance, and events update it explicitly.
#pragma once

#include <continui/key.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace continui {

// Control category a LocalState entry belongs to. A Key must not be reused
// across categories (architecture §4.5 requires type-consistent access).
enum class ControlKind : std::uint8_t {
    none = 0,
    button,
    input,
};

// Framework-owned widget state. Exposed read-only through LocalState.
struct WidgetState {
    ControlKind kind = ControlKind::none;
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    // Single-line text input editing state. `edited_text` is the authoritative
    // buffer between events (user State is only refreshed on the next frame's
    // evaluate), so a burst of input events cannot lose characters (0.7 audit
    // F2). It is NOT business state — it mirrors the user's input value.
    std::string edited_text;
    std::size_t caret = 0;
    std::size_t selection_start = 0;
    std::size_t selection_end = 0;
    // IME composition (TSF, architecture §8.3). Not a plain key event.
    bool composing = false;
    std::string composition;
};

// Read-only view over widget-local state, passed to ViewFn. Every lookup is
// keyed; typed lookups return nullptr on a miss or a category mismatch.
class LocalState {
public:
    LocalState() noexcept = default;
    explicit LocalState(const std::unordered_map<Key, WidgetState>* states) noexcept
        : states_(states) {}

    [[nodiscard]] const WidgetState* button(Key key) const noexcept {
        return typed(key, ControlKind::button);
    }
    [[nodiscard]] const WidgetState* input(Key key) const noexcept {
        return typed(key, ControlKind::input);
    }
    [[nodiscard]] const WidgetState* find(Key key) const noexcept {
        if (states_ == nullptr) {
            return nullptr;
        }
        const auto it = states_->find(key);
        return it == states_->end() ? nullptr : &it->second;
    }

    [[nodiscard]] bool hovered(Key key) const noexcept {
        const WidgetState* state = find(key);
        return state != nullptr && state->hovered;
    }
    [[nodiscard]] bool pressed(Key key) const noexcept {
        const WidgetState* state = find(key);
        return state != nullptr && state->pressed;
    }
    [[nodiscard]] bool focused(Key key) const noexcept {
        const WidgetState* state = find(key);
        return state != nullptr && state->focused;
    }

private:
    [[nodiscard]] const WidgetState* typed(const Key key,
                                           const ControlKind kind) const noexcept {
        const WidgetState* state = find(key);
        return state != nullptr && state->kind == kind ? state : nullptr;
    }

    const std::unordered_map<Key, WidgetState>* states_ = nullptr;
};

}  // namespace continui
