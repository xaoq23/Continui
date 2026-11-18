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

void test_interaction() {
    using namespace continui;
    struct State {
        int count = 0;
        std::string text = "Hi";
        int text_changes = 0;
    } state;
    bool observed_hover = false;
    bool observed_focus = false;
    bool observed_composing = false;
    std::string observed_composition;

    UI ui([&](FrameArena& arena, const LocalState& local) {
        observed_hover = local.hovered(Key{10});
        observed_focus = local.focused(Key{20});
        const WidgetState* input_state = local.input(Key{20});
        observed_composing = input_state != nullptr && input_state->composing;
        observed_composition = input_state != nullptr ? input_state->composition
                                                      : std::string{};
        check(local.button(Key{20}) == nullptr,
              "typed LocalState lookup rejects a category mismatch");

        Style button;
        button.width = 100;
        button.height = 40;
        Style input;
        input.width = 120;
        input.height = 40;
        const auto b = Button(arena, Key{10}, "Btn", [&] { ++state.count; }, button);
        const auto i = Input(arena, Key{20}, state.text, [&](std::string_view s) {
            state.text = std::string(s);
            ++state.text_changes;
        }, input);
        return Row(arena, {b, i}, Style{});
    });
    ui.set_text_measurer(std::make_unique<FakeMeasurer>());

    detail::BatchBuilder batches;
    auto repaint = [&] {
        batches.begin_frame(Size{800, 600});
        Painter painter{batches};
        ui.paint(painter, Size{800, 600});
        batches.finish_frame();
    };
    repaint();

    // Hover is framework-owned, read-only in ViewFn, and survives to next frame.
    Event move{};
    move.type = EventType::mouse_move;
    move.position = Point{50, 20};
    check(ui.handle_event(move), "mouse move over a widget is consumed");
    repaint();
    check(observed_hover, "LocalState reports hovered after repaint");

    // Tab focuses the first widget; Enter activates the focused button.
    Event tab{};
    tab.type = EventType::key_down;
    tab.key = KeyCode::tab;
    check(ui.handle_event(tab), "Tab is consumed");
    repaint();
    Event enter{};
    enter.type = EventType::key_down;
    enter.key = KeyCode::enter;
    check(ui.handle_event(enter), "Enter is consumed by the focused button");
    check(state.count == 1, "Enter activates the focused button");

    // A second Tab moves focus to the input; edits flow back to user state.
    check(ui.handle_event(tab), "second Tab is consumed");
    repaint();
    check(observed_focus, "LocalState reports focused on the input");

    Event text{};
    text.type = EventType::text_input;
    text.text = "X";
    check(ui.handle_event(text), "text input is consumed");
    repaint();
    check(state.text == "HiX", "insertion reports the edited text to the user");
    check(state.text_changes == 1, "text callback fires once per edit");

    Event backspace{};
    backspace.type = EventType::key_down;
    backspace.key = KeyCode::backspace;
    check(ui.handle_event(backspace), "backspace is consumed");
    repaint();
    check(state.text == "Hi", "backspace removes the last character");

    // IME composition stays separate from plain keys until commit (§8.3).
    Event comp_start{};
    comp_start.type = EventType::composition_start;
    check(ui.handle_event(comp_start), "composition start is consumed");
    Event comp_update{};
    comp_update.type = EventType::composition_update;
    comp_update.text = "中";
    check(ui.handle_event(comp_update), "composition update is consumed");
    repaint();
    check(observed_composing && observed_composition == "中",
          "LocalState exposes the IME composition preview");
    Event comp_end{};
    comp_end.type = EventType::composition_end;
    comp_end.text = "中";  // TSF SetText path supplies the commit payload
    check(ui.handle_event(comp_end), "composition end is consumed");
    repaint();
    check(state.text == "Hi中", "composition commit reports text to the user");

    // F9: TSF commits via text_input and then ends composition with an empty
    // payload. The preview string must NOT be inserted a second time.
    Event cs2{};
    cs2.type = EventType::composition_start;
    check(ui.handle_event(cs2), "second composition start is consumed");
    Event cu2{};
    cu2.type = EventType::composition_update;
    cu2.text = "预";
    ui.handle_event(cu2);
    Event commit{};
    commit.type = EventType::text_input;
    commit.text = "Z";
    ui.handle_event(commit);
    Event ce2{};
    ce2.type = EventType::composition_end;
    ce2.text.clear();
    check(ui.handle_event(ce2), "empty-payload composition end is consumed");
    check(state.text == "Hi中Z",
          "text_input commit + empty composition_end inserts exactly once");
}

void test_editing_regressions() {
    using namespace continui;
    struct State {
        std::string text;
    } state;
    UI ui([&](FrameArena& arena, const LocalState&) {
        Style input;
        input.width = 200;
        input.height = 40;
        return Input(arena, Key{30}, state.text,
                     [&](std::string_view s) { state.text = std::string(s); },
                     input);
    });
    ui.set_text_measurer(std::make_unique<FakeMeasurer>());

    detail::BatchBuilder batches;
    auto repaint = [&] {
        batches.begin_frame(Size{800, 600});
        Painter painter{batches};
        ui.paint(painter, Size{800, 600});
        batches.finish_frame();
    };
    repaint();
    Event tab{};
    tab.type = EventType::key_down;
    tab.key = KeyCode::tab;
    ui.handle_event(tab);
    repaint();

    // F2: a burst of input events before a repaint must accumulate, not
    // overwrite using the previous frame's stale node text.
    Event a{};
    a.type = EventType::text_input;
    a.text = "A";
    check(ui.handle_event(a), "first text event consumed");
    Event b{};
    b.type = EventType::text_input;
    b.text = "B";
    check(ui.handle_event(b), "second text event consumed without repaint");
    check(state.text == "AB", "two edits before repaint accumulate");
    repaint();

    // F3: forward delete at a mid-string caret removes the next character.
    Event left{};
    left.type = EventType::key_down;
    left.key = KeyCode::left;
    ui.handle_event(left);  // caret 2 -> 1 (before 'B')
    Event del{};
    del.type = EventType::key_down;
    del.key = KeyCode::del;
    ui.handle_event(del);
    check(state.text == "A", "forward delete removes the character after caret");
    repaint();

    // F3: backspace then a follow-up insertion uses the corrected caret.
    Event bs{};
    bs.type = EventType::key_down;
    bs.key = KeyCode::backspace;
    ui.handle_event(bs);
    check(state.text.empty(), "backspace at the end removes the last character");
    repaint();
    Event x{};
    x.type = EventType::text_input;
    x.text = "X";
    ui.handle_event(x);
    check(state.text == "X", "insertion after backspace lands at the caret");
}

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
    UI ui([&](FrameArena& arena, const LocalState&) {
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
    click.button = MouseButton::left;
    click.position = Point{static_cast<int>(minus->bounds.x + 20),
                           static_cast<int>(minus->bounds.y + 20)};
    Event down = click;
    down.type = EventType::mouse_button_down;
    check(ui.handle_event(down), "button consumes mouse down");
    click.type = EventType::mouse_button_up;
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
    Event outside_down = outside;
    outside_down.type = EventType::mouse_button_down;
    check(!ui.handle_event(outside_down), "mouse down outside buttons is ignored");
    check(!ui.handle_event(outside), "clicks outside buttons are ignored");
    check(state.count == 41, "ignored clicks leave state untouched");

    // --- 0.6 audit regression: resize / setters / duplicate key --------
    {
        UI local([](FrameArena& arena, const LocalState&) {
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
        UI dup([&](FrameArena& arena, const LocalState&) {
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
        dup_click.button = MouseButton::left;
        dup_click.position = Point{static_cast<int>(box->bounds.x + 2),
                                   static_cast<int>(box->bounds.y + 10)};
        Event dup_down = dup_click;
        dup_down.type = EventType::mouse_button_down;
        dup.handle_event(dup_down);
        dup_click.type = EventType::mouse_button_up;
        check(dup.handle_event(dup_click), "duplicate-key button consumes the click");
        check(first == 1 && second == 0,
              "duplicate key keeps the first callback instead of overwriting");
    }

    test_interaction();
    test_editing_regressions();

    if (failures != 0) {
        std::println(stderr, "ui_test: {} check(s) failed", failures);
        return 1;
    }
    std::println("ui tests passed");
    return 0;
}
