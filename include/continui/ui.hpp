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
#include <continui/painter.hpp>
#include <continui/view.hpp>

#include <functional>
#include <memory>

namespace continui {

class UI {
public:
    using ViewFn = std::function<View(FrameArena&)>;

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
    // Custom measurement (tests, headless tooling). Replaces the font-based
    // measurer.
    void set_text_measurer(std::unique_ptr<TextMeasurer> measurer);

    // Called from the renderer's paint callback. Re-evaluates and re-solves
    // only when invalidated, then paints the current layout. `viewport`
    // constrains the root box (client-area pixels).
    void paint(Painter& painter, Size viewport);

    // Called from the window event handler. Returns true when a button
    // consumed the click. The callback itself calls invalidate().
    bool handle_event(const Event& event);

    [[nodiscard]] const LayoutResult& layout() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace continui
