// Dawn GPU renderer (ADR-009).
//
// Status: Phase 0.3 — device/surface/swapchain bring-up and one triangle.
// The gfx::Device abstraction and Painter arrive with the render system in
// Phase 0.4; until then this is the only GPU-facing public type.
//
// Threading contract: main thread only, like all App/Window methods.
#pragma once

#include <continui/app.hpp>

#include <expected>
#include <memory>

namespace continui {

namespace detail {
class DawnRendererImpl;
}

enum class GpuError {
    backend_init_failed,      // instance/adapter/device creation failed
    surface_creation_failed,  // the window surface was rejected
    window_invalid,           // the window handle is not usable
};

namespace dawn {

// Frame-ticked Dawn renderer for one window. render_frame() clears, draws
// one triangle, and presents. It reconfigures the swapchain whenever the
// client size changed (Phase 0.3 interim: size polling, no invalidation-
// driven repaint yet — that arrives with the render system).
class Renderer {
public:
    // Attaches to the window's native surface and registers an App frame
    // callback that calls render_frame(). Fails when the GPU stack cannot
    // be brought up or the window has no native handle.
    //
    // Lifetime: the Renderer must be destroyed before the App (and any
    // Window handles from it) it was created with, and the App must not be
    // moved after create(). The frame callback holds a pointer into the
    // renderer, so violating that order is use-after-free — enforced by
    // convention until the render system owns this wiring.
    static std::expected<Renderer, GpuError> create(App& app, const Window& window);

    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) noexcept;
    ~Renderer();

    // Draws and presents one frame. No-op when the window is gone or the
    // renderer was moved-from.
    void render_frame();

private:
    Renderer() = default;

    std::unique_ptr<detail::DawnRendererImpl> impl_;
};

}  // namespace dawn
}  // namespace continui
