// Stable node identity.
//
// A Key identifies a logical node across consecutive view updates.
// Rules (docs/architecture.md §4.3):
//   1. The same logical node must keep the same Key across frames.
//   2. A Key must not depend on the node's position inside a container.
//   3. Duplicate Keys must be detected.
//   4. When a node is destroyed, its local state must be reclaimable.
//   5. Key identity is scoped to the owning window / root tree.
#pragma once

#include <cstdint>
#include <functional>

namespace continui {

struct Key {
    std::uint64_t value = 0;
};

constexpr bool operator==(Key lhs, Key rhs) noexcept { return lhs.value == rhs.value; }
constexpr bool operator!=(Key lhs, Key rhs) noexcept { return lhs.value != rhs.value; }

}  // namespace continui

// Stable hash for node identity maps.
template <>
struct std::hash<continui::Key> {
    std::size_t operator()(const continui::Key& key) const noexcept {
        return std::hash<std::uint64_t>{}(key.value);
    }
};
