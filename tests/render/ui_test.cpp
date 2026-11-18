// UI loop tests (Phase 0.6): state -> View -> layout -> paint -> hit ->
// callback -> explicit invalidation -> re-evaluation. Pure CPU; no GPU.
#include <continui/ui.hpp>

#include "batcher.hpp"

#include <cmath>
#include <memory>
#include <print>
#include <string>

namespace {

int failures = 0;

void check(const bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::println(stderr, "FAIL: {}", what);
    }
}

bool approx(const float got, const float want, const float eps = 1e-4f) {
    return std::fabs(got - want) <= eps;
}

struct FakeMeasurer final : continui::TextMeasurer {
    [[nodiscard]] continui::Size measure(const std::string_view text,
                                         const float) const override {
        return {static_cast<int>(text.size()) * 10, 20};
    }
};

}  // namespace

int main() {
    using namespace continui;

    struct State {
        int count = 42;
        int callbacks = 0;
    } state;

    // Declared before UI so the view callback can invalidate it (MSVC does
    // not allow capturing a variable during its own initialization).
    UI* ui_ptr = nullptr;
    UI ui([&](FrameArena& arena) {
        Style panel;
        panel.width = 300;
        panel.height = 200;
        panel.padding = {10, 10, 10, 10};
        panel.gap = 10;
        Style button;
        button.width = 100;
        button.height = 40;
        button.background = Color::rgb(70, 105, 230);  // paints a rect
        const auto minus = Button(arena, Key{2}, "-", [&] {
            --state.count;
            ++state.callbacks;
            ui_ptr->invalidate();
        }, button);
        const auto plus = Button(arena, Key{3}, "+", [&] {
            ++state.count;
            ++state.callbacks;
            ui_ptr->invalidate();
        }, button);
        return Column(arena, Key{1}, {minus, plus}, panel);
    });
    ui_ptr = &ui;
    ui.set_text_measurer(std::make_unique<FakeMeasurer>());

    check(ui.pending() == Invalidation::view, "a fresh UI starts invalidated");

    continui::detail::BatchBuilder batches;
    batches.begin_frame(Size{800, 600});
    Painter painter{batches};
    ui.paint(painter, Size{800, 600});
    batches.finish_frame();
    check(ui.pending() == Invalidation::none, "paint consumes the invalidation");
    check(ui.layout().boxes.size() == 3, "layout has root and two buttons");
    const LayoutBox* minus = ui.layout().find(Key{2});
    check(minus != nullptr, "button keeps its explicit key across evaluation");
    check(state.callbacks == 0, "evaluation never runs interaction callbacks");

    // Click the minus button using last frame's layout.
    Event click{};
    click.type = EventType::mouse_button_up;
    click.button = MouseButton::left;
    click.position = Point{static_cast<int>(minus->bounds.x + 20),
                           static_cast<int>(minus->bounds.y + 20)};
    check(ui.handle_event(click), "button consumes the click");
    check(state.count == 41 && state.callbacks == 1,
          "callback mutates user state exactly once");
    check(ui.pending() == Invalidation::view, "callback invalidates explicitly");

    // Next frame re-evaluates and paints; no further callback fires.
    batches.begin_frame(Size{800, 600});
    Painter painter2{batches};
    ui.paint(painter2, Size{800, 600});
    batches.finish_frame();
    check(ui.pending() == Invalidation::none, "second paint clears invalidation");
    check(state.callbacks == 1, "repaint does not re-run callbacks");
    check(!batches.vertices().empty(), "the new view produced draw geometry");

    // Outside clicks are ignored.
    Event outside = click;
    outside.position = Point{700, 500};
    check(!ui.handle_event(outside), "clicks outside buttons are ignored");
    check(state.count == 41, "ignored clicks leave state untouched");

    // --- 0.6 audit regression: resize / setters / duplicate key --------
    {
        UI local([](FrameArena& arena) {
            Style panel;
            panel.width = 300;
            panel.height = 100;
            return Solid(arena, panel);
        });
        local.set_text_measurer(std::make_unique<FakeMeasurer>());

        detail::BatchBuilder local_batches;
        local_batches.begin_frame(Size{200, 600});
        Painter p1{local_batches};
        local.paint(p1, Size{200, 600});
        local_batches.finish_frame();
        check(approx(local.layout().root_bounds.width, 200.0f),
              "small viewport clamps the root box");

        local_batches.begin_frame(Size{500, 600});
        Painter p2{local_batches};
        local.paint(p2, Size{500, 600});
        local_batches.finish_frame();
        check(approx(local.layout().root_bounds.width, 300.0f),
              "viewport resize re-solves the layout with new constraints");
        check(local.pending() == Invalidation::none,
              "resize paint consumes the layout invalidation");

        local.set_text_measurer(std::make_unique<FakeMeasurer>());
        check(local.pending() == Invalidation::layout,
              "set_text_measurer invalidates layout");
        local.set_font(nullptr);
        check(local.pending() == Invalidation::layout,
              "set_font invalidates layout");

        int first = 0;
        int second = 0;
        UI dup([&](FrameArena& arena) {
            const View a = Button(arena, Key{9}, "a", [&] { ++first; }, {});
            const View b = Button(arena, Key{9}, "b", [&] { ++second; }, {});
            return Row(arena, {a, b}, {});
        });
        dup.set_text_measurer(std::make_unique<FakeMeasurer>());
        local_batches.begin_frame(Size{800, 600});
        Painter p3{local_batches};
        dup.paint(p3, Size{800, 600});
        local_batches.finish_frame();
        const LayoutBox* box = dup.layout().find(Key{9});
        check(box != nullptr, "duplicate key still resolves to a box");
        Event dup_click{};
        dup_click.type = EventType::mouse_button_up;
        dup_click.button = MouseButton::left;
        dup_click.position = Point{static_cast<int>(box->bounds.x + 2),
                                   static_cast<int>(box->bounds.y + 10)};
        check(dup.handle_event(dup_click), "duplicate-key button consumes the click");
        check(first == 1 && second == 0,
              "duplicate key keeps the first callback instead of overwriting");
    }

    if (failures != 0) {
        std::println(stderr, "ui_test: {} check(s) failed", failures);
        return 1;
    }
    std::println("ui tests passed");
    return 0;
}
