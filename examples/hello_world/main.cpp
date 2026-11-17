// Hello Continui.
//
// Phase 0.1 smoke example: builds against the umbrella target and the
// draft public headers. Real UI (window, view, button) arrives in
// Phase 0.2–0.7; this example only proves the build system works.
#include <continui/continui.hpp>

#include <print>

int main() {
    using namespace continui;

    // Draft headers compile and the minimal types behave.
    constexpr Style style{};
    static_assert(style.background.a == 0);

    std::println("Hello Continui!");
    return 0;
}
