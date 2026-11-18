// Dawn renderer implementation (ADR-009). Bring-up follows the canonical
// sequence from Dawn's docs/quickstart-cmake.md, verified against the pinned
// Dawn sources (tag v20261113.214538):
//   CreateInstance -> RequestAdapter (futures callback) -> RequestDevice ->
//   CreateSurface (SurfaceSourceWindowsHWND chain) -> GetCapabilities ->
//   Configure -> per-frame GetCurrentTexture / render / Present.
// API notes (why some names look unfamiliar):
//   - RequestAdapter/RequestDevice are futures-based; the synchronous path
//     is WaitAny(future, UINT64_MAX) with CallbackMode::WaitAnyOnly.
//   - The WGSL descriptor is ShaderSourceWGSL (renamed from the older
//     ShaderModuleWGSLDescriptor); it chains into ShaderModuleDescriptor.
//   - SwapChain is gone from the modern path; Surface + configuration
//     replaces it ("SwapChain resources" in development.md Phase 0.3).

#include "dawn_renderer.hpp"

#include <continui/platform_windows.hpp>

#include "dawn/webgpu_cpp.h"

// min/max macros would poison the Dawn headers and the std:: usage below.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <expected>
#include <memory>
#include <string>
#include <utility>

namespace continui::detail {
namespace {

// Interior triangle in clip space; vertex_index selects the position so the
// pipeline needs no buffers at all (minimal Phase 0.3 acceptance shape).
constexpr char kTriangleShader[] = R"(
    @vertex
    fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
        var pos = array<vec2f, 3>(vec2f(0.0, 0.5),
                                  vec2f(-0.5, -0.5),
                                  vec2f(0.5, -0.5));
        return vec4f(pos[i], 0.0, 1.0);
    }
    @fragment
    fn fs() -> @location(0) vec4f {
        return vec4f(0.16, 0.40, 0.86, 1.0);
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
class DawnRendererImpl {
public:
    DawnRendererImpl() = default;

    DawnRendererImpl(const DawnRendererImpl&) = delete;
    DawnRendererImpl& operator=(const DawnRendererImpl&) = delete;

    ~DawnRendererImpl() {
        if (registered_) {
            app_->set_frame_callback(nullptr);  // the callback captures this
        }
        // Release in reverse creation order; Unconfigure detaches the
        // surface from the swapchain state before the handles die.
        pipeline_ = nullptr;
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

    // One presented frame. Steps that cannot proceed (moved-from, window
    // gone, minimized to zero area) leave the frame skipped, not an error:
    // a dropped frame is recoverable, a thrown exception is not.
    void render_frame() {
        if (!device_ || !surface_) {
            return;
        }

        const bool configured = reconfigure_if_resized();
        if (!configured) {
            return;
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
        pass.SetPipeline(pipeline_);
        pass.Draw(3);  // three vertices from vertex_index, no vertex buffers
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

    bool create_pipeline() {
        wgpu::ShaderSourceWGSL shader_source{};
        shader_source.code = kTriangleShader;
        wgpu::ShaderModuleDescriptor shader_desc{};
        shader_desc.nextInChain = &shader_source;
        wgpu::ShaderModule module = device_.CreateShaderModule(&shader_desc);
        if (!module || error_seen_) {
            return false;
        }

        wgpu::ColorTargetState target{};
        target.format = surface_format_;

        wgpu::FragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = "fs";
        fragment.targetCount = 1;
        fragment.targets = &target;

        wgpu::RenderPipelineDescriptor pipeline_desc{};
        pipeline_desc.label = "continui-triangle";
        pipeline_desc.vertex.module = module;
        pipeline_desc.vertex.entryPoint = "vs";
        pipeline_desc.fragment = &fragment;

        pipeline_ = device_.CreateRenderPipeline(&pipeline_desc);
        return pipeline_ != nullptr && !error_seen_;
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
    wgpu::TextureFormat surface_format_ = wgpu::TextureFormat::Undefined;
    int configured_width_ = 0;
    int configured_height_ = 0;
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

}  // namespace continui::dawn
