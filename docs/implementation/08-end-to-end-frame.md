# 08 — 一帧的完整数据流（拆黑盒用）

本文把架构 §3.2 的 11 个逻辑阶段映射到 0.6 的实际代码路径，以 `examples/counter/main.cpp` 为例。逻辑顺序不变，但 0.6 允许多阶段合并（例如 Evaluate/Reconcile/Resolve 一起在 `UI::Impl::evaluate()` 完成）。

## 全局图景

```
用户状态 State{count}
   │  （用户持有；框架不拥有）
   ▼
View = build(arena)  ──► ViewNode 树（arena 内，本帧有效）
                              │
                              ▼
                     LayoutResult（纯函数 solve）
                     boxes[] / Key / is_button
                              │
        ┌─────────────────────┴──────────────────────┐
        ▼                                            ▼
   UI::paint → Painter 命令                    UI::handle_event
   rect/rounded_rect/text                     hit_test(上一帧 boxes)
        │                                            │
        ▼                                            ▼
   BatchBuilder → Batch[]                      button 回调
   (texture, scissor, kind)                    改 State + UI::invalidate()
        │
        ▼
   DawnRendererImpl::render_frame
   WriteBuffer(uniform/vertex/index) + WriteTexture(atlas dirty)
   → DrawIndexed（baseVertex=0）→ Present
```

## 阶段映射（架构 §3.2 → 0.6 代码）

| # | 阶段 | 实际代码 |
|---|---|---|
| 1 | Poll（处理平台事件） | `App::run_one_frame()`（`platform_app.cpp:158`）→ `window_procedure`（`platform_window.cpp:99`）→ `Window::set_event_handler` 回调 |
| 2 | Consume（Channel 补丁） | **未实现**（Channel 占位，Phase 1） |
| 3 | Invalidate（合并失效） | `UI::invalidate()` / `merge_invalidation`（`invalidation.hpp:11`） |
| 4 | Evaluate（重算视图） | `UI::Impl::evaluate()`（`ui.cpp:55`）：arena.reset → `ViewFn` → ViewNode 树 |
| 5 | Reconcile（Key 匹配/Diff） | **未实现**（0.6 每帧全量重建；Phase 0.8） |
| 6 | Resolve（样式） | Style 直接存在 ViewNode 上，无独立解析阶段 |
| 7 | Layout | `solve()`（`layout_solver.cpp:266`） |
| 8 | Paint | `UI::Impl::paint()`（`ui.cpp:100`）→ `Painter` → `BatchBuilder` |
| 9 | Submit | `DawnRendererImpl::render_frame()`（`dawn_renderer.cpp:451`） |
| 10 | Present | `surface_.Present()`（同函数末尾） |
| 11 | Reclaim | `UI::Impl::evaluate()` 下一帧开头的 `arena.reset()` |

## Counter 例子的七步（逐行对照）

1. **用户持有状态**：`examples/counter/main.cpp` 的局部 `struct State { int count = 42; }`
2. **View 从状态生成**：`UI ui([&](FrameArena& arena){ ... })` 里 `Text(arena, std::to_string(state.count), ...)`、`Button(arena, "-", [&]{ --state.count; ui_ptr->invalidate(); }, ...)`
3. **布局**：`renderer->set_paint_callback` 里 `ui.paint(painter, client)` → `evaluate()` → `solve()` → `LayoutResult`；按钮盒子坐标由 Column/Row 的 padding/gap/align 决定
4. **绘制**：`UI::Impl::paint()` 对每个 box：背景色 → `painter.rounded_rect`，文字 → `FontFace::shape` → `painter.text`（每 glyph 一个 quad，同 atlas 合批）
5. **鼠标命中**：窗口过程翻译 `WM_LBUTTONUP` → `Event{mouse_button_up, left, position}` → `UI::handle_event()` → `hit_test` 用**上一帧** boxes（因为事件先于 paint 泵出）
6. **回调改状态 + 显式失效**：`--state.count; ui_ptr->invalidate()`；没有隐式依赖追踪
7. **下一帧更新**：下一次 `run_one_frame()` → paint 回调发现 `pending != none` → `evaluate()` 重建树 → `Text` 拿到新的 count → 新画面

实测：初始 42；客户区 (130,130) 点击减号 → 41；(350,130) 点击加号 → 42。

## 为什么事件必须用"上一帧"布局

`App::run_one_frame()` 先泵消息再调用 frame callback。若事件里直接查"本帧"布局，它还没算出来；用上一帧 boxes 是唯一一致的选择。代价：resize 后的第一帧点击可能落在旧几何上（0.6 已知限制，尚无输入重映射）。

## 一次典型的失效合并

```
点击减号 → invalidate(view)        // pending = view
同一帧再点击加号 → invalidate(view) // merge(view, view) = view
下一帧 paint → evaluate + solve + 绘制 → pending = none
```

多次失效不会累加执行次数；等级只取最强者（架构 §3.3）。

## 与 0.8 的差距（不要误以为已实现）

- 没有 Key 匹配/diff：即使只有 count 变化，整棵树也重建
- 没有布局脏区：所有节点重算几何
- 没有绘制脏区：renderer 每帧全屏 clear + 重画
- 没有键盘输入/焦点/IME：Counter 只能用鼠标；Escape 由事件处理器直接 `app->quit()`

## 调试入口

- 布局不对 → `tests/layout/layout_test.cpp`、打印 `UI::layout().boxes`
- 点击没反应 → 检查 `Event.position`（客户区像素）、`hit_test` 找不找得到 button box、`Key → callback` 是否登记（`ui.cpp:55` 的遍历）
- 数字变了但画面没变 → 检查回调是否调用了 `invalidate()`、`UI::pending()`
- 画面全黑 → 检查字体是否设置、Style 背景 alpha、Painter 是否被 renderer 调用

## 溯源

架构 §3（核心运行模型）、0.6 提交 `cb7c152`。审计待办见 `09-traceability.md`。
