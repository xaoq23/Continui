// Dawn GPU renderer (ADR-009).
//
// Status: Phase 0.4 — batched 2D drawing. One pipeline draws anti-aliased
// rects / rounded rects / textures with premultiplied alpha and scissor
// clipping, driven by the paint callback's Painter records (ADR-007). The
// gfx::Device abstraction and an invalidation-driven frame path remain
// future work.
//
// Threading contract: main thread only, like all App/Window methods.
#pragma once

#include <continui/app.hpp>
#include <continui/events.hpp>
#include <continui/painter.hpp>
#include <continui/texture.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
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

// Frame-ticked Dawn renderer for one window. render_frame() records the
// paint callback into batches, uploads them, and presents one indexed draw
// per batch. It reconfigures the swapchain whenever the client size changed
// (Phase 0.3 interim: size polling, no invalidation-driven repaint yet).
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

    // Registers the per-frame paint callback. Each presented frame calls it
    // once with a Painter that records into that frame's batches; records
    // made outside the callback draw nothing. Passing nullptr clears it.
    // The callback runs on the main thread inside render_frame();
    // exceptions propagate to the run_one_frame() caller (ADR-019, same
    // boundary as event callbacks).
    void set_paint_callback(std::function<void(Painter&)> callback);

    // Uploads straight-alpha RGBA8 pixel data as a texture that lives until
    // this renderer is destroyed. Returns an invalid handle when the input
    // is rejected (empty size, null data, or byte_count != width*height*4)
    // or when Dawn rejects the upload. Ids are never reused; drawn through
    // Painter::image().
    Texture create_texture(Size size, const std::uint8_t* rgba,
                           std::size_t byte_count);

    // Main-thread diagnostic: true once any uncaptured GPU error occurred
    // since create() (WGSL compile failure, validation error, ...). For
    // tests and bring-up debugging; not part of the drawing contract.
    bool had_uncaptured_errors() const;

private:
    Renderer() = default;

    std::unique_ptr<detail::DawnRendererImpl> impl_;
};

}  // namespace dawn
}  // namespace continui
