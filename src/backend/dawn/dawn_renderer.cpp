// Dawn renderer implementation (ADR-009). Bring-up follows the canonical
// sequence from Dawn's docs/quickstart-cmake.md, verified against the pinned
// Dawn sources (tag v20261113.214538):
//   CreateInstance -> RequestAdapter (futures callback) -> RequestDevice ->
//   CreateSurface (SurfaceSourceWindowsHWND chain) -> GetCapabilities ->
//   Configure -> per-frame GetCurrentTexture / render / Present.
//
// Phase 0.4 rendering: the paint callback records into a BatchBuilder; each
// frame uploads its vertex/index data into persistent buffers and issues one
// indexed draw per batch (one texture + one scissor). One pipeline serves
// everything: the fragment stage evaluates a rounded-box SDF for the
// anti-aliased shape alpha, samples the texture (solid rects use a 1x1
// white texture), and blends premultiplied alpha.
//
// API notes (why some names look unfamiliar):
//   - RequestAdapter/RequestDevice are futures-based; the synchronous path
//     is WaitAny(future, UINT64_MAX) with CallbackMode::WaitAnyOnly.
//   - The WGSL descriptor is ShaderSourceWGSL (renamed from the older
//     ShaderModuleWGSLDescriptor); it chains into ShaderModuleDescriptor.
//   - SwapChain is gone from the modern path; Surface + configuration
//     replaces it ("SwapChain resources" in development.md Phase 0.3).

#include "dawn_renderer.hpp"

#include <continui/platform_windows.hpp>

#include "batcher.hpp"
#include "glyph_cache.hpp"
#include "text_context.hpp"
#include "dawn/webgpu_cpp.h"

// min/max macros would poison the Dawn headers and the std:: usage below.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace continui::detail {
namespace {

// Pixel-space pipeline. Vertex input matches BatchVertex exactly (64 bytes).
// The SDF parameters travel per-vertex so rounded corners cost nothing.
constexpr char kQuadShader[] = R"(
    struct ScreenUniform {
        half_size : vec2f,
        padding : vec2f,
    };
    @group(0) @binding(0) var<uniform> screen : ScreenUniform;

    struct VertexIn {
        @location(0) pos_uv : vec4f,
        @location(1) tint : vec4f,
        @location(2) rect : vec4f,    // cx, cy, hw, hh (pixel space)
        @location(3) radius : vec4f,  // tl, tr, br, bl
    };

    struct VertexOut {
        @builtin(position) pos : vec4f,
        @location(0) uv : vec2f,
        @location(1) tint : vec4f,
        @location(2) rect : vec4f,
        @location(3) radius : vec4f,
    };

    @vertex
    fn vs(v : VertexIn) -> VertexOut {
        var out : VertexOut;
        // Pixel (y down) -> NDC (y up). The max() guards the uniform being
        // zero before the first frame; real frames always have a size.
        let ndc_x = v.pos_uv.x / max(screen.half_size.x, 1.0) - 1.0;
        let ndc_y = 1.0 - v.pos_uv.y / max(screen.half_size.y, 1.0);
        out.pos = vec4f(ndc_x, ndc_y, 0.0, 1.0);
        out.uv = v.pos_uv.zw;
        out.tint = v.tint;
        out.rect = v.rect;
        out.radius = v.radius;
        return out;
    }

    @group(1) @binding(0) var samp : sampler;
    @group(1) @binding(1) var tex : texture_2d<f32>;

    // radius = (tl, tr, br, bl); pixel y grows downward, so p.y < 0 is the
    // top half. select(false_value, true_value, condition).
    fn corner_radius(radius : vec4f, p : vec2f) -> f32 {
        return select(
            select(radius.z, radius.y, p.y < 0.0),  // right half: br / tr
            select(radius.w, radius.x, p.y < 0.0),  // left half:  bl / tl
            p.x < 0.0);
    }

    // Rounded-box signed distance (Inigo Quilez's form, scalar radius).
    // d < 0 inside the shape, d = 0 on the edge.
    fn sd_rounded_box(p : vec2f, b : vec2f, r : f32) -> f32 {
        let q = abs(p) - b + r;
        return length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r;
    }

    @fragment
    fn fs(in : VertexOut) -> @location(0) vec4f {
        // @builtin(position) arrives in framebuffer coordinates with the
        // pixel center at (x.5, y.5) — exactly the coordinate the distance
        // field consumes, and y already grows downward like our pixel space.
        let p = in.pos.xy - in.rect.xy;
        let d = sd_rounded_box(p, in.rect.zw, corner_radius(in.radius, p));

        // One-pixel anti-aliased coverage; the field is in pixel units, so
        // its screen-space derivative is ~1.
        let aa = max(fwidth(d), 1e-6);
        let coverage = 1.0 - smoothstep(-aa, aa, d);

        let texel = textureSample(tex, samp, in.uv);
        // CPU pixels are straight alpha; blending is premultiplied, and a
        // premul tint multiplies componentwise in premul space.
        var color = vec4f(texel.rgb * texel.a, texel.a);
        color *= coverage;
        return color * in.tint;
    }

    // Phase 0.5 text: the atlas texel's alpha IS the glyph coverage — no
    // shape SDF, whose edge fade would eat the outermost glyph pixels.
    // Contrast comes from the ink luminance (Impeller: 1 + luma * max in
    // the CPU stage, then 1 - (1-coverage)^contrast here). The white mask
    // (255,255,255,c) becomes premultiplied (c,c,c,c), then tint multiplies.
    @fragment
    fn fs_text(in : VertexOut) -> @location(0) vec4f {
        let mask = textureSample(tex, samp, in.uv).a;
        let contrast = max(in.radius.x, 1.0);
        let coverage = 1.0 - pow(1.0 - mask, contrast);
        return vec4f(coverage, coverage, coverage, coverage) * in.tint;
    }
)";

// wgpu::StringView bytes are not guaranteed NUL-terminated; copy before
// handing them to C string APIs. Bring-up diagnostics only, never control
// flow (failures surface through the return value).
void debug_log(const char* prefix, wgpu::StringView message) {
    std::string text = prefix;
    if (message.data != nullptr && message.length > 0) {
        text.append(message.data, message.length);
    }
    text += '\n';
    OutputDebugStringA(text.c_str());
}

// Repeatable callbacks (uncaptured error, device lost) must be captureless:
// Dawn static_asserts capturing lambdas away, so state travels via userdata.
// Defined after DawnRendererImpl (needs the complete type); device-lost
// logging needs no state and uses a captureless lambda inline.
void on_uncaptured_error(const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message,
                         void* userdata);

}  // namespace

// One Dawn bring-up for one window. All members are main-thread-only; the
// owning Renderer must die before the App (its destructor unregisters the
// App frame callback first, keeping the callback pointer valid until then).
class DawnRendererImpl final : public TextRasterContext {
public:
    DawnRendererImpl() {
        // Growth can happen mid-paint; flush pending pixels to the old atlas
        // texture before the strike cache rebuilds (see GlyphCache).
        glyph_cache_.set_before_growth_flush([this] { sync_atlas(); });
    }

    DawnRendererImpl(const DawnRendererImpl&) = delete;
    DawnRendererImpl& operator=(const DawnRendererImpl&) = delete;

    ~DawnRendererImpl() {
        if (registered_) {
            app_->set_frame_callback(nullptr);  // the callback captures this
        }
        // Release in reverse creation order; Unconfigure detaches the
        // surface from the swapchain state before the handles die. The
        // texture map / buffers / bind groups release before device_ does
        // (member declaration order), so their last references die first.
        pipeline_ = nullptr;
        text_pipeline_ = nullptr;
        textures_.clear();
        white_bind_group_ = nullptr;
        white_view_ = nullptr;
        screen_bind_group_ = nullptr;
        sampler_ = nullptr;
        vertex_buffer_ = nullptr;
        index_buffer_ = nullptr;
        screen_buffer_ = nullptr;
        if (surface_) {
            surface_.Unconfigure();
            surface_ = nullptr;
        }
        queue_ = nullptr;
        device_ = nullptr;
        adapter_ = nullptr;
        instance_ = nullptr;
    }

    // Called synchronously on the calling thread by on_uncaptured_error.
    void note_uncaptured_error(wgpu::StringView message) {
        error_seen_ = true;
        debug_log("continui: uncaptured GPU error: ", message);
    }

    bool had_uncaptured_errors() const { return error_seen_; }

    // TextRasterContext: resolves a glyph into normalized atlas UVs. A
    // growth rebuild swaps the GPU texture here (before the quad records
    // its id); older quads this frame keep the old id and UVs, which stay
    // valid because textures live until the renderer dies.
    GlyphSlot glyph(const FontFace& face, const float size_px, const int x_bucket,
                    const std::uint32_t glyph_id) override {
        const auto entry = glyph_cache_.glyph(face, size_px, x_bucket, glyph_id);
        if (!entry) {
            return {};
        }
        if (atlas_id_ == 0
            || glyph_cache_.generation() != atlas_generation_) {
            create_atlas_texture();
        }
        if (atlas_id_ == 0) {
            return {};  // texture creation failed; uncaptured error is set
        }
        const float width = static_cast<float>(glyph_cache_.atlas().width());
        const float height = static_cast<float>(glyph_cache_.atlas().height());
        return GlyphSlot{Rect{entry->uv_px.x / width, entry->uv_px.y / height,
                              entry->uv_px.width / width,
                              entry->uv_px.height / height},
                         entry->left, entry->top, entry->uv_px.width,
                         entry->uv_px.height};
    }

    std::uint64_t atlas_texture() const override { return atlas_id_; }

    std::expected<void, GpuError> initialize(App& app, const Window& window) {
        app_ = &app;

        void* hwnd = platform_windows::native_window_handle(window);
        if (hwnd == nullptr) {
            return std::unexpected(GpuError::window_invalid);
        }
        hwnd_ = hwnd;

        // TimedWaitAny enables WaitAny with finite timeouts; this bring-up
        // only ever uses UINT64_MAX, but the canonical setup keeps the
        // feature on so later phases can bound waits.
        static constexpr auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
        const wgpu::InstanceDescriptor instance_desc{
            .requiredFeatureCount = 1,
            .requiredFeatures = &kTimedWaitAny,
        };
        instance_ = wgpu::CreateInstance(&instance_desc);
        if (!instance_) {
            return std::unexpected(GpuError::backend_init_failed);
        }

        if (!request_adapter_and_device()) {
            return std::unexpected(GpuError::backend_init_failed);
        }

        wgpu::SurfaceSourceWindowsHWND hwnd_source{};
        hwnd_source.hinstance = static_cast<void*>(GetModuleHandleW(nullptr));
        hwnd_source.hwnd = hwnd_;
        wgpu::SurfaceDescriptor surface_desc{};
        surface_desc.nextInChain = &hwnd_source;
        surface_ = instance_.CreateSurface(&surface_desc);
        if (!surface_) {
            return std::unexpected(GpuError::surface_creation_failed);
        }

        wgpu::SurfaceCapabilities capabilities{};
        if (surface_.GetCapabilities(adapter_, &capabilities) != wgpu::Status::Success
            || capabilities.formats == nullptr || capabilities.formatCount == 0) {
            return std::unexpected(GpuError::surface_creation_failed);
        }
        // Output structs free their members in the generated destructor
        // (FreeMembers is private by design); just consume formats[0].
        surface_format_ = capabilities.formats[0];

        if (!create_pipeline()) {
            return std::unexpected(GpuError::backend_init_failed);
        }

        app_->set_frame_callback([this] { render_frame(); });
        registered_ = true;
        return {};
    }

    void set_paint_callback(std::function<void(Painter&)> callback) {
        paint_callback_ = std::move(callback);
    }

    // Uploads straight-alpha RGBA8 pixel data as a new texture owned by the
    // renderer. Returns an invalid handle when the input is rejected (empty
    // size, null data, wrong byte count) or when Dawn rejects the upload
    // (detected through the uncaptured-error flag, which fires synchronously
    // inside WriteTexture validation).
    Texture create_texture(const Size size, const std::uint8_t* rgba,
                           const std::size_t byte_count) {
        if (size.width <= 0 || size.height <= 0 || rgba == nullptr) {
            return Texture{};
        }
        const std::size_t needed =
            static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * 4;
        if (byte_count != needed) {
            return Texture{};
        }
        if (!device_ || !queue_) {
            return Texture{};
        }

        const std::uint64_t id = next_texture_id_++;
        wgpu::TextureDescriptor desc{};
        desc.label = "continui-texture";
        desc.size = {static_cast<std::uint32_t>(size.width),
                     static_cast<std::uint32_t>(size.height), 1};
        desc.format = wgpu::TextureFormat::RGBA8Unorm;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        wgpu::Texture texture = device_.CreateTexture(&desc);
        if (!texture || error_seen_) {
            return Texture{};  // never pass a null texture to WriteTexture
        }

        // WebGPU requires bytesPerRow to be a multiple of 256 for copies,
        // but callers hand over tightly packed rows (stride = width * 4).
        // Stage a padded copy whenever the tight stride is not aligned.
        const std::uint32_t row_bytes = static_cast<std::uint32_t>(size.width) * 4;
        const std::uint32_t aligned_rows = (row_bytes + 255u) & ~255u;
        const std::uint8_t* upload = rgba;
        std::vector<std::uint8_t> staged;
        if (aligned_rows != row_bytes) {
            staged.resize(static_cast<std::size_t>(aligned_rows)
                          * static_cast<std::size_t>(size.height));
            for (int y = 0; y < size.height; ++y) {
                std::memcpy(staged.data()
                                + static_cast<std::size_t>(y) * aligned_rows,
                            rgba + static_cast<std::size_t>(y) * row_bytes,
                            row_bytes);
            }
            upload = staged.data();
        }

        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = texture;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = aligned_rows;
        const wgpu::Extent3D extent{static_cast<std::uint32_t>(size.width),
                                    static_cast<std::uint32_t>(size.height), 1};
        const bool had_error = error_seen_;
        queue_.WriteTexture(&destination, upload,
                            staged.empty() ? byte_count : staged.size(), &layout,
                            &extent);
        if (error_seen_ != had_error) {
            return Texture{};  // the failed texture object dies here
        }

        wgpu::TextureView view = texture.CreateView();
        wgpu::BindGroup bind_group = make_texture_bind_group(view);
        textures_.emplace(id, TextureEntry{std::move(texture), std::move(view),
                                           std::move(bind_group), size});
        return Texture{id, size};
    }

    // Creates the atlas texture on first glyph, and a NEW one whenever the
    // strike cache grows/rebuilds. create_texture() uploads the whole CPU
    // atlas immediately, so the rebuild's full dirty rect is consumed here.
    // Old ids stay registered (renderer-lifetime textures), so quads
    // recorded earlier in this frame still sample valid pixels.
    void create_atlas_texture() {
        const GlyphAtlas& atlas = glyph_cache_.atlas();
        const Size size{atlas.width(), atlas.height()};
        const std::size_t bytes = static_cast<std::size_t>(size.width)
                                  * static_cast<std::size_t>(size.height) * 4;
        const Texture texture = create_texture(size, atlas.pixels(), bytes);
        atlas_id_ = texture.id_;
        atlas_generation_ = glyph_cache_.generation();
        if (atlas_id_ != 0) {
            (void)glyph_cache_.atlas().take_dirty();
        }
    }

    // Pushes newly packed glyph pixels (union dirty rect) into the existing
    // atlas texture before the frame is encoded. Growth never reaches here
    // as a partial update: that path creates a fresh texture in full.
    void sync_atlas() {
        if (atlas_id_ == 0) {
            return;
        }
        const Rect dirty = glyph_cache_.atlas().take_dirty();
        if (dirty.empty()) {
            return;
        }
        const auto it = textures_.find(atlas_id_);
        if (it == textures_.end()) {
            return;  // no texture: the uncaptured-error flag explains why
        }
        const int atlas_w = glyph_cache_.atlas().width();
        const int atlas_h = glyph_cache_.atlas().height();
        const int x = std::clamp(static_cast<int>(std::floor(dirty.x)), 0, atlas_w - 1);
        const int y = std::clamp(static_cast<int>(std::floor(dirty.y)), 0, atlas_h - 1);
        const int w = std::min(static_cast<int>(std::ceil(dirty.right())) - x,
                               atlas_w - x);
        const int h = std::min(static_cast<int>(std::ceil(dirty.bottom())) - y,
                               atlas_h - y);
        if (w <= 0 || h <= 0) {
            return;
        }

        const std::uint32_t row_bytes = static_cast<std::uint32_t>(w) * 4;
        const std::uint32_t aligned_rows = (row_bytes + 255u) & ~255u;
        std::vector<std::uint8_t> staged(
            static_cast<std::size_t>(aligned_rows) * static_cast<std::size_t>(h));
        const std::uint8_t* source = glyph_cache_.atlas().pixels();
        const std::size_t atlas_stride = static_cast<std::size_t>(atlas_w) * 4;
        for (int row = 0; row < h; ++row) {
            std::memcpy(staged.data() + static_cast<std::size_t>(row) * aligned_rows,
                        source + static_cast<std::size_t>(y + row) * atlas_stride
                                 + static_cast<std::size_t>(x) * 4,
                        row_bytes);
        }

        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = it->second.texture;
        destination.origin = {static_cast<std::uint32_t>(x),
                              static_cast<std::uint32_t>(y), 0};
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = aligned_rows;
        const wgpu::Extent3D extent{static_cast<std::uint32_t>(w),
                                     static_cast<std::uint32_t>(h), 1};
        queue_.WriteTexture(&destination, staged.data(), staged.size(), &layout,
                            &extent);
        // Validation failures surface through had_uncaptured_errors(), the
        // same programmatic signal the backend tests assert on.
    }

    // One presented frame. Steps that cannot proceed (moved-from, window
    // gone, minimized to zero area) leave the frame skipped, not an error:
    // a dropped frame is recoverable, a thrown exception is not. Paint
    // callback exceptions propagate to the run_one_frame() caller (ADR-019
    // boundary, same as event callbacks).
    void render_frame() {
        if (!device_ || !surface_) {
            return;
        }
        if (!reconfigure_if_resized()) {
            return;
        }

        const Size viewport{configured_width_, configured_height_};
        batcher_.begin_frame(viewport);
        if (paint_callback_) {
            Painter painter{batcher_, this};
            paint_callback_(painter);
        }
        batcher_.finish_frame();
        // Glyphs packed during the paint callback need their pixels on the
        // GPU before the draw encodes; full uploads happened at creation.
        sync_atlas();

        const auto& vertices = batcher_.vertices();
        const auto& indices = batcher_.indices();
        const auto& batches = batcher_.batches();

        // Uniform: the pixel-to-NDC half extents (16 bytes, vec2f padded).
        const float half_size[4]{viewport.width * 0.5f, viewport.height * 0.5f,
                                 0.0f, 0.0f};
        queue_.WriteBuffer(screen_buffer_, 0, half_size, sizeof(half_size));

        // Upload the frame's geometry; persistent buffers regrow on demand.
        const std::size_t vbytes = vertices.size() * sizeof(BatchVertex);
        const std::size_t ibytes = indices.size() * sizeof(std::uint16_t);
        if (!ensure_buffers(vbytes, ibytes)) {
            return;
        }
        if (vbytes > 0) {
            queue_.WriteBuffer(vertex_buffer_, 0, vertices.data(), vbytes);
        }
        if (ibytes > 0) {
            queue_.WriteBuffer(index_buffer_, 0, indices.data(), ibytes);
        }

        wgpu::SurfaceTexture surface_texture{};
        surface_.GetCurrentTexture(&surface_texture);
        // Suboptimal still presents; only real failures skip the frame.
        if (surface_texture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
            && surface_texture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
            return;
        }

        wgpu::RenderPassColorAttachment attachment{};
        attachment.view = surface_texture.texture.CreateView();
        attachment.loadOp = wgpu::LoadOp::Clear;
        attachment.storeOp = wgpu::StoreOp::Store;
        attachment.clearValue = wgpu::Color{0.09f, 0.09f, 0.13f, 1.0f};

        wgpu::RenderPassDescriptor pass_desc{};
        pass_desc.colorAttachmentCount = 1;
        pass_desc.colorAttachments = &attachment;

        wgpu::CommandEncoder encoder = device_.CreateCommandEncoder();
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&pass_desc);
        pass.SetBindGroup(0, screen_bind_group_);
        BatchKind pipeline_kind = BatchKind::shape;
        bool pipeline_set = false;
        // Bind geometry only when there is geometry: a bufferless frame
        // (no paint callback, nothing recorded) must stay a plain clear.
        if (!indices.empty()) {
            pass.SetVertexBuffer(0, vertex_buffer_);
            pass.SetIndexBuffer(index_buffer_, wgpu::IndexFormat::Uint16);
        }
        for (const Batch& batch : batches) {
            if (!pipeline_set || batch.kind != pipeline_kind) {
                pass.SetPipeline(batch.kind == BatchKind::text ? text_pipeline_
                                                               : pipeline_);
                pipeline_kind = batch.kind;
                pipeline_set = true;
            }
            // Batcher scissors are viewport-clamped; integer-floored here so
            // the scissor rect covers the same pixels the SDF would fade in.
            const std::uint32_t sx = static_cast<std::uint32_t>(std::floor(batch.scissor.x));
            const std::uint32_t sy = static_cast<std::uint32_t>(std::floor(batch.scissor.y));
            const std::uint32_t sw =
                static_cast<std::uint32_t>(std::ceil(batch.scissor.right())) - sx;
            const std::uint32_t sh =
                static_cast<std::uint32_t>(std::ceil(batch.scissor.bottom())) - sy;
            pass.SetScissorRect(sx, sy, sw, sh);
            pass.SetBindGroup(1, bind_group_for(batch.texture_id));
            // The stored indices are frame-absolute vertex ids, so baseVertex
            // must stay 0: passing vertex_offset would shift every fetched
            // index again (applied by the device at execution, and silent —
            // out-of-range vertex fetch is hardware-defined, not validated).
            pass.DrawIndexed(batch.index_count, 1, batch.index_offset, 0, 0);
        }
        pass.End();
        wgpu::CommandBuffer commands = encoder.Finish();
        queue_.Submit(1, &commands);
        surface_.Present();
    }

private:
    bool request_adapter_and_device() {
        wgpu::Adapter adapter = nullptr;
        wgpu::RequestAdapterOptions options{};
        auto on_adapter = [](wgpu::RequestAdapterStatus status, wgpu::Adapter received,
                             wgpu::StringView message, void* userdata) {
            if (status == wgpu::RequestAdapterStatus::Success && received) {
                *static_cast<wgpu::Adapter*>(userdata) = received;
            } else if (status != wgpu::RequestAdapterStatus::Success) {
                debug_log("continui: adapter request failed: ", message);
            }
        };
        instance_.WaitAny(instance_.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
                                                   on_adapter, &adapter),
                          UINT64_MAX);
        if (!adapter) {
            return false;
        }
        adapter_ = std::move(adapter);

        wgpu::Device device = nullptr;
        wgpu::DeviceDescriptor device_desc{};
        device_desc.label = "continui-device";
        // Dawn returns non-null error objects for failed creation calls (the
        // wrapper's bool conversion is only a null check) and silently drops
        // uncaptured errors without a callback — WGSL or pipeline validation
        // failures would otherwise surface as success. The uncaptured-error
        // callback fires synchronously on the calling thread; create_pipeline
        // checks the flag right after each creation call. Device loss is
        // logged only; recovery arrives with the render system.
        device_desc.SetUncapturedErrorCallback(on_uncaptured_error, this);
        device_desc.SetDeviceLostCallback(
            wgpu::CallbackMode::AllowSpontaneous,
            [](const wgpu::Device&, wgpu::DeviceLostReason, wgpu::StringView message) {
                debug_log("continui: device lost: ", message);
            });
        auto on_device = [](wgpu::RequestDeviceStatus status, wgpu::Device received,
                            wgpu::StringView message, void* userdata) {
            if (status == wgpu::RequestDeviceStatus::Success && received) {
                *static_cast<wgpu::Device*>(userdata) = received;
            } else if (status != wgpu::RequestDeviceStatus::Success) {
                debug_log("continui: device request failed: ", message);
            }
        };
        // WaitAny exists only on the Instance; the device request future
        // originates from the adapter but is waited on through the instance.
        instance_.WaitAny(adapter_.RequestDevice(&device_desc, wgpu::CallbackMode::WaitAnyOnly,
                                                 on_device, &device),
                          UINT64_MAX);
        if (!device) {
            return false;
        }
        device_ = std::move(device);
        queue_ = device_.GetQueue();
        return queue_ != nullptr;
    }

    // One pipeline for every Phase 0.4 shape: SDF alpha + texture + premul
    // blending. Group 0 carries the screen uniform (vertex stage), group 1
    // the sampler+texture pair (fragment stage, rebound per batch).
    bool create_pipeline() {
        wgpu::ShaderSourceWGSL shader_source{};
        shader_source.code = kQuadShader;
        wgpu::ShaderModuleDescriptor shader_desc{};
        shader_desc.nextInChain = &shader_source;
        wgpu::ShaderModule module = device_.CreateShaderModule(&shader_desc);
        if (!module || error_seen_) {
            return false;
        }

        wgpu::BindGroupLayoutEntry screen_entry{};
        screen_entry.binding = 0;
        screen_entry.visibility = wgpu::ShaderStage::Vertex;
        screen_entry.buffer.type = wgpu::BufferBindingType::Uniform;

        wgpu::BindGroupLayoutDescriptor screen_bgl_desc{};
        screen_bgl_desc.label = "continui-screen-bgl";
        screen_bgl_desc.entryCount = 1;
        screen_bgl_desc.entries = &screen_entry;
        wgpu::BindGroupLayout screen_bgl = device_.CreateBindGroupLayout(&screen_bgl_desc);

        wgpu::BindGroupLayoutEntry texture_entries[2]{};
        texture_entries[0].binding = 0;
        texture_entries[0].visibility = wgpu::ShaderStage::Fragment;
        texture_entries[0].sampler.type = wgpu::SamplerBindingType::Filtering;
        texture_entries[1].binding = 1;
        texture_entries[1].visibility = wgpu::ShaderStage::Fragment;
        texture_entries[1].texture.sampleType = wgpu::TextureSampleType::Float;
        texture_entries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;

        wgpu::BindGroupLayoutDescriptor texture_bgl_desc{};
        texture_bgl_desc.label = "continui-texture-bgl";
        texture_bgl_desc.entryCount = 2;
        texture_bgl_desc.entries = texture_entries;
        texture_bgl_ = device_.CreateBindGroupLayout(&texture_bgl_desc);

        wgpu::BindGroupLayout layouts[2]{screen_bgl, texture_bgl_};
        wgpu::PipelineLayoutDescriptor layout_desc{};
        layout_desc.bindGroupLayoutCount = 2;
        layout_desc.bindGroupLayouts = layouts;
        wgpu::PipelineLayout layout = device_.CreatePipelineLayout(&layout_desc);

        wgpu::VertexBufferLayout vertex_layout{};
        vertex_layout.arrayStride = sizeof(BatchVertex);
        vertex_layout.stepMode = wgpu::VertexStepMode::Vertex;
        wgpu::VertexAttribute attributes[4]{};
        constexpr wgpu::VertexFormat kFormats[4]{
            wgpu::VertexFormat::Float32x4, wgpu::VertexFormat::Float32x4,
            wgpu::VertexFormat::Float32x4, wgpu::VertexFormat::Float32x4};
        constexpr std::uint64_t kOffsets[4]{0, 16, 32, 48};
        for (int i = 0; i < 4; ++i) {
            // Field-by-field: braced assignment would match nextInChain
            // first and fail to compile.
            attributes[i].format = kFormats[i];
            attributes[i].offset = kOffsets[i];
            attributes[i].shaderLocation = static_cast<std::uint32_t>(i);
        }
        vertex_layout.attributeCount = 4;
        vertex_layout.attributes = attributes;

        // Premultiplied alpha over: out = src + dst * (1 - src.a), with the
        // same weights on the alpha channel.
        wgpu::BlendState blend{};
        blend.color.srcFactor = wgpu::BlendFactor::One;
        blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        blend.color.operation = wgpu::BlendOperation::Add;
        blend.alpha.srcFactor = wgpu::BlendFactor::One;
        blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        blend.alpha.operation = wgpu::BlendOperation::Add;

        wgpu::ColorTargetState target{};
        target.format = surface_format_;
        target.blend = &blend;
        target.writeMask = wgpu::ColorWriteMask::All;

        wgpu::FragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = "fs";
        fragment.targetCount = 1;
        fragment.targets = &target;

        wgpu::RenderPipelineDescriptor pipeline_desc{};
        pipeline_desc.label = "continui-batched-quads";
        pipeline_desc.nextInChain = nullptr;
        pipeline_desc.layout = layout;
        pipeline_desc.vertex.module = module;
        pipeline_desc.vertex.entryPoint = "vs";
        pipeline_desc.vertex.bufferCount = 1;
        pipeline_desc.vertex.buffers = &vertex_layout;
        pipeline_desc.fragment = &fragment;

        if (!screen_bgl || error_seen_) {
            return false;
        }
        pipeline_ = device_.CreateRenderPipeline(&pipeline_desc);
        if (!pipeline_ || error_seen_) {
            return false;
        }

        // Phase 0.5 text shares the vertex layout, bind groups, blending
        // and blend state with shapes — only the fragment entry differs
        // (white-mask coverage + gamma, no SDF). One extra pipeline, no
        // extra vertex format and no shader permutation explosion.
        fragment.entryPoint = "fs_text";
        pipeline_desc.label = "continui-glyph-atlas";
        pipeline_desc.fragment = &fragment;
        text_pipeline_ = device_.CreateRenderPipeline(&pipeline_desc);
        if (!text_pipeline_ || error_seen_) {
            return false;
        }

        if (!create_presentation_resources(std::move(screen_bgl))) {
            return false;
        }
        return true;
    }

    bool create_presentation_resources(wgpu::BindGroupLayout screen_bgl) {
        wgpu::BufferDescriptor buffer_desc{};
        buffer_desc.label = "continui-screen-uniform";
        buffer_desc.size = 16;  // vec2f half_size + padding
        buffer_desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Uniform;
        screen_buffer_ = device_.CreateBuffer(&buffer_desc);
        if (!screen_buffer_ || error_seen_) {
            return false;
        }

        wgpu::BindGroupEntry screen_entry{};
        screen_entry.binding = 0;
        screen_entry.buffer = screen_buffer_;
        screen_entry.size = 16;
        wgpu::BindGroupDescriptor screen_bg_desc{};
        screen_bg_desc.layout = screen_bgl;
        screen_bg_desc.entryCount = 1;
        screen_bg_desc.entries = &screen_entry;
        screen_bind_group_ = device_.CreateBindGroup(&screen_bg_desc);
        if (!screen_bind_group_ || error_seen_) {
            return false;
        }

        wgpu::SamplerDescriptor sampler_desc{};
        sampler_desc.label = "continui-sampler";
        sampler_desc.magFilter = wgpu::FilterMode::Linear;
        sampler_desc.minFilter = wgpu::FilterMode::Linear;
        sampler_desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        sampler_desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        sampler_ = device_.CreateSampler(&sampler_desc);
        if (!sampler_ || error_seen_) {
            return false;
        }

        // Solid rects sample one white texel; texture id 0 maps to it.
        constexpr std::uint8_t white_pixel[4] = {255, 255, 255, 255};
        wgpu::TextureDescriptor white_desc{};
        white_desc.label = "continui-white-texture";
        white_desc.size = {1, 1, 1};
        white_desc.format = wgpu::TextureFormat::RGBA8Unorm;
        white_desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        wgpu::Texture white = device_.CreateTexture(&white_desc);
        if (!white || error_seen_) {
            return false;
        }
        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = white;
        wgpu::TexelCopyBufferLayout layout{};
        const wgpu::Extent3D extent{1, 1, 1};
        queue_.WriteTexture(&destination, white_pixel, sizeof(white_pixel),
                            &layout, &extent);
        if (error_seen_) {
            return false;
        }
        white_view_ = white.CreateView();
        white_bind_group_ = make_texture_bind_group(white_view_);
        if (!white_view_ || !white_bind_group_ || error_seen_) {
            return false;
        }
        return true;
    }

    wgpu::BindGroup make_texture_bind_group(const wgpu::TextureView& view) {
        wgpu::BindGroupEntry entries[2]{};
        entries[0].binding = 0;
        entries[0].sampler = sampler_;
        entries[1].binding = 1;
        entries[1].textureView = view;
        wgpu::BindGroupDescriptor desc{};
        desc.layout = texture_bgl_;
        desc.entryCount = 2;
        desc.entries = entries;
        return device_.CreateBindGroup(&desc);
    }

    // (Re)creates the frame buffers when the frame outgrows them. Capacity
    // doubles so a growing phase 0.4 workload re-creates logarithmically
    // often; the buffers are never shared mid-frame, so replacement is safe.
    bool ensure_buffers(const std::uint64_t vbytes, const std::uint64_t ibytes) {
        if (vbytes > vertex_capacity_) {
            const std::uint64_t capacity =
                std::max<std::uint64_t>(vbytes, vertex_capacity_ * 2);
            wgpu::BufferDescriptor desc{};
            desc.label = "continui-vertex-buffer";
            desc.size = capacity;
            desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex;
            vertex_buffer_ = device_.CreateBuffer(&desc);
            vertex_capacity_ = vertex_buffer_ ? capacity : 0;
            if (!vertex_buffer_ || error_seen_) {
                return false;
            }
        }
        if (ibytes > index_capacity_) {
            const std::uint64_t capacity =
                std::max<std::uint64_t>(ibytes, index_capacity_ * 2);
            wgpu::BufferDescriptor desc{};
            desc.label = "continui-index-buffer";
            desc.size = capacity;
            desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index;
            index_buffer_ = device_.CreateBuffer(&desc);
            index_capacity_ = index_buffer_ ? capacity : 0;
            if (!index_buffer_ || error_seen_) {
                return false;
            }
        }
        return true;
    }

    wgpu::BindGroup bind_group_for(const std::uint64_t texture_id) const {
        if (texture_id == 0) {
            return white_bind_group_;
        }
        const auto it = textures_.find(texture_id);
        // Unregistered ids cannot come out of the batcher; the white texture
        // is the defensive default that keeps a frame presentable.
        return it != textures_.end() ? it->second.bind_group : white_bind_group_;
    }

    // Phase 0.3 interim: the client size is polled every frame; the
    // invalidation-driven resize path arrives with the render system.
    // Known limitations (harmless while one window per app): a destroyed
    // window's HWND value could in theory be recycled, and a surface that
    // goes stale without a size change (display-mode change) is not
    // detected here — both are revisited with the multi-window/DPI work.
    // Returns false when there is no drawable area (moved-from, window
    // destroyed, minimized to zero). A zero-sized Configure is not legal.
    bool reconfigure_if_resized() {
        if (hwnd_ == nullptr) {
            return false;
        }
        RECT client{};
        if (!GetClientRect(static_cast<HWND>(hwnd_), &client)) {
            return false;  // window destroyed since last frame
        }
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width <= 0 || height <= 0) {
            return false;  // minimized; nothing drawable this frame
        }
        if (width == configured_width_ && height == configured_height_) {
            return true;
        }
        wgpu::SurfaceConfiguration config{};
        config.device = device_;
        config.format = surface_format_;
        config.usage = wgpu::TextureUsage::RenderAttachment;
        config.width = static_cast<std::uint32_t>(width);
        config.height = static_cast<std::uint32_t>(height);
        config.alphaMode = wgpu::CompositeAlphaMode::Auto;
        config.presentMode = wgpu::PresentMode::Fifo;
        surface_.Configure(&config);
        configured_width_ = width;
        configured_height_ = height;
        return true;
    }

    struct TextureEntry {
        wgpu::Texture texture;
        wgpu::TextureView view;
        wgpu::BindGroup bind_group;
        Size size{};
    };

    App* app_ = nullptr;          // never null after successful initialize
    bool registered_ = false;     // frame callback currently registered with the App
    bool error_seen_ = false;     // any uncaptured GPU error since device creation
    void* hwnd_ = nullptr;        // Win32 client window (owned by the App)
    wgpu::Instance instance_;
    wgpu::Adapter adapter_;
    wgpu::Device device_;
    wgpu::Queue queue_;
    wgpu::Surface surface_;
    wgpu::RenderPipeline pipeline_;
    wgpu::RenderPipeline text_pipeline_;
    wgpu::TextureFormat surface_format_ = wgpu::TextureFormat::Undefined;
    int configured_width_ = 0;
    int configured_height_ = 0;

    // Frame resources. The batcher and the paint callback live here so the
    // per-frame data stays hot across frames.
    BatchBuilder batcher_;
    std::function<void(Painter&)> paint_callback_;

    // Phase 0.5 glyph cache: CPU strike map + atlas, owned here because the
    // renderer also owns the GPU atlas texture(s) and uploads their pixels.
    GlyphCache glyph_cache_;
    std::uint64_t atlas_id_ = 0;         // 0 until the first glyph is drawn
    std::uint64_t atlas_generation_ = 0; // generation backing atlas_id_

    // Presentation resources, created with the pipeline.
    wgpu::BindGroupLayout texture_bgl_;  // group 1, needed per bind group
    wgpu::BindGroup screen_bind_group_;
    wgpu::BindGroup white_bind_group_;
    wgpu::TextureView white_view_;
    wgpu::Sampler sampler_;
    wgpu::Buffer screen_buffer_;
    wgpu::Buffer vertex_buffer_;
    wgpu::Buffer index_buffer_;
    std::uint64_t vertex_capacity_ = 0;
    std::uint64_t index_capacity_ = 0;

    // All textures live until the renderer dies (Phase 0.4 lifetime rule);
    // explicit destruction arrives with the TextureCache.
    std::unordered_map<std::uint64_t, TextureEntry> textures_;
    std::uint64_t next_texture_id_ = 1;
};

// Out-of-line because it needs the complete DawnRendererImpl type. A second
// anonymous-namespace block is still the same namespace as the declaration.
namespace {

void on_uncaptured_error(const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message,
                         void* userdata) {
    static_cast<DawnRendererImpl*>(userdata)->note_uncaptured_error(message);
}

}  // namespace

}  // namespace continui::detail

namespace continui::dawn {

std::expected<Renderer, GpuError> Renderer::create(App& app, const Window& window) {
    auto impl = std::make_unique<detail::DawnRendererImpl>();
    auto initialized = impl->initialize(app, window);
    if (!initialized) {
        return std::unexpected(initialized.error());
    }

    Renderer renderer;
    renderer.impl_ = std::move(impl);
    return renderer;
}

Renderer::Renderer(Renderer&& other) noexcept = default;
Renderer& Renderer::operator=(Renderer&& other) noexcept = default;
Renderer::~Renderer() = default;

void Renderer::render_frame() {
    if (impl_ != nullptr) {
        impl_->render_frame();
    }
}

void Renderer::set_paint_callback(std::function<void(Painter&)> callback) {
    if (impl_ != nullptr) {
        impl_->set_paint_callback(std::move(callback));
    }
}

Texture Renderer::create_texture(const Size size, const std::uint8_t* rgba,
                                 const std::size_t byte_count) {
    if (impl_ != nullptr) {
        return impl_->create_texture(size, rgba, byte_count);
    }
    return Texture{};
}

bool Renderer::had_uncaptured_errors() const {
    return impl_ != nullptr && impl_->had_uncaptured_errors();
}

}  // namespace continui::dawn
