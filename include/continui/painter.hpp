// Painter: the public drawing abstraction (docs/architecture.md §7.2).
//
// Status: skeleton (Phase 0.1). Painter records draw commands and never
// exposes backend (Dawn) types. The real command-recording implementation
// arrives with Phase 0.4.
#pragma once

namespace continui {

// Placeholder: the real Painter records into the frame's draw batches and
// is only usable inside a valid paint phase (Canvas callbacks, widget
// painting).

}  // namespace continui
