// Texture: a lightweight handle to a GPU-owned 2D RGBA8 texture.
//
// Status: Phase 0.4 (minimal). Textures are created through the GPU
// renderer (see backend_dawn.hpp) and stay alive until that renderer is
// destroyed — the renderer owns them all, which keeps Phase 0.4 lifetime
// rules trivial; explicit destruction arrives with the TextureCache in a
// later phase. Default-constructed handles are legal to pass around and
// draw nothing.
#pragma once

#include <continui/events.hpp>

#include <cstdint>

namespace continui {

namespace dawn {
class Renderer;  // defined in backend_dawn.hpp; mints handles
}  // namespace dawn

namespace detail {
class DawnRendererImpl;  // defined in the dawn backend; mints handles
}  // namespace detail

class Painter;

class Texture {
public:
    Texture() = default;

    [[nodiscard]] Size size() const noexcept { return size_; }
    [[nodiscard]] bool valid() const noexcept { return id_ != 0; }

private:
    friend class dawn::Renderer;
    friend class detail::DawnRendererImpl;
    friend class Painter;

    explicit Texture(std::uint64_t id, Size size) noexcept : id_(id), size_(size) {}

    // Hidden friend: equality compares only the handle identity.
    friend constexpr bool operator==(const Texture& lhs, const Texture& rhs) noexcept {
        return lhs.id_ == rhs.id_;
    }

    std::uint64_t id_ = 0;
    Size size_{};
};

}  // namespace continui
