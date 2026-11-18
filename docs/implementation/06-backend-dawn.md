# 06 — backend/dawn：管线、上传与帧提交

## 职责边界

`continui_backend_dawn` 把 render 的批次数据上传 GPU 并提交呈现。它是 opt-in 依赖（`CONTINUI_BUILD_BACKEND_DAWN`），不进入 umbrella；公共头 `backend_dawn.hpp` 不泄漏 Dawn 类型（pimpl `unique_ptr<DawnRendererImpl>`）。

## 公共 API 地图（`include/continui/backend_dawn.hpp`）

`dawn::Renderer::create(app, window) -> expected<Renderer, GpuError>`、`render_frame`、`set_paint_callback(std::function<void(Painter&)>)`、`create_texture(Size, rgba*, byte_count) -> Texture`、`had_uncaptured_errors()`。Renderer 必须在 App 之前销毁（dtor 先注销 frame callback）。

## 内部实现

### 初始化（`src/backend/dawn/dawn_renderer.cpp:551`）

1. `CreateInstance`（TimedWaitAny 特性）→ `RequestAdapter`（WaitAny）→ `RequestDevice`（设置 uncaptured-error / device-lost 回调）
2. `CreateSurface`（Win32 HWND 链）→ `GetCapabilities` → 取 formats[0]
3. `create_pipeline()` `:610`：一个 WGSL 模块，两个片元入口 `fs`（SDF 形状）与 `fs_text`（白 mask + gamma）；共享顶点布局（64B）、bind group layout、blend（One / OneMinusSrcAlpha，颜色+alpha 双通道）
4. `create_presentation_resources()` `:727`：screen uniform buffer、linear sampler、1×1 白纹理（texture id 0 的兜底）

### 每帧（`render_frame()` `:451`）

```
reconfigure_if_resized（client size 轮询，0.3 interim）
→ batcher.begin_frame(viewport)
→ set_paint_callback(Painter{batcher, this})   // this 实现 TextRasterContext
→ batcher.finish_frame()
→ sync_atlas()                                 // 上传本帧新字形像素
→ WriteBuffer(screen uniform + 顶点 + 索引)
→ GetCurrentTexture → BeginRenderPass(clear 0.09,0.09,0.13)
→ 每个 batch: SetPipeline(kind) / SetScissorRect(floor/ceil) / bind_group_for / DrawIndexed
→ Submit + Present
```

`DrawIndexed(count, 1, index_offset, **0**, 0)`：baseVertex 必须为 0（0.4 审计 F1：索引帧绝对，传 vertex_offset 会二次偏移，静默取越界顶点）。管线切换按 batch kind，只有变化时调用。

### 纹理与 atlas 上传

- `create_texture()` `:308`：RGBA8Unorm + TextureBinding|CopyDst；bytesPerRow 按 256 对齐，必要时 staging 复制；id 递增不复用；纹理随 renderer 生命周期
- `glyph()` `:221`：实现 TextRasterContext，`GlyphCache::glyph()` 拿 placement，必要时 `create_atlas_texture()`；UV 归一化由 atlas 尺寸除得，像素尺寸原样传回
- `create_atlas_texture()` `:379`：首字或增长后 create_texture 全量上传；旧纹理保留（旧 id 的 quad 仍有效）
- `sync_atlas()` `:395`：脏矩形并集 → 256B 对齐 staging 行 → `WriteTexture` 到 atlas 纹理的子矩形
- 增长前 flush：`GlyphCache` 的 `before_growth_flush_` 回调 = `[this]{ sync_atlas(); }`，保证旧纹理拿到本帧早先 glyph 的像素（0.5 审计 MAJOR）

### WGSL 要点（`:53` 附近）

- VS：`x/half_size - 1`，`1 - y/half_size`（像素 y-down → NDC y-up）
- `fs`：IQ rounded-box SDF + fwidth 抗锯齿 + straight→premul + tint 分量乘
- `fs_text`：`coverage = 1 - pow(1 - mask, contrast)`，`contrast` 从顶点 `radius_tl` 读（batcher 按 Rec.709 亮度算，1 + luma*1.2），输出 premul 白色 × premul tint

## 不变量

1. 索引帧绝对 → baseVertex 恒 0（回归测试钉死）
2. 纹理 id 不复用；增长产生的旧纹理保留到 renderer 析构
3. 任何 uncaptured GPU 错误 → `error_seen_` 置位且**不会清零**（诊断用，测试断言）
4. 重复回调必须无捕获（Dawn static_assert），状态经 userdata 传递
5. 缓冲区只在帧首替换，不与在飞帧共享

## 错误路径

设备/适配器请求失败 → `backend_init_failed`；WGSL/pipeline 创建后立即检查 `error_seen_`（Dawn 对失败对象返回非空但不是真失败）；create_texture 输入非法 → 无效 Texture；WriteTexture 校验错误 → uncaptured error 置位（测试用 `had_uncaptured_errors` 断言）。帧跳过（窗口销毁/最小化）不是错误。

## 已知限制

- client size 轮询（Phase 0.3 interim）：HWND 复用、display-mode 变化的 stale surface 未处理
- 单窗口；多窗口/DPI 待后续
- GPU surface 无 `COPY_SRC`；程序化像素验证走测试侧 `PrintWindow(PW_RENDERFULLCONTENT)` GDI 快照（见 `tests/backend/dawn_renderer_test.cpp`），不改 backend/公共 API
- uint16 索引上限 16384 quads/帧

## 验证入口

`tests/backend/dawn_renderer_test.cpp`：真实窗口 bring-up、纹理上传（含非法输入）、painted 帧 + 文本增长负载、真实 SetWindowPos resize、teardown、uncaptured-error 断言，以及 GDI 像素回读断言（纯色、跨批次、文字亮像素）。`examples/shapes/text/counter` 目视。

## 溯源

ADR-009（Dawn）、ADR-019（异常边界）。0.4 baseVertex 修复、0.5 纹理增长修复见 `09-traceability.md`。
