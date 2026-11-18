# 09 — 可追溯性：决策、缺陷、审计与代码位置

本文记录"为什么代码长这样"以及"哪个问题在哪儿修的"。新增缺陷/决策时按文末模板追加，不要只改代码不留痕。

## 1. ADR 与决策状态

| ADR | 主题 | 状态 | 代码落点 |
|---|---|---|---|
| ADR-005 | Arena 管理帧内临时对象 | Accepted | `include/continui/arena.hpp`、`src/render/ui.cpp:55` |
| ADR-006 | Key 寻址控件身份和局部状态 | Accepted（局部状态仍未实现） | `key.hpp`、`ui.cpp` 的 `callbacks_` |
| ADR-009 | Dawn 作为首个图形后端 | Accepted | `src/backend/dawn/` |
| ADR-010 | HarfBuzz + FreeType | Accepted | `src/text/`、`src/render/text_context.hpp` |
| ADR-011 | Channel 作为跨线程数据入口 | Accepted（实现 Planned） | `channel.hpp` 仍是空占位 |
| ADR-013 | Flex 参考 Taffy | **Proposed（未采纳）** | 0.6 自写最小 Flex，无新依赖 |
| ADR-019 | Phase 0.2 异常边界 | Accepted | `platform_window.cpp:198`、事件泵不捕获 |
| ADR-020 | clang 工具链 | Accepted | `CMakePresets.json` |

**业主决策记录**：
- gamma 系数：0.5 采用 1.2（FreeType），未采用 ADR 中提到的 1.6（DirectWrite）；依据 Impeller 源码的语义区分，业主 2026-11-18 确认保持 1.2
- umbrella：0.4 漏加 `continui_render`，0.5 收尾时补上（`57c12a5`），业主确认
- 0.6 范围：按业主提供的里程碑建议，把 0.7 的按钮命中与 0.8 的显式失效提前成垂直切片；不冒充 0.7/0.8 验收

## 2. 审计发现 → 修复闭环

### Phase 0.3（首次 GPU bring-up 审计）

| 发现 | 修复 | 代码位置 |
|---|---|---|
| F1 静默 GPU 错误（无 uncaptured callback） | 安装 uncaptured-error + device-lost 回调，`error_seen_` 在每个资源创建点检查 | `dawn_renderer.cpp:162,210` |
| F2 回调被覆盖 | `registered_` 标志，析构时注销 | `dawn_renderer.cpp:179` 附近 |
| F7 平台依赖未声明 | `PRIVATE continui_platform` | `src/backend/dawn/CMakeLists.txt` |
| HWND 复用 / stale surface | 记为已知限制（多窗口/DPI 阶段处理） | `dawn_renderer.cpp:853` 附近注释 |

### Phase 0.4（渲染系统审计，FAIL → 修正后接受）

| 发现 | 证据 | 修复 | 代码位置 |
|---|---|---|---|
| **F1 [MAJOR] baseVertex 双重偏移**：批次 2..N 静默丢失（审计员截图证实棋盘格/条纹窗消失） | `DrawIndexed(..., vertex_offset, 0)` 第 4 参是 baseVertex；索引帧绝对 → 二次偏移；Dawn 不校验顶点越界 | 传 0；契约写进 Batch 文档；回归测试钉死跨批次索引 | `dawn_renderer.cpp:537`、`batcher.hpp:35`、`tests/render/batcher_test.cpp:125` 附近 |
| F2 [MINOR] 测试盲区（无像素观察） | 4/4 全绿但一半内容没画 | `had_uncaptured_errors()` + 程序化断言；索引回归测试 | `backend_dawn.hpp`、`dawn_renderer.cpp:985` |
| F3 [MINOR] 16384 quads 静默丢弃 | `vertices_.size()+4 > 65536` 直接 return | 明确记为已知限制（offsets 一致，不损坏） | `batcher.cpp:70` 附近注释 |
| F4 [MINOR] Texture friend 过宽 | BatchBuilder 从不访问 Texture 私有成员 | 删除 friend 与未用 include | `texture.hpp` |
| F5–F8 [INFO] SDF/gamma/白纹理兜底/私有 include | 无行动，文档化权衡 | 同各模块文档 | — |

**教训**：目视验收必须对照明确检查清单；"测试全绿 + 目视正常"不能替代实验。

### Phase 0.5（文本系统审计，FAIL → 修正 → 复核 PASS）

| 发现 | 证据 | 修复 | 代码位置 |
|---|---|---|---|
| **CRITICAL：text quad 尺寸用归一化 UV**（10px 字形在 256 图集里算成 0.039px）→ 全部文字不可见 | 诊断打印 `v5 pos=(40.03,31)`；审计员与我的独立截图一致 | `GlyphSlot` 增加 `width_px/height_px`；batcher 用像素字段；假 context CPU 测试钉死 10×14px quad | `text_context.hpp:26`、`batcher.cpp:88`、`tests/render/batcher_test.cpp:266` 附近 |
| **MAJOR：增长帧旧纹理脏像素未上传** | 本帧早先 quad 引用旧 id；旧图集对象被 grow 销毁，脏 rect 丢失 | `set_before_growth_flush` 回调；增长前 sync 到旧纹理 | `glyph_cache.cpp:79`、`dawn_renderer.cpp:172` |
| MINOR：临时诊断代码残留 | `[text-diag]` fprintf | 全部删除 | `dawn_renderer.cpp`（grep 无残留） |
| MINOR：测试无法抓 GPU 缺陷 | 全绿但无文字 | 新增 quad 尺寸 CPU 回归 + 增长测试 | 同上测试文件 |
| 复核残留 MINOR：单个 512px 字形触发 2 次增长 | 探针输出 generation=2 / 2 flushes；第二次把新尺寸写进旧纹理 → 校验错误 | `grow(w,h)` 一次到位（do-while 至少翻倍）；增长后复查 cell；512px 回归测试 | `glyph_cache.cpp:110`、`tests/text/text_test.cpp:180` 附近 |

最终复核结论：**PASS / Accept**（审计员独立实测 1 次增长 1 次 flush、截图确认全部文字正常）。

### Phase 0.6（UI 闭环审计）

**审计结论：FAIL（1 CRITICAL + 5 MINOR/INFO）→ 修复 → 复现程序验证通过。**

| 发现 | 证据 | 修复 | 代码位置 |
|---|---|---|---|
| **CRITICAL：FrameArena 原地替换仍被占用的块 → use-after-free** | `arena.hpp:117` 的 `blocks_[current_] = std::move(block)` 释放了本帧仍持有 ViewNode 和析构登记的块；审计员用 25 节点树在第 2/3 帧复现 0xC0000005；现有测试树太小（<4 KiB）从未覆盖 | 永不替换已占用块，改为在末尾追加新块并推进 `current_`；新增"400 个非平凡对象 × 4 帧"回归测试 | `arena.hpp:114` 附近、`tests/layout/layout_test.cpp::test_arena_growth_across_frames` |
| MINOR：重复显式 Key 静默覆盖回调 | `ui.cpp:79` 用 `operator[]`；架构 §4.3 要求重复 Key 必须检测 | **已修**（保留先登记者 + stderr 诊断） | `ui.cpp:78` 附近 |
| MINOR：resize 不触发 UI 失效 → 命中旧几何 | 事件处理器只处理 mouse_button_up | **已修**（`paint` 检测 viewport 变化 → `invalidate(layout)`） | `ui.cpp:152` |
| MINOR：`flex_basis` 在 Column 上仍用 width 的 min/max 钳制 | `layout_solver.cpp:148` | **已修**（按主轴选 min/max 字段） | `layout_solver.cpp:148` 附近 |
| MINOR：固有尺寸漏算子节点 margin；min/max 与 padding 重复钳制 | `layout_solver.cpp:54–99` | **已修**（margin 计入固有尺寸；min/max 只在 border-box 上钳一次） | `layout_solver.cpp:54–99` |
| INFO：`solve()` 写 `ViewNode::index`；hit_test 注释与实现不符；setter 不自动失效 | 代码审查 | 注释/文档已修；setter **已修**（`invalidate(layout)`）；`index` 帧内缓存保留 | `layout_solver.cpp:113`、`layout.hpp:54`、`ui.cpp:143` |

### A 阶段：审计残留 MINOR 清理（已修，2026-11-18）

| 项 | 改动 | 回归测试 |
|---|---|---|
| resize 失效 | `UI::paint` 比较传入 viewport 与缓存值；变化则 `invalidate(layout)` 并重算 | `ui_test.cpp`：小视口 200→500，断言根盒 200→300 |
| 重复 Key | `callbacks.emplace` 替代 `operator[]`；重复时输出 stderr 诊断并保留先登记者 | `ui_test.cpp`：两个 Key{9} 按钮，断言首个回调触发、第二个不触发 |
| Column flex_basis | 主轴为 Column 时用 `min_height/max_height` 钳制 | `layout_test.cpp`：basis=80 + min_height=120 + min_width=1000 → 120 |
| 固有尺寸 margin | `measure_children` 计入子节点 main/cross margin；`place_children` 的 `bases[i]`/`child_cross` 同样含 margin，使测量与摆放一致 | `layout_test.cpp`：margin 5/7 → 容器 10×14；margin 10 + 文字 16 → 子盒 16 @ x=5 |
| min/max 与 padding | 合并 `apply_style_size` 入 `measure`，auto 先加 padding 再钳一次 | `layout_test.cpp`：min_width=100 + padding 40 + 内容 16 → 100（原 140） |
| setter 失效 | `set_font`/`set_text_measurer` 调用 `invalidate(layout)` | `ui_test.cpp`：两次 setter 后断言 pending==layout |

验证：clang 7/7、MSVC 6/6，双工具链无编译警告；`ui_test` stderr 可见重复 Key 诊断。

**独立 review 处置**：fresh-context review 子代理确认 5 项均已验真——把新测试链接到旧版 `layout_solver.cpp`/`ui.cpp` 时，3 个布局断言与 4 个 UI 断言全部失败，排除了 pass-while-wrong；无 CRITICAL/MAJOR、无架构/所有权/确定性违规。发现 2 项：
- [MINOR] margin 修复不完整：`measure_children` 计入 margin，但 `place_children` 仍按无 margin 的 `bases[i]` 分配槽位并再次扣减 margin。复现（Row auto、文字 16×20、水平 margin 10）：旧代码子盒 6×20 @ x=5；首次修复后仍为 6×20，且容器多出 10px 尾部空白。**已接受并修正**：`bases[i]` 与 `child_cross` 均含 margin，测量与摆放使用同一基准；新增子盒 16 @ x=5 断言。
- [INFO] 重复 Key 诊断因 `evaluate()` 每帧重建 callbacks 而每帧刷屏。**已接受并修正**：`UI::Impl` 用跨帧 `unordered_set<Key>` 记录已警告 Key，每个 Key 只打印一次。

修正后重跑：clang 7/7、MSVC 6/6，无警告。**独立复核结论：Accept**——复核子代理用原复现程序确认子盒从 6×20 恢复为 16×20 @ x=5、容器 26×20 且无尾部空白；探针检查 flex grow/shrink、stretch/center、非对称 margin 均正确，Counter 布局未变；一次性诊断经 3 帧 × 2 个重复 Key 验证仅输出 2 行。残留 2 个 INFO：flex-shrink 权重含 margin（~0.2px 有界偏差，确定性，非缺陷）；`platform_window_test` 全套件下有偶发失败（未触及模块，pre-existing flakiness，单独运行 3/3 通过）。

**修复后验证**：
- `cmake --build --preset clang` + `ctest --preset clang` 7/7；MSVC 6/6
- 审计员原复现程序（`%TEMP%/opencode/arena_tree.cpp`，25 节点 × 3 帧）重编译运行：`frame 0/1/2: boxes=25 ... ok=1`，退出码 0（修复前第 2 帧崩溃）
- 审计建议的替代验证（复现程序 + 新回归测试）已满足；若后续要求正式复核，在此补录

**其他验证为干净**：公共头无第三方泄漏、`ui.hpp` 独立包含可编译、模块边界无环、0.4/0.5 无回退；Counter 三次独立截图均确认 42→41→42。

### C 阶段：GPU 像素回读测试（已实现，2026-11-18）

- **目标**：把 0.4（baseVertex 双重偏移）与 0.5（归一化 UV 当 quad 尺寸 → 0.03px 不可见文字）两类"测试全绿但画面错"的静默缺陷变成程序化断言。
- **方案**：不动 backend/公共 API，仅在 `tests/backend/dawn_renderer_test.cpp` 用 Win32 GDI `PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT)` 把呈现后的窗口快照拷进内存 DC，`GetPixel` 读客户区像素。实验确认 `GetDC(hwnd)` 读到的是 DWM 背景 `(30,30,40)`，不可用。
- **断言**：全屏红色中心像素；两个 scissor 批次的红左/蓝右同时可见；`HELLO` 文字区域亮像素 > 50。
- **时序**：DWM 合成滞后 `Present()` 1–2 帧，裸快照约 40% 读到旧帧；`readback_until` 每次 pump 一帧相同内容并重试，上限 8 次。修正后 20/20 连续通过。
- **App 生命周期陷阱**：像素测试必须排在其他测试之前且不调用 `quit()`——`quit()` 投递线程全局 WM_QUIT，而 `App::run_one_frame()` 在 `quit_requested` 为真时直接返回、无法 drain，会污染后续 App。
- **独立 review 结论：Accept after corrections**——bug 注入实验确认测试有效性：把 `dawn_renderer.cpp` 的 baseVertex 改回 `batch.vertex_offset` 后断言 (2) 3/3 失败；把 `batcher.cpp` 的 quad 尺寸改回归一化 UV 后断言 (3) 3/3 失败；还原后基线全过。20/20 连续运行无 flake。修正 2 个 MINOR（早退分支不 `quit()`、文件头注释更新）后重跑 clang 7/7。
- **覆盖限制 [INFO]**：`default`（MSVC）preset 未开启 `CONTINUI_BUILD_BACKEND_DAWN`，backend 测试仅在 `clang` preset 注册；像素回读断言目前只有 clang 背书。GDI 读回是 Win32 API，编译器差异影响有限；如需 MSVC 背书需单独配置 DAWN ON 的 MSVC 构建。

### B 阶段：0.7 交互 + TSF IME + fallback（实现完成，独立审计中）

- **范围**：LocalState（按 Key 持有 hover/pressed/focused、文本 caret/selection、IME composition）、`ViewFn` 签名改为 `View(FrameArena&, const LocalState&)`、`Input` 组合子、鼠标/键盘/文本编辑事件路由、TSF IME、字体 fallback 链。
- **IME 技术决策**：Windows 用 TSF（业主决策，遵循架构 §8.3 / ADR-015，未用 Imm32）。`ime_tsf.cpp` 实现 `ITextStoreACP` + `ITfContextOwnerCompositionSink`，composition 作为独立事件下发，不转成普通按键。
- **SDK 缺口**：本机 Windows SDK 有 `msctf.dll` 但无 `msctf.lib`；本地以 `constexpr kIidTfContextOwnerCompositionSink` / `kIidTfContextOwnerServices`（内部链接，避免与 `uuid`/`dxguid` 冲突）定义所需 IID，用 `GetProcAddress` 动态加载 `TF_CreateThreadMgr`。
- **fallback**：`Glyph` 携带 face 指针，`shape_with_fallback` 逐 codepoint 选择首个覆盖该字符的 face；逐字符 shaping 不产生连字/kerning（已知限制）。
- **验证**：clang 7/7、MSVC 6/6，无编译警告；`ui_test` 覆盖 LocalState/焦点/文本编辑/composition 事件，`text_test` 覆盖 segoeui→msyh 的 fallback face 选择；Counter 例子加入输入框与中文字体 fallback。真实中文 IME 需手工验收（无法自动化）。
- **独立审计结论：FAIL（BLOCKED，5 MAJOR + 5 MINOR + 3 INFO）→ 全部修正 → 待复核**。关键发现与修复：
  - [MAJOR F1] TSF 从未 `Push` context → IME 路径完全失效。已加 `doc_mgr->Push/Pop`，使 `AdviseSink` 被调用、锁可被授予。
  - [MAJOR F2] 编辑读取帧陈旧 arena 节点 → 连续输入丢字符（复现 `A`+`B`→`B`）。`WidgetState::edited_text` 成为事件间权威缓冲。
  - [MAJOR F3] 前向删除无效、backspace 后 caret 错位。改为直接从当前 caret/选区算删除范围，caret 用字符索引。
  - [MAJOR F4] caret/selection 存了但不按位置渲染。改为从实际字形 advance 计算 caret x 并绘制选区矩形。
  - [MAJOR F5] 候选窗定位/文本同步缺失。`UI::ime_context()` → `Window::set_ime_context()` → `TsfInputContext::sync_state()` 显式连接；`GetTextExt`/`GetACPFromPoint`/`GetScreenExt` 改用真实 caret/文本。
  - [MINOR] COM 初始化平衡、advise sink AddRef/Release、teardown 顺序、双重 commit、UTF-16 surrogate、本地 IID 命名、文档与测试覆盖均已修正。
  - 新增 `ui_test::test_editing_regressions`（连续输入/前向删除/删除后插入），clang 7/7、MSVC 6/6。真实 IME 仍需手工验收。
- **第二轮复核：FAIL（2 MAJOR + 3 MINOR）→ 已修正**。F1–F4/F6–F8/F10–F12 经原复现程序与代码证据确认修复；剩余：
  - [MAJOR N1] TSF 坐标空间不匹配：`GetTextExt` 返回 client 坐标（API 要求 screen）、`GetACPFromPoint` 用 screen 点比 client 矩形。已改为 `ClientToScreen`/`ScreenToClient`，fallback 用 `GetWindowRect`。
  - [MAJOR F9] 双重 commit 守卫无效：UI 的 `composition_end` 用 `state->composition` 而非 `event.text`，TSF 空 payload 守卫失效。已改为提交 `event.text`（空则不插入），并加"text_input + 空 composition_end 只插入一次"回归测试。
  - [MINOR N2] `GetTextExt` 返回 1.5px 竖线、selection 每帧被重置 → 改为返回完整文本范围，仅文本变化时重置 selection。
  - [MINOR N3] `WM_CHAR` surrogate 缓存是函数级 static → 移入 `WindowImpl`（每窗口独立）。
  - [MINOR N4] 文档不一致 → 已修正。
  - **第三轮复核结论：Accept**——审计员强制重编译两工具链（clang 7/7、MSVC 6/6，无警告），并用真实 pre-fix `ui.cpp` 对照程序证明 F9 回归测试能抓到双重提交（修复后 `"Hi中Z"`、pre-fix `"Hi中Z预"`）。N1 的 `ClientToScreen`/`ScreenToClient` 方向经 MSDN API 契约核对正确。无阻塞代码发现；真实中文 IME 仍需手工验收（环境无法自动化 TIP 驱动）。

### 用户手工验收发现的回归（提交 af1b1b0 之后）

| 现象 | 根因 | 修复 | 验证 |
|---|---|---|---|
| 数字 "42" 显示为 "4 2"、按钮/输入框文字间距翻倍并溢出容器 | `shape_with_fallback` 把累计 pen 加进 `glyph.offset_x`，batcher 又按 `advance` 累加 → pen 算两次 | 去掉 `offset_x += pen`，pen 只用于 `result.width`；新增 fallback 与直接 shaping 的 offset 对比断言 | clang 7/7、MSVC 6/6；截图确认显示正常 |
| composition 预输入时 caret 不跟随 | 预览串画在 caret 右侧但 caret 坐标未加预览宽度 | 预览串占位于 caret，caret 移到预览末尾，`ime_caret_` 同步跟随 | 用户复测确认 |
| 点击输入框无聚焦提示 | focus ring 画在背景之前，被不透明背景盖住 | 库硬编码最小亮蓝边框（四条细矩形，透明/不透明背景均可见），不暴露样式 API；`ui_test::test_focus_feedback` 断言透明背景也有反馈 | clang 7/7、MSVC 6/6 |

**架构决策（业主确认）**：先不扩展样式系统，保持"HTML/CSS 解耦"方向、当前只做结构层（View 树/布局/交互）。聚焦反馈由库硬编码最小实现，不新增 Style 字段；用户若需自定义样式，留待未来样式层（Phase 1）。此前一度加入的 `Style::focus_ring` 字段已按此决策撤销。review 指出的"透明背景实心块"问题已由四条细矩形边框解决。

### 0.8 阶段：失效闭环（实现完成，独立审计中）

- **范围**：失效等级真正驱动阶段调度 + 按 Key 值类型快照缓存（实用版 Tree Diff）。
- **决策落地**（业主确认）：0.8a+0.8b 本轮完成、保持 arena 帧内模型 + Key 快照缓存（不改 retained ViewNode）、不做 GPU 脏矩形合成。
- **关键实现**（`src/render/ui.cpp`、`include/continui/ui.hpp`）：
  - `DrawCommand`/`NodeVisual`/`NodeSnapshot` 值类型缓存，`build_commands()` 按 Key 比较签名，只有变化节点重新 shape；`replay()` 把命令重放进每帧清空的 Painter/BatchBuilder。
  - `solve_only()` 支持 layout 级只重求解、不重建 ViewFn；`font_epoch` 在字体/measurer 变更时递增使缓存失效。
  - `UI::frame_stats()` 暴露 evaluates/solves/command_generations/glyph_shapes 诊断计数（公共 API 新增，属测试/诊断用途，不改变绘制契约）。
  - LocalState 变化（hover/focus/IME）的失效等级由 paint 升为 view：ViewFn 读 LocalState，C3 无隐式追踪，必须保守重建视图投影。
  - **设计权衡（审计 Finding 3，需业主知晓）**：因此验收标准(3)"局部变化不无条件触发所有模块完整重建"只在**绘制命令层**满足（按 Key diff），view/layout 阶段对 LocalState 事件仍全量重算。这是 C3 无隐式依赖追踪的必然代价——框架无法知道 ViewFn 是否读取了 LocalState。若要更强的局部性，需引入显式的"ViewFn 读取 LocalState"声明（未来考虑）。
  - `Invalidation::full` 额外清空按 Key 快照缓存（真正强制刷新），`view` 仍复用未变节点命令（审计 Finding 5）。
- **验证**：`ui_test::test_invalidation_stages` 覆盖四条验收（不变零计算/layout 不重建/失效合并/局部重算 `command_generations==1`）；`ui_test::test_command_cache_content` 用真实字体 + glyph 敏感 stub 验证缓存重放字节一致、纯重放 `glyph_shapes==0`、文本变化只重绘一个节点（审计员用变异测试证明该测试能抓到丢弃缓存/陈旧 run）；clang 7/7、MSVC 6/6；Counter 目视显示正常。
- **独立审计结论：Accept（PASS WITH WARNINGS → 修正后 Accept）**——初审计 6 项发现（1 MINOR 高优先级验证缺口 + 4 MINOR + INFO）全部处置：命令缓存测试补强、stale 快照清理、`full` 清缓存、删死成员、注释规范化、设计权衡登记。双工具链干净重编译 7/7、6/6，无项目代码警告。

## 3. 已知问题登记（未修，但已记录）

| 问题 | 影响 | 归属阶段 | 代码位置 |
|---|---|---|---|
| 自动 Key 是位置序；按钮回调层已检测重复 Key，节点级重复未强制唯一 | 列表重排时身份错乱；违反架构 §4.3 规则 3 的严格形式 | 0.8 | `arena.hpp` / `view_tree.cpp` |
| 按 Key 快照缓存未覆盖节点跨帧 retained（每帧仍 build 树）；无 GPU 脏矩形 | 性能余量，正确性无影响 | 后续 | `ui.cpp::build_commands` |
| 事件使用上一帧布局 | resize 当帧的点击仍命中旧几何（resize 已在 paint 时触发重算，但事件先于 paint 泵出） | 0.8 / 输入阶段 | `ui.cpp:164` |
| 字体路径仅 ASCII；TTC 取 face 0 | Unicode 路径/字体集合受限 | 后续平台阶段 | `font_manager.cpp:101` |
| 图集无淘汰，上限 2048 | 大字数多语言时图集可能封顶 | 0.7+ | `glyph_cache.cpp:110` |
| fallback 逐字符 shaping，无连字/kerning | 混排文本的字距/连写不精确 | 0.7+ | `font_manager.cpp::shape_with_fallback` |
| uint16 索引上限 16384 quads/帧，超出静默丢弃 | 文本密集帧可能丢字 | 后续（u32 索引） | `batcher.cpp:70` |
| HWND 复用 / display-mode 变化 stale surface | 多窗口/DPI 阶段处理 | 后续 | `dawn_renderer.cpp:853` |

## 4. 测试矩阵

| 测试 | 覆盖代码 | 能抓什么 |
|---|---|---|
| `tests/core/key_test.cpp` 等 | Phase 0.1–0.2 | 基础契约 |
| `tests/render/batcher_test.cpp` | batcher | 拓扑、合批、帧绝对索引、text quad 像素尺寸、gamma 指数 |
| `tests/render/ui_test.cpp` | ui.cpp | 命中→回调→失效→重绘，回调不重入，外部点击忽略 |
| `tests/layout/layout_test.cpp` | arena + solver + invalidation | 生命周期、gap/padding/flex/对齐/钳制/命中/合并 |
| `tests/text/text_test.cpp` | text 模块 | shaping、metrics、图集增长、512px 单次增长 |
| `tests/backend/dawn_renderer_test.cpp` | 全 GPU 路径 | bring-up、纹理、painted 帧、增长负载、resize、uncaptured error；GDI 像素回读（纯色/跨批次/文字亮像素） |

**像素验证现状**：backend 测试用 `PrintWindow(PW_RENDERFULLCONTENT)` 读回客户区像素，断言纯色、双 scissor 批次、文字亮像素；因 DWM 合成滞后，`readback_until` 带 8 次有界重试。0.4/0.5 的历史缺陷现在有程序化断言防线；目视验收仍建议使用逐项检查清单。

## 5. 提交地图

| 提交 | 内容 |
|---|---|
| `ef13083` | ADR-009/020 文档 |
| `8d9365d` | Phase 0.4 渲染系统（20 文件，+1465） |
| `88eec82` | 0.1–0.4 状态标记 |
| `1581a58` | Phase 0.5 文本系统（31 文件，+1786） |
| `ed23fb3` | ADR-010 转正 + 0.5 状态 |
| `fe25b23` | 0.5 多增长残留修复 |
| `57c12a5` | umbrella 补 continui_render |
| `cb7c152` | Phase 0.6 UI 闭环（22 文件，+1453） |

## 6. 新增追溯记录的模板

```markdown
### [日期] 一句话标题
- 症状/发现：（用户可见现象或审计结论，附截图/日志路径）
- 证据：（file:line + 实际代码/运行结果）
- 根因：（机制解释，不是猜测）
- 修复：（改了什么、为什么这样改）
- 验证：（命令 + 结果；如涉及画面，写明截图检查项）
- 提交：（hash）
```
