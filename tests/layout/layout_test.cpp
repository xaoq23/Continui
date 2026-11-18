// Phase 0.6 pure-CPU tests: arena lifetime, view tree construction, flex
// solver behavior, hit testing, and explicit invalidation merging.
// No GPU, no platform, no real fonts (a fake TextMeasurer is injected).
#include <continui/arena.hpp>
#include <continui/invalidation.hpp>
#include <continui/layout.hpp>
#include <continui/view.hpp>

#include <cmath>
#include <print>
#include <string>
#include <vector>
#include <string_view>

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
        return {static_cast<int>(text.size()) * 8, 20};
    }
};

void test_arena() {
    using namespace continui;
    struct Tracked {
        int* counter;
        explicit Tracked(int* c) : counter(c) {}
        ~Tracked() { ++*counter; }
    };
    FrameArena arena;
    int destroyed = 0;
    auto* a = arena.create<Tracked>(&destroyed);
    check(a != nullptr && destroyed == 0, "arena object alive until reset");
    const std::size_t capacity = arena.byte_capacity();
    arena.reset();
    check(destroyed == 1, "arena reset runs destructors");
    check(arena.byte_capacity() == capacity, "arena reuses block storage");
    check(arena.next_auto_key() == 1, "auto keys restart each frame");
    arena.create<int>(7);
    arena.reset();
    check(arena.allocations() == 0, "reset clears allocation bookkeeping");
}

void test_arena_growth_across_frames() {
    using namespace continui;
    FrameArena arena;
    // 400 non-trivial objects (>4 KiB) force multiple blocks; rebuilding the
    // same workload for 4 frames exercises the "current block too small"
    // path with LIVE objects still in older blocks (0.6 audit CRITICAL:
    // replacing an occupied block caused a use-after-free in frame 2).
    for (int frame = 0; frame < 4; ++frame) {
        arena.reset();
        std::vector<std::string*> strings;
        strings.reserve(400);
        for (int i = 0; i < 400; ++i) {
            strings.push_back(arena.create<std::string>(
                std::string(48, static_cast<char>('a' + (i % 26)))));
        }
        bool intact = true;
        for (int i = 0; i < 400; ++i) {
            intact = intact && strings[i]->size() == 48
                     && (*strings[i])[0] == static_cast<char>('a' + (i % 26));
        }
        check(intact, "arena objects survive multi-frame block growth");
    }
    arena.reset();
    check(arena.allocations() == 0, "multi-frame arena resets cleanly");
}

void test_solver() {
    using namespace continui;
    FakeMeasurer measurer;
    const Constraints loose{};

    // --- single text: intrinsic size, max-width clamp --------------------
    {
        FrameArena arena;
        const View view = Text(arena, "ABCD");  // 4*8 x 20
        const auto result = solve(view, loose, measurer);
        check(result.boxes.size() == 1, "one text -> one layout box");
        check(approx(result.boxes[0].bounds.width, 32.0f)
              && approx(result.boxes[0].bounds.height, 20.0f),
              "text intrinsic size from the measurer");
        const auto clamped = solve(view, Constraints{0, 12, 0, 100}, measurer);
        check(approx(clamped.boxes[0].bounds.width, 12.0f),
              "max-width constraint clamps the box");
    }

    // --- column: gap, padding, preorder indices -------------------------
    {
        FrameArena arena;
        Style column_style;
        column_style.padding = Edges{10, 5, 10, 5};
        column_style.gap = 8;
        const View a = Text(arena, "AB");   // 16x20
        const View b = Button(arena, "C", {});  // 8x20
        const View root = Column(arena, Key{7}, {a, b}, column_style);
        const auto result = solve(root, loose, measurer);
        check(result.boxes.size() == 3, "column keeps root + 2 children");
        check(result.boxes[0].key == Key{7}, "root keeps its explicit key");
        check(approx(result.boxes[1].bounds.x, 10.0f)
              && approx(result.boxes[1].bounds.y, 5.0f),
              "padding offsets the first child");
        check(approx(result.boxes[2].bounds.y, 5.0f + 20.0f + 8.0f),
              "gap separates children on the main axis");
        check(approx(result.root_bounds.width, 36.0f)
              && approx(result.root_bounds.height, 58.0f),
              "container size = max child + padding, sum + gap");
    }

    // --- flex grow shares the remaining main axis -----------------------
    {
        FrameArena arena;
        Style row_style;
        row_style.width = 200;
        const Style growing = Style{};
        // Use mutable styles for the children (flex_grow must differ).
        Style left;
        left.flex_grow = 1;
        Style right;
        right.flex_grow = 1;
        const View a = Solid(arena, left);
        const View b = Solid(arena, right);
        const View root = Row(arena, {a, b}, row_style);
        const auto result = solve(root, loose, measurer);
        check(approx(result.boxes[1].bounds.width, 100.0f)
              && approx(result.boxes[2].bounds.width, 100.0f),
              "flex grow splits free main space evenly");
        check(approx(result.boxes[2].bounds.x, 100.0f),
              "second child starts after the first");
        (void)growing;
    }

    // --- alignment and justification -----------------------------------
    {
        FrameArena arena;
        Style row_style;
        row_style.width = 100;
        row_style.height = 40;
        row_style.align_items = Align::center;
        row_style.justify_content = Justify::end;
        const View child = Text(arena, "AB");  // 16x20
        const View root = Row(arena, {child}, row_style);
        const auto result = solve(root, loose, measurer);
        check(approx(result.boxes[1].bounds.x, 84.0f),
              "justify end places the child at the far main edge");
        check(approx(result.boxes[1].bounds.y, 10.0f),
              "align center centers the child on the cross axis");
    }

    // --- hit testing picks the deepest box ------------------------------
    {
        FrameArena arena;
        Style outer;
        outer.width = 100;
        outer.height = 100;
        Style inner;
        inner.width = 30;
        inner.height = 30;
        inner.margin = {10, 10, 0, 0};
        const View child = Button(arena, "X", {}, inner);
        const View root = Solid(arena, outer);
        // Wrapping the button directly keeps the test independent of child
        // placement details: use a row with padding for a predictable spot.
        Style container;
        container.width = 100;
        container.height = 100;
        const View wrapped = Row(arena, {child}, container);
        const auto result = solve(wrapped, loose, measurer);
        const auto hit = hit_test(result, PointF{11, 11});
        check(hit.has_value() && result.boxes[*hit].is_button,
              "hit test finds the button under the point");
        check(!hit_test(result, PointF{200, 200}).has_value(),
              "points outside the tree hit nothing");
        (void)root;
    }
}

void test_layout_regressions() {
    using namespace continui;
    FakeMeasurer measurer;
    const Constraints loose{};

    // --- child margin contributes to intrinsic container size ----------
    {
        FrameArena arena;
        Style child;
        child.margin = {5, 7, 5, 7};  // horizontal 10, vertical 14
        const View root = Column(arena, {Solid(arena, child)}, Style{});
        const auto result = solve(root, loose, measurer);
        check(approx(result.root_bounds.width, 10.0f)
              && approx(result.root_bounds.height, 14.0f),
              "child margin is included in intrinsic container size");
    }

    // --- margin-inclusive slot: child keeps its measured border box -------
    {
        FrameArena arena;
        Style child;
        child.margin = {5, 0, 5, 0};  // horizontal 10
        const View root = Row(arena, {Text(arena, "AB", child)}, Style{});
        const auto result = solve(root, loose, measurer);
        check(approx(result.root_bounds.width, 26.0f),
              "container intrinsic width includes child margin");
        check(approx(result.boxes[1].bounds.width, 16.0f),
              "child keeps its measured width inside a margin-inclusive slot");
        check(approx(result.boxes[1].bounds.x, 5.0f),
              "child margin offsets it inside its slot");
    }

    // --- min/max applied once on the border box, not doubled by padding -
    {
        FrameArena arena;
        Style style;
        style.min_width = 100;
        style.padding = {20, 0, 20, 0};  // horizontal 40
        const View root = Text(arena, "AB", style);  // intrinsic width 16
        const auto result = solve(root, loose, measurer);
        check(approx(result.root_bounds.width, 100.0f),
              "auto size clamps min-width once on the border box");
    }

    // --- column flex_basis clamps against height, not width -------------
    {
        FrameArena arena;
        Style column;
        column.height = 200;
        Style basis;
        basis.flex_basis = 80;
        basis.min_height = 120;  // must lift the basis to 120
        basis.min_width = 1000;  // must NOT affect a column's main axis
        const View root = Column(arena, {Solid(arena, basis)}, column);
        const auto result = solve(root, loose, measurer);
        check(approx(result.boxes[1].bounds.height, 120.0f),
              "column flex_basis uses min/max_height for its main axis");
    }
}

void test_invalidation() {
    using continui::Invalidation;
    using continui::merge_invalidation;
    check(merge_invalidation(Invalidation::paint, Invalidation::layout)
              == Invalidation::layout,
          "paint + layout merges to layout");
    check(merge_invalidation(Invalidation::layout, Invalidation::view)
              == Invalidation::view,
          "layout + view merges to view");
    check(merge_invalidation(Invalidation::view, Invalidation::full)
              == Invalidation::full,
          "view + full merges to full");
    check(merge_invalidation(Invalidation::none, Invalidation::paint)
              == Invalidation::paint,
          "none is neutral");
}

}  // namespace

int main() {
    test_arena();
    test_arena_growth_across_frames();
    test_solver();
    test_layout_regressions();
    test_invalidation();
    if (failures == 0) {
        std::println("layout tests passed");
    }
    return failures == 0 ? 0 : 1;
}
