// App: GUI runtime entry point (docs/architecture.md §4.7).
//
// Status: skeleton (Phase 0.1). The real lifecycle (managed loop,
// run_one_frame, external event-loop embedding) arrives with Phase 0.2.
#pragma once

namespace continui {

enum class AppError {
    platform_init_failed,
};

class App {
public:
    App() = default;
};

}  // namespace continui
