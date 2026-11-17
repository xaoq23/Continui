// Core module tests. Phase 0 policy: the test framework choice is deferred
// (cizon test would only check exit codes anyway). Runtime checks must not
// depend on <cassert> so that a Release (-DNDEBUG) run stays meaningful.
#include <continui/key.hpp>

#include <unordered_set>

int check_runtime() {
    using continui::Key;

    std::unordered_set<Key> seen;
    seen.insert(Key{1});
    if (!seen.contains(Key{1})) return 1;   // equality/hash identity failed
    if (seen.contains(Key{2})) return 1;    // different keys collided

    return 0;
}

int main() {
    using continui::Key;

    // Compile-time contract.
    constexpr Key default_key{};
    static_assert(default_key == Key{0});

    constexpr Key a{1};
    constexpr Key b{1};
    constexpr Key c{2};
    static_assert(a == b);
    static_assert(a != c);

    // Runtime contract (independent of NDEBUG).
    return check_runtime();
}
