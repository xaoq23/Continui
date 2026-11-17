// Channel: the only standard entry for cross-thread business data
// (docs/architecture.md §4.6, ADR-011).
//
// Status: skeleton (Phase 0.1). The real queue (initially a proven
// thread-safe implementation, lock-free only if profiling demands it)
// arrives in Phase 1.
#pragma once

namespace continui {

// Placeholder. Design notes for the real implementation:
//   - created explicitly by the user via App, never a global;
//   - non-blocking send from business threads;
//   - consumption on the main thread at frame start, in defined order;
//   - queue capacity, overflow policy, and close behavior must be explicit.

}  // namespace continui
