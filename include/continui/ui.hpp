// UI orchestration: the minimal declarative loop (Phase 0.6 vertical slice,
// architecture §3). State stays with the user; `build` is the pure projection
// View = render(State). Interaction callbacks mutate state and call
// invalidate() explicitly — no hidden dependency tracking.
//
// Event handling uses the PREVIOUS frame's layout (events are pumped before
// paint); callbacks are copied out of the frame arena into a stable registry
// so nothing arena-owned escapes the frame.
#pragma once

#include <continui/arena.hpp>
#include <continui/events.hpp>
#include <continui/invalidation.hpp>
#include <continui/layout.hpp>
#include <continui/local_state.hpp>
#include <continui/painter.hpp>
#include <continui/view.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace continui {

// Per-frame stage counters (0.8). Exposed so tests can verify that explicit
// invalidation levels actually skip unnecessary work and that unchanged nodes
// reuse cached draw commands. Diagnostic only — not part of the drawing contract.
struct FrameStats {
    std::uint64_t evaluates = 0;   // ViewFn evaluations this frame's paint
    std::uint64_t solves = 0;      // layout solves this frame's paint
    std::uint64_t command_generations = 0;  // nodes whose draw commands were rebuilt
    std::uint64_t glyph_shapes = 0;  // shaping calls (0 on a pure cache replay)
};

// Readback for the platform's IME: the focused input's current text and the
// caret rectangle in client pixels. The user connects UI -> Window explicitly
// (library-first); no hidden coupling (architecture §8.3, audit F5).
struct ImeContext {
    std::string text{};
    Rect caret{};        // caret line, client pixels
    Rect text_bounds{};  // full text extent, client pixels (candidate anchor)
};

class UI {
public:
    // The view is a pure projection of user State plus read-only LocalState
    // (hover/pressed/focused, text caret/selection, IME composition). Events
    // mutate LocalState and user State, then invalidate explicitly (C3).
    using ViewFn = std::function<View(FrameArena&, const LocalState&)>;

    explicit UI(ViewFn build);
    ~UI();
    UI(const UI&) = delete;
    UI& operator=(const UI&) = delete;
    UI(UI&&) noexcept;
    UI& operator=(UI&&) noexcept;

    // Explicit invalidation from an interaction callback (C3). Multiple
    // requests merge to the strongest level (architecture §3.3).
    void invalidate(Invalidation level = Invalidation::view) noexcept;
    [[nodiscard]] Invalidation pending() const noexcept;

    // Optional font for real text measurement/painting. The face must
    // outlive this UI. Without a font, text nodes paint as empty boxes and
    // measure zero (layout still works).
    void set_font(const FontFace* face);
    // Optional per-codepoint font fallback chain (0.7). Faces are tried in
    // order when the primary face from set_font() does not cover a codepoint.
    // All faces must outlive this UI.
    void set_fallback_fonts(std::span<const FontFace* const> faces);
    // Custom measurement (tests, headless tooling). Replaces the font-based
    // measurer.
    void set_text_measurer(std::unique_ptr<TextMeasurer> measurer);

    // Called from the renderer's paint callback. Re-evaluates and re-solves
    // only when invalidated, then paints the current layout. `viewport`
    // constrains the root box (client-area pixels).
    void paint(Painter& painter, Size viewport);

    // Called from the window event handler. Returns true when the event was
    // consumed by a widget (click, key, text input, or IME composition).
    // Callbacks mutate user State / LocalState and invalidate explicitly.
    bool handle_event(const Event& event);

    [[nodiscard]] const LayoutResult& layout() const noexcept;

    // Current IME context (text + caret) for candidate positioning; valid
    // after paint(). Empty when no input is focused.
    [[nodiscard]] ImeContext ime_context() const noexcept;

    // Diagnostic stage counters for the last paint() call (0.8).
    [[nodiscard]] FrameStats frame_stats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace continui
