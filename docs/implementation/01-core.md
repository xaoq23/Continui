# 01 — core：契约、帧内内存与显式失效

## 职责边界

core 持有 UI 的**基础契约与生命周期原语**：App/Window 句柄、Event、Key、FrameArena、Invalidation、View 句柄、Channel 占位。它不计算布局、不解析字体、不调用 GPU、不做平台 API（平台实现在 `src/platform/windows/`，通过 pimpl 隔离）。

代码分布有点特殊：公共契约在 `include/continui/`，App/Window 的**实现**在 platform 模块（`platform_app.cpp:141` 起、`platform_window.cpp:99` 起）；`View` 的实现在 layout 模块（`src/layout/view_tree.cpp:10`）。这是有意的分层：core 定义类型，别的模块提供实现，避免 core 依赖任何下层。

## 公共 API 地图

| 头文件 | 类型 | 用途 |
|---|---|---|
| `app.hpp` | `App::create/create_window/run/run_one_frame/set_frame_callback/request_redraw/quit` | 平台运行时与单帧驱动；`run_one_frame()` 是嵌入外部循环的入口 |
| `window.hpp` | `WindowDesc/Window/WindowError` | 非拥有窗口句柄（可拷贝），`set_event_handler/show/hide/client_size` |
| `events.hpp` | `EventType/KeyCode/MouseButton/Point/Size/Event` | 扁平事件结构（sum type 尚未冻结），鼠标坐标在客户区像素 |
| `key.hpp` | `Key`（`uint64_t`） | 跨帧稳定节点身份，`std::hash` 特化已提供 |
| `arena.hpp` | `FrameArena` | 帧内 bump 分配 + 统一析构 + reset 重用 |
| `invalidation.hpp` | `Invalidation`/`merge_invalidation/at_least` | 显式失效等级与合并规则 |
| `view.hpp` | `View` + `Row/Column/Text/Button/Solid` | 声明式节点句柄与组合子（含显式 Key 重载） |
| `channel.hpp` | 空占位 | 跨线程业务数据入口仍在 Phase 1 实现 |

## 内部实现

### FrameArena（`include/continui/arena.hpp`）

- `create<T>(args...)`：在当前块按 `alignof(T)` 对齐后 placement-new；非平凡析构类型登记 `destroy<T>` 函数指针（`arena.hpp:33` 附近）
- `allocate_array<T>(n)`：仅用于平凡类型（ViewNode 的子指针数组）
- `reset()`：逆序调用登记的析构器 → 各块 `used=0` → 自动 Key 计数器归零（`arena.hpp:48` 附近）
- 块策略：首次至少 4 KiB；当前块放不下时**在 vector 末尾追加新块**并把 `current_` 指向它——绝不替换已占用块（0.6 审计 CRITICAL：替换会释放仍持有 ViewNode/析构登记的块）；reset 后块保留复用，稳定帧零系统分配
- 自动 Key：`next_auto_key()` 返回 `1 + auto_key_++`，每帧从 1 重新开始 → **位置相关**，只对结构不变的树稳定

**不变量**：任何 `View`/`ViewNode*` 只能在"同一帧"内使用；跨帧只允许 `Key` 和复制到 `UI::callbacks_` 的 `std::function`。

### Invalidation（`include/continui/invalidation.hpp:11`）

`none < paint < layout < view < full`，`merge_invalidation` 取数值较大者，满足架构 §3.3 的合并律。`UI::paint` 当前对任何非 none 等级都整树重算（0.6 简化，见 `05-render.md`）。

### View 组合子（`src/layout/view_tree.cpp:10`）

`make()` 在 arena 里构造 `ViewNode`：类型、Key、Style、文本/回调、子指针数组。子节点指针直接来自传入的 `View` 句柄，`ViewTreeAccess`（`src/layout/view_tree.hpp`）是唯一被授权把句柄转成内部节点的桥；公共 API 不暴露节点树。

## 所有权与生命周期

```
App (owns platform runtime)
 └─ owns WindowImpl[] (Window 只是句柄)
FrameArena (owned by UI::Impl)
 └─ ViewNode[]  ── 只在本帧有效
UI::Impl::callbacks_ (Key → std::function) ── 跨帧，从 arena 复制出来
```

`FontManager` 拥有 FT_Library + 全部 FT_Face；`UI` 只持有 `const FontFace*` 非拥有指针，用户必须保证它比 UI/渲染器活得久。

## 线程

全部主线程：`App`/`Window` 方法、事件投递、渲染器帧回调、UI 求值/绘制。Channel 占位意味着跨线程补丁入口尚未实现；后台线程不得触碰 UI 状态。

## 错误路径

- `App::create/create_window` 返回 `std::expected`（`AppError`/`WindowError`）
- 窗口事件处理器异常按 ADR-019 传播到 `run()/run_one_frame()` 调用方，事件泵不捕获
- Arena 分配失败由 `std::bad_alloc` 传播（无静默降级）

## 已知限制

- `Channel` 仍为空占位（Phase 1）
- `LocalState`（focus/hover/scroll/cursor）未实现
- 自动 Key 是位置序；列表重排需要显式 Key 重载。按钮回调层已检测重复 Key（stderr 诊断 + 保留先登记者），节点级重复唯一性强制留待 0.8（架构 §4.3 规则 3）
- `View` 树无 diff/reconcile：每帧重建（Phase 0.8）

## 验证入口

`tests/layout/layout_test.cpp::test_arena`（析构计数、块复用、Key 重置）；`examples/counter/main.cpp` 的七个步骤；`tests/render/ui_test.cpp`（回调跨帧不重入）。

## 溯源

ADR-005（arena 管理帧内临时对象，Accepted）、ADR-006（Key 寻址身份与局部状态，Accepted）、ADR-019（异常边界，Accepted）。0.6 提交 `cb7c152`。
