# 05 — render：绘制批、Painter、UI 编排、文本 quad

## 职责边界

render 把"绘制意图"变成 GPU 可消费的 CPU 数据：`Painter` 是公共薄封装，`BatchBuilder` 生成顶点/索引/批次，`UI` 把 `LayoutResult` 变成一系列 Painter 调用并管理命中回调。render 不调用 Win32、不创建 Dawn 对象；它输出的是纯数据结构，由 backend 消费。

## 公共 API 地图

- `Painter::rect/rounded_rect/image/clip/text`（`include/continui/painter.hpp`）：只在 paint 回调内有效
- `Paint{Color}`：非预乘颜色
- `UI`（`include/continui/ui.hpp`）：`ViewFn`、`invalidate`、`set_font`、`set_text_measurer`、`paint(painter, viewport)`、`handle_event`、`layout`、`pending`
- `detail::BatchBuilder`/`detail::TextRasterContext`：内部类型，经私有 include 路径给 backend/UI 使用

## 内部实现

### BatchVertex 与 Batch（`src/render/batcher.hpp:23–48`）

64 字节 = 16 float：`pos_uv(x,y,u,v)`、`tint(r,g,b,a)`（预乘）、`rect(cx,cy,hw,hh)`、`radius(tl,tr,br,bl)`。文本 quad 把 `radius_tl` 复用为 gamma 对比度指数（同一布局，不同 fs 解释）。

`Batch{texture_id, scissor, kind, vertex_offset, index_offset, index_count}`：
- 合批键 = `(texture, scissor, kind)`；kind ∈ {shape, text} → 两种 WGSL 片元入口
- **索引是帧级绝对顶点 id**（0.4 审计 F1：`DrawIndexed` 的 baseVertex 必须为 0）

### BatchBuilder 流程（`src/render/batcher.cpp`）

- `begin_frame` `:31`：清空缓冲、clip=视口、reset kind
- `rect/rounded_rect` `:43/:47` → `push_quad` `:112`：空矩形/裁剪外剔除、uint16 上限（16384 quads）**静默丢弃**；SDF 参数钳制；1px 外扩给 SDF 淡出；UV 按未扩 bounds 归一化
- `image` `:52` → `push_quad`（texture_id != 0）
- `text` `:59`：遍历 `GlyphRun`，按每个 glyph 的 pen 位置算 x 次像素分桶（`:11`），向 `TextRasterContext::glyph()` 要 slot；用 **slot.width_px/height_px** 作 quad 尺寸、slot.uv 作纹理坐标（0.5 审计 CRITICAL：曾经错用归一化 UV 当像素尺寸，导致 0.03px quad 全部不可见）；增长导致 id 变化时重新读 atlas id
- `push_text_quad` `:212`：无 SDF 外扩（atlas gutter 已防串色），顶点 tint 来自 `text_contrast()`（`:21`，Impeller 公式 `1 + luma*1.2`）
- `clip` `:96`：与视口求交，变化时 flush
- `open_batch` `:195` / `flush` `:265`：管理合批边界；`finish_frame` 必须调用，否则最后批次不可见

### UI 编排（`src/render/ui.cpp`）

`Impl` 持有：FrameArena、`ViewFn`、LayoutResult、`nodes[]`（本帧节点快照）、`callbacks_`（Key→std::function，跨帧）、`TextMeasurer`、`font`、`pending`。

- `evaluate()` `:55`：arena.reset() → callbacks/nodes 清空 → `build(arena)` 生成 View → 迭代前序遍历写 `ViewNode::index`、登记 button 回调 → `solve()` 得布局
- `paint(painter, viewport)` `:152`：非 none 失效 → evaluate → 逐 box 绘制：背景 `rounded_rect`，text/button 用 `FontFace::shape` 取 GlyphRun、按 content 区居中算 baseline → `Painter::text`
- `handle_event` `:164`：仅 `mouse_button_up + left` → `hit_test(上一帧 layout)` → 只接受 button 节点 → 调 `callbacks_[key]`（回调自己 invalidate）
- `FaceMeasurer` `:21`：用 `shape().width` + `metrics()` 实现 `TextMeasurer`，使求解器不依赖 text 模块

**事件顺序**：App 先泵事件（此时用上一帧 layout），再调用渲染器帧回调 → `UI::paint` 若失效则重建。回调里 `ui.invalidate()` 保证下一帧重建；UI 对象必须比其回调捕获的引用活得久（MSVC 下不能在 `UI ui([&]{ui.invalidate();})` 初始化中捕获自身，示例用 `UI* ui_ptr` 提前声明）。

**失效触发补充**（0.6 审计残留清理）：`paint` 比较传入 viewport 与缓存值，变化则 `invalidate(layout)`；`set_font`/`set_text_measurer` 同样触发 `invalidate(layout)`；按钮 Key 用 `callbacks.emplace` 登记，重复时输出 stderr 诊断并保留先登记者，不再静默覆盖。

### TextRasterContext（`src/render/text_context.hpp`）

窄接口：`glyph(face,size,bucket,id) -> GlyphSlot{uv(normalized),left,top,width_px,height_px}` 与 `atlas_texture()`。backend 实现它；render 只见接口。

## 不变量

1. 一个 quad = 4 顶点 + 6 索引 `{base,base+1,base+2,base,base+2,base+3}`；base 帧级绝对
2. 文本 quad 像素尺寸来自 `width_px/height_px`，**不得**从归一化 uv 推导
3. batcher 输出的 tint 始终预乘；backend 的 blend 假设 One/OneMinusSrcAlpha
4. UI 的 callback registry 只存 arena 之外的 std::function 副本；节点指针不出帧
5. 0.8 起按失效等级调度：`none` 不重算、`layout` 只重求解、`view` 重建+求解；`paint` 后 pending == none

## 错误/边界处理

空 View / 空 GlyphRun / 空 slot / 空 clip → 不产生几何；索引超 65536 静默丢弃（已知限制）；字体未设置 → text 节点不绘制但布局仍成立；点击落在非 button 节点 → 返回 false。

## 已知限制

- 0.8 用按 Key 值类型快照（实用版 Tree Diff）复用未变节点的绘制命令，但不做 GPU 脏矩形合成（架构 §7.4 允许整窗口重绘）；跨帧 retained ViewNode 树仍未实现
- 按钮/输入框的 hover/pressed/focus 由 LocalState 提供（`ui.cpp`、0.7）；聚焦反馈由库硬编码为最小亮蓝边框（无样式 API，遵循"先只做 HTML、样式留到后续层"的决策），用四条细矩形绘制，透明/不透明背景均可见；composition 预览期间 caret 移到预览串末尾；caret 支持任意字符位置与选区矩形
- uint16 索引上限 16384 quads/帧（0.4 已知限制，文本密集帧也要注意）
- `TextRasterContext::atlas_texture()` 每 glyph 查询一次（增长时换 id 的代价）

## 验证入口

`tests/render/batcher_test.cpp`（拓扑、合批、帧绝对索引、text quad 像素尺寸与 UV、gamma 指数）、`tests/render/ui_test.cpp`（命中→回调→失效→重绘闭环）、`examples/counter`（真实鼠标点击 42→41→42）。

## 溯源

0.4（batch/SDF/premul）、0.5（text quad 与 gamma，`kMaxGammaCorrection=1.2` 对应 FreeType；1.6 属 DirectWrite 后端未来值）、0.6（UI 编排，`cb7c152`）。审计发现见 `09-traceability.md`。
