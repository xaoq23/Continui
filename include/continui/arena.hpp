// FrameArena: bump allocation for one frame's temporary view tree (ADR-005,
// docs/architecture.md §4.4). Allocations live until reset(); pointers into
// the arena MUST NOT escape the frame. Blocks are kept across resets so a
// steady-state frame allocates no memory from the system.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace continui {

class FrameArena {
public:
    FrameArena() = default;
    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;
    FrameArena(FrameArena&&) noexcept = default;
    FrameArena& operator=(FrameArena&&) noexcept = default;
    ~FrameArena() { reset(); }

    // Constructs T in the arena. Destructors run at the next reset() (in
    // reverse order), so non-trivial types such as std::string are safe.
    template <class T, class... Args>
    T* create(Args&&... args) {
        void* storage = allocate_bytes(sizeof(T), alignof(T));
        T* object = ::new (storage) T(std::forward<Args>(args)...);
        if constexpr (!std::is_trivially_destructible_v<T>) {
            destructors_.push_back({&destroy<T>, object});
        }
        ++allocations_;
        return object;
    }

    // Allocates a raw array of POD elements (no destructors needed).
    template <class T>
    T* allocate_array(std::size_t count) {
        static_assert(std::is_trivially_destructible_v<T>);
        return static_cast<T*>(allocate_bytes(count * sizeof(T), alignof(T)));
    }

    // Destroys everything created since the last reset and rewinds the bump
    // pointer; block storage is reused. Stable auto-keys restart each frame.
    void reset() noexcept {
        for (auto it = destructors_.rbegin(); it != destructors_.rend(); ++it) {
            it->first(it->second);
        }
        destructors_.clear();
        for (Block& block : blocks_) {
            block.used = 0;
        }
        current_ = 0;
        allocations_ = 0;
        auto_key_ = 0;
    }

    // Deterministic position-based identity. Safe only for view trees whose
    // structure does not reorder between frames; explicit Keys are preferred
    // for lists (documented 0.6 limitation).
    std::uint64_t next_auto_key() noexcept { return 1 + auto_key_++; }

    [[nodiscard]] std::size_t allocations() const noexcept { return allocations_; }
    [[nodiscard]] std::size_t byte_capacity() const noexcept {
        std::size_t total = 0;
        for (const Block& block : blocks_) {
            total += block.size;
        }
        return total;
    }

private:
    struct Block {
        std::unique_ptr<std::byte[]> data;
        std::size_t size = 0;
        std::size_t used = 0;
    };

    template <class T>
    static void destroy(void* object) noexcept {
        static_cast<T*>(object)->~T();
    }

    void* allocate_bytes(std::size_t bytes, std::size_t alignment) {
        if (bytes == 0) {
            bytes = 1;
        }
        if (current_ < blocks_.size()) {
            Block& block = blocks_[current_];
            const auto base = reinterpret_cast<std::uintptr_t>(block.data.get());
            const std::size_t aligned =
                static_cast<std::size_t>((base + block.used + alignment - 1)
                                         & ~(static_cast<std::uintptr_t>(alignment) - 1));
            const std::size_t offset = aligned - base;
            if (offset <= block.size && bytes <= block.size - offset) {
                block.used = offset + bytes;
                return block.data.get() + offset;
            }
        }
        // New or enlarged block: at least 4 KiB, otherwise enough for this
        // allocation alone. Earlier blocks stay for the next reset.
        const std::size_t size = std::max<std::size_t>(bytes + alignment, 4096);
        Block block{std::make_unique<std::byte[]>(size), size, 0};
        const auto base = reinterpret_cast<std::uintptr_t>(block.data.get());
        const std::size_t offset =
            static_cast<std::size_t>((base + alignment - 1)
                                     & ~(static_cast<std::uintptr_t>(alignment) - 1)) - base;
        block.used = offset + bytes;
        void* result = block.data.get() + offset;
        if (current_ == blocks_.size()) {
            blocks_.push_back(std::move(block));
        } else {
            blocks_[current_] = std::move(block);  // grow an undersized block
        }
        ++current_;
        return result;
    }

    std::vector<Block> blocks_;
    std::vector<std::pair<void (*)(void*) noexcept, void*>> destructors_;
    std::size_t current_ = 0;   // active block index
    std::size_t allocations_ = 0;
    std::uint64_t auto_key_ = 0;
};

}  // namespace continui
