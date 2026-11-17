# Continui — Architecture & Development Specification

> Document Version: 1.0-draft
> Project Language: C++23
> Project Type: Cross-platform Native Desktop GUI Library
> Architecture Style: Library-first / Declarative UI / Function-driven Rendering
> Initial Platform: Windows
> Initial Graphics Backend: Dawn

---

## 1. 项目概述

### 1.1 项目定位

Continui 是一个基于 C++23 的现代跨平台桌面 GUI 库。

它采用函数驱动的声明式 UI 架构，将业务状态映射为视图树，通过显式失效机制触发视图更新，使用 GPU 批量渲染完成最终呈现。

Continui 不试图成为一个完整的应用程序框架，而是提供可嵌入、可组合、可独立使用的 GUI 基础设施。

核心定位：

«Function-driven layout, GPU-rendered, Chromium-free, library-first.»

核心目标：

- 提供现代声明式 UI 开发体验。
- 提供接近 Web Flexbox 的布局表达能力。
- 保持 C++ 原生性能与同进程内存访问。
- 使用 GPU 加速完成现代桌面 UI 渲染。
- 通过显式失效取代隐式响应式依赖追踪。
- 不接管用户的业务逻辑、线程池、程序入口和构建体系。
- 提供跨平台窗口、输入、文本和渲染抽象。

### 1.2 设计目标

| 目标 | 具体要求 |
|---|---|
| 开发体验 | 声明式、组合式、低样板代码 |
| 性能 | GPU 批渲染、局部重布局、局部重绘 |
| 内存 | 无 GC、明确生命周期、Arena 分配 |
| 状态 | 用户持有业务状态，框架持有控件局部状态 |
| 调度 | 显式失效，不做隐式依赖追踪 |
| 线程 | 不管理业务线程池，提供明确的跨线程入口 |
| 平台 | Windows、macOS、Linux |
| 构建 | CMake、源码集成、可选 Modules |
| 运行时 | 不依赖 Chromium、WebView 或脚本引擎 |

### 1.3 非目标

Continui 不提供：

- 网络请求。
- 数据库。
- JSON 序列化。
- 音频系统。
- 通用脚本引擎。
- 业务状态管理框架。
- 任务调度器。
- 业务线程池。
- 完整 HTML/CSS/JavaScript 兼容层。
- 强制性的资源打包系统。
- 全局单例式应用服务。

这些能力可以由第三方库或可选扩展提供。

### 1.4 设计宪法

以下原则属于项目最高级别的架构约束。

| 编号 | 原则 | 含义 |
|---|---|---|
| C1 | Library-first | Continui 是库，不是框架。用户决定程序入口、线程模型、业务生命周期。 |
| C2 | Single UI Paradigm | 所有结构化 UI 使用统一的声明式视图范式。缓存和 diff 是内部优化，不对用户暴露第二套 UI 模式。 |
| C3 | Explicit Invalidation | 业务状态发生变化后，必须通过显式失效通知框架。不使用隐式依赖追踪图。 |
| C4 | Ownership by Boundary | 业务状态由用户管理；布局树由框架管理；跨线程数据通过明确的 Channel 边界传递。 |
| C5 | Native Memory | 业务数据与 UI 运行在同一个进程和内存域，不要求经过 JSON 或 IPC 序列化。 |
| C6 | Explainable Styling | 样式系统保持有限且可解释。任何样式值都必须具有明确的求值规则。 |
| C7 | Honest Errors | 用户代码错误、平台错误、布局错误、资源错误必须具有不同的错误边界。 |
| C8 | Platform Independence | 公共 API 不依赖具体操作系统和图形 API。平台特有功能通过明确的扩展接口暴露。 |

---

## 2. 总体架构

### 2.1 架构总览

Continui 采用分层架构，各模块通过稳定的公共接口通信。

```
┌──────────────────────────────────────────────────────────┐
│                      User Application                    │
│                                                          │
│   Business State     Business Logic      User Threadpool │
│        │                   │                    │        │
│        └───────────────────┴────────────────────┘        │
│                            │                             │
│                     Continui Public API                  │
└────────────────────────────┬─────────────────────────────┘
                             │
┌────────────────────────────▼─────────────────────────────┐
│                         CORE                             │
│                                                          │
│   View / ViewNode       Key       Invalidation           │
│   Arena / Lifetime      Local State      Channel         │
└────────────────────────────┬─────────────────────────────┘
                             │
             ┌───────────────┴────────────────┐
             │                                │
┌────────────▼────────────┐      ┌────────────▼────────────┐
│          STYLE          │      │          LAYOUT         │
│                         │      │                         │
│ Style / Theme           │      │ Box Model / Flex        │
│ Units / Inheritance     │      │ Text Measurement        │
│ Style Resolution        │      │ Layout Tree             │
└────────────┬────────────┘      └────────────┬────────────┘
             │                                │
             └───────────────┬────────────────┘
                             │
                  ┌──────────▼──────────┐
                  │       TEXT          │
                  │                     │
                  │ Shaping / Glyph     │
                  │ Font Fallback       │
                  │ Atlas / Metrics     │
                  └──────────┬──────────┘
                             │
                  ┌──────────▼──────────┐
                  │       RENDER        │
                  │                     │
                  │ Painter / DrawBatch │
                  │ GPU Resource Cache  │
                  │ Dirty Region        │
                  │ Compositing         │
                  └──────────┬──────────┘
                             │
                  ┌──────────▼──────────┐
                  │       BACKEND       │
                  │                     │
                  │ gfx::Device         │
                  │ Dawn / wgpu-native  │
                  │ Future Native APIs  │
                  └──────────┬──────────┘
                             │
                  ┌──────────▼──────────┐
                  │      PLATFORM       │
                  │                     │
                  │ Window / Input      │
                  │ IME / DPI           │
                  │ Clipboard / Events  │
                  └─────────────────────┘
```

### 2.2 模块职责

**core** —— UI 系统的基础契约。

主要组件：`View`、`ViewNode`、`Key`、`Invalidation`、`Arena`、`Lifetime`、`LocalState`、`Channel`、`App`。

core 不负责：GPU API 调用、具体布局计算、字体解析、操作系统窗口管理。

**style** —— 样式值、继承和解析。

主要组件：`Style`、`Color`、`Length`、`Spacing`、`FontStyle`、`Theme`、`StyleResolver`、`StyleScope`。

样式系统采用值类型设计，尽量避免运行时多态和动态分配。

**layout** —— 将声明式节点树转换成具有实际几何尺寸的布局结果。

主要组件：`LayoutNode`、`Box`、`Flex`、`TextMeasure`、`LayoutSolver`、`LayoutResult`、`Canvas`。

布局求解器必须保持无状态：

```cpp
LayoutResult solve(
    const LayoutTree& tree,
    const Constraints& constraints
);
```

相同输入必须产生相同输出，不允许在求解过程中修改业务状态或依赖 GPU。

**text** —— 字体、字形和文本布局能力。

主要组件：`FontManager`、`FontFace`、`FontFallback`、`TextShaper`、`GlyphRun`、`GlyphAtlas`、`TextMetrics`。

依赖：HarfBuzz（shaping）、FreeType（字体加载与字形栅格化）。

文本系统不负责平台窗口和输入法事件。

**render** —— 将布局结果转换为 GPU 绘制命令。

主要组件：`Painter`、`DrawBatch`、`RenderCommand`、`TextureCache`、`GlyphAtlasCache`、`DirtyRegion`、`Compositor`。

render 不直接调用 Win32、Cocoa 等平台 API。

**backend** —— 图形 API 抽象。首个后端使用 Dawn。

```cpp
namespace continui::gfx {
class Device;
class Buffer;
class Texture;
class Pipeline;
class CommandEncoder;
}
```

后续可增加 D3D12、Metal、Vulkan、OpenGL。是否直接增加原生后端，应以实际 profiler 数据为依据，而不是预先假设一定比 Dawn 更快。

**platform** —— 操作系统相关能力：`Window`、`EventLoop`、`Keyboard`、`Mouse`、`Touchpad`、`Clipboard`、`IME`、`DPI`、`Monitor`、`NativeHandle`。

平台模块是跨平台工程的关键隔离层。公共 UI API 不得直接暴露 `HWND`、`NSView` 等平台对象；平台扩展 API 可以提供受控的 `NativeHandle` 访问。

**extras** —— 可选扩展模块：`hot_reload`、`devtools`、`msg-store`、`a11y`、`script_binding`。extras 不得成为 core 的强制依赖。

---

## 3. 核心运行模型

### 3.1 核心范式

```
View = render(State, Style);
```

业务状态是唯一业务数据来源。视图是状态和样式的纯函数投影。框架负责将视图描述转换为内部布局树，并通过差异比较更新渲染结果。

完整流程：

```
Business State
      │
      ▼
Explicit Invalidation
      │
      ▼
View Evaluation
      │
      ▼
Temporary View Tree (Arena)
      │
      ▼
Stable Key Matching
      │
      ▼
Tree Reconciliation / Diff
      │
      ▼
Style Resolution
      │
      ▼
Layout Calculation
      │
      ▼
Dirty Region Calculation
      │
      ▼
Draw Batch Generation
      │
      ▼
GPU Submission
      │
      ▼
Present
```

### 3.2 帧生命周期

每一帧由以下阶段组成：

1. **Poll**：处理平台事件。
2. **Consume**：消费 Channel 中的业务补丁。
3. **Invalidate**：合并本帧失效请求。
4. **Evaluate**：重算需要更新的视图。
5. **Reconcile**：比较新旧节点树。
6. **Resolve**：计算样式。
7. **Layout**：计算几何尺寸。
8. **Paint**：生成绘制批次。
9. **Submit**：提交 GPU 命令。
10. **Present**：呈现窗口内容。
11. **Reclaim**：回收本帧 Arena 内存。

```cpp
void App::frame() {
    platform_.poll_events();
    channels_.consume_pending();
    scheduler_.process_invalidations();
    view_system_.evaluate();
    reconciler_.reconcile();
    style_system_.resolve();
    layout_system_.solve();
    renderer_.paint();
    renderer_.submit();
    platform_.present();
    frame_arena_.reset();
}
```

以上为逻辑顺序。具体阶段允许内部合并和优化，但不得破坏生命周期与数据依赖关系。

### 3.3 失效等级

| 类型 | 含义 | 需要执行的阶段 |
|---|---|---|
| None | 无变化 | 无 |
| Paint | 仅绘制变化 | Paint、Submit |
| Layout | 几何布局变化 | Layout、Paint |
| View | 视图结构变化 | Evaluate、Reconcile 及后续阶段 |
| Full | 全窗口强制刷新 | 全部阶段 |

失效等级应具有可合并性：

```
merge(Paint, Layout) == Layout
merge(Layout, View)  == View
merge(View, Full)    == Full
```

失效传播必须支持向父节点和必要的依赖节点传播。

### 3.4 纯函数边界

视图计算不得修改业务状态。

```cpp
template<class State>
using ViewFn = View (*)(const State&, BuildContext&);
```

交互回调可以持有用户状态的可变引用，但只能在事件触发时执行：

```cpp
Button("Increment", [&state] {
    ++state.count;
    state.invalidate();
});
```

必须区分两个阶段：

- **View Evaluation**：只读取业务状态。
- **Event Execution**：允许用户业务代码修改状态。

禁止在 View Evaluation 阶段执行事件回调。这一区分是函数驱动架构成立的基础。

---

## 4. 核心对象与接口规范

本章节定义公共 API 的设计方向。具体模板参数、内部存储和 ABI 可在原型验证后冻结。

### 4.1 View

View 是公共 UI 描述类型。

```cpp
namespace continui {
class View;
}
```

View 应满足：

- 支持值语义。
- 支持组合。
- 不要求用户管理底层节点内存。
- 不暴露内部布局树。
- 可以在 Arena 生命周期内构建。
- 不允许内部节点引用在 Arena 释放后继续使用。

推荐接口：

```cpp
View Column(std::initializer_list<View> children, Style style = {});
View Row(std::initializer_list<View> children, Style style = {});
View Text(std::string_view content, Style style = {});
View Button(std::string_view label, Callback callback, Style style = {});
View Canvas(Size size, CanvasCallback callback);
```

### 4.2 ViewNode

ViewNode 是内部布局树的基础节点。

```cpp
struct ViewNode {
    NodeType type;
    Key key;
    StyleId style_id;
    NodeFlags flags;
    ChildRange children;
    AccessibilityMetadata accessibility;
};
```

ViewNode 不存储：业务对象所有权、完整历史状态、GC 信息、长期事件监听器注册表、平台窗口句柄。

内部节点类型使用紧凑的类型标识与连续存储，避免不必要的虚函数和碎片化分配。

### 4.3 Key

Key 用于标识节点在连续视图更新过程中的身份。

```cpp
struct Key {
    uint64_t value;
};
```

规则：

1. 同一逻辑节点在不同帧中必须保持相同 Key。
2. Key 不应依赖节点在容器中的位置。
3. 重复 Key 必须被检测。
4. 节点销毁后，其局部状态必须能够回收。
5. Key 的身份作用域必须明确，建议限定在所属 Window 或 Root Tree 内。

建议提供带 Key 的重载，列表场景支持用户提供稳定业务 ID：

```cpp
View Text(Key key, std::string_view content, Style style = {});
```

### 4.4 Arena 与生命周期

Arena 负责分配帧内临时对象。

设计原则：批量分配、帧末批量回收、不允许布局树逃逸、避免逐节点释放、通过借据类型约束生命周期。

```cpp
class FrameArena {
public:
    ArenaScope begin_scope();
    void reset();
};
```

所有指向 Arena 内部对象的引用必须携带生命周期约束。禁止：

```cpp
ViewNode* saved = frame_arena.allocate<Node>();
// 在 Arena reset 之后继续使用 saved
```

需要区分四种生命周期，不得共用同一种管理策略：

- 帧内临时 ViewNode。
- 跨帧持久化的节点匹配信息。
- 长期 GPU 资源。
- 控件局部状态。

### 4.5 LocalState

框架持有控件自身的局部状态：Scroll Position、Text Cursor、Text Selection、Focus、Hover、Pressed、IME Composition State。

业务状态不应放入 LocalState。

通过稳定 Key 和类型化访问：

```cpp
auto& scroll = context.local_state<ScrollState>(key);
```

必须检查类型一致性，不能允许同一个 Key 被不同控件类型无检查地复用。

### 4.6 Channel

Channel 是跨线程业务数据进入 UI 主线程的唯一标准入口。

```cpp
auto channel = app.create_channel<Patch>();

// 业务线程：
channel.send(patch);

// 主线程（帧首消费）：
channel.consume([](const Patch& patch) {
    apply_patch(patch);
    state.invalidate();
});
```

规范：

- 业务线程不得直接修改 UI 内部数据结构。
- 业务线程不得直接操作布局树和渲染资源。
- Channel 的消费发生在主线程帧首。
- 同一批补丁按照明确的顺序消费。
- 不允许阻塞式等待 UI 线程。
- 队列容量、溢出策略和关闭行为必须明确。

重要决策：Channel 的无锁实现属于性能优化目标，而不是第一阶段的正确性前提。初期采用经过验证的线程安全队列；只有在性能测试证明存在瓶颈后，才针对生产者数量、消费者数量和内存序设计无锁实现。

### 4.7 App

App 负责管理 GUI 运行时，但不拥有用户业务状态。

```cpp
namespace continui {
class App {
public:
    static std::expected<App, AppError> create();

    int run();
    void run_one_frame();
    void request_redraw();
    void quit();
};
}
```

嵌入外部事件循环的能力是重要目标，但必须明确区分：完全托管事件循环、用户驱动单帧、平台事件泵集成。不能假设所有平台的原生事件循环都支持完全相同的嵌入方式。

### 4.8 Window

```cpp
WindowDesc desc{
    .title = "Continui Demo",
    .size = {1280, 800},
    .resizable = true
};

auto window = app.create_window(desc);
window.set_root(make_window(state, counter_view));
window.show();
```

Window 应独立于业务 State 的具体类型。一个 App 可以管理多个 Window，每个 Window 拥有独立的：根视图、失效调度上下文、Focus 链、DPI 状态、渲染目标、窗口级控件状态。

---

## 5. 样式系统规范

### 5.1 设计目标

样式系统参考 CSS 的开发体验，但不追求 CSS 兼容。采用 C++ 值类型和链式 API。

```cpp
Style style{
    .padding = 16,
    .gap = 8,
    .background = Colors::Slate900
};

// 或链式：
Style{}.padding(16)
       .gap(8)
       .background(Colors::Slate900)
       .font_size(24);
```

### 5.2 样式分类

第一阶段需要支持：

| 分类 | 属性 |
|---|---|
| Layout | display、direction、width、height |
| Spacing | margin、padding、gap |
| Flex | grow、shrink、basis、align、justify |
| Background | color、gradient |
| Border | width、color、radius |
| Text | font size、weight、color、line height |
| Visual | opacity、shadow |
| Transform | translate、scale、rotate |

不在初期实现完整 CSS 选择器和层叠机制。

### 5.3 单位

```cpp
struct Length {
    enum class Unit {
        Px,
        Percent,
        Auto,
        Fraction
    };

    float value;
    Unit unit;
};
```

必须明确：Px 表示逻辑像素还是设备像素；Percent 相对于哪个父级尺寸计算；Auto 的约束求解规则；Fraction 是否只允许用于特定布局上下文。

第一版优先支持：逻辑像素、百分比、Auto、内容自适应。Fraction 可随 Grid 或其他布局扩展引入。

### 5.4 样式继承

只对有明确继承意义的属性开放继承：Font Family、Font Size、Font Weight、Text Color、Line Height。

背景、边框、布局尺寸等默认不继承。

---

## 6. 布局系统规范

### 6.1 总体原则

布局系统是独立于 GPU 和平台的纯计算模块。

```cpp
LayoutResult solve(
    const LayoutTree& tree,
    const Constraints& constraints
);
```

输入：节点结构、样式值、父级约束、文本测量结果。

输出：每个节点的几何盒、内容区域、子节点位置、裁剪区域、布局诊断信息。

### 6.2 Box Model

每个节点至少包含完整的盒模型结构：

```
┌─────────────────────────────┐
│           Margin            │
│  ┌───────────────────────┐  │
│  │        Border         │  │
│  │  ┌─────────────────┐  │  │
│  │  │     Padding     │  │  │
│  │  │  ┌───────────┐  │  │  │
│  │  │  │  Content  │  │  │  │
│  │  │  └───────────┘  │  │  │
│  │  └─────────────────┘  │  │
│  └───────────────────────┘  │
└─────────────────────────────┘
```

### 6.3 Flex Layout

第一阶段实现基础 Flex：Row / Column、Main Axis、Cross Axis、Gap、Padding、Margin、Align Items、Justify Content、Flex Grow、Flex Shrink、Flex Basis、Min / Max Size。

布局算法可参考 Taffy，但必须通过独立的行为测试。不应为了快速实现而直接将 Web CSS 的全部布局规则引入。

### 6.4 文本测量

文本测量属于布局依赖：

```cpp
TextMetrics measure_text(
    const TextRun& text,
    const Constraints& constraints
);
```

必须支持：单行测量、自动折行、最大宽度约束、行高、字体 fallback、字重变化。

布局与文本 shaping 之间需要明确缓存失效规则。

---

## 7. 渲染系统规范

### 7.1 渲染架构

采用 Retained Layout + Batched Rendering 的组合。

Retained 部分负责：节点结构、布局结果、样式结果、局部失效信息。

Batched Rendering 负责：收集绘制命令、合并相同材质的绘制操作、管理 GPU Buffer、执行纹理与字形图集采样、提交渲染任务。

不要求每个 UI 节点对应一个 GPU Object。

### 7.2 Painter

Painter 是公共绘制抽象：

```cpp
class Painter {
public:
    void rect(Rect, Paint);
    void rounded_rect(Rect, Radius, Paint);
    void line(Point, Point, Stroke);
    void path(const Path&, Paint);
    void image(const Texture&, Rect);
    void text(const GlyphRun&, Point);

    void transform(const Matrix4&);
    void clip(Rect);
};
```

Painter 负责记录绘制命令，不直接暴露 Dawn 的 CommandEncoder。

### 7.3 Canvas

Canvas 是声明式 UI 中的叶节点，允许高频绘制直接使用业务数据，但不创建大量结构化布局子节点。

```cpp
Canvas(
    Size{800, 600},
    [&simulation](Painter& painter) {
        painter.transform(simulation.viewport);
        painter.path(simulation.trajectory);
        painter.heatmap(simulation.cost_field);
    }
);
```

Canvas 必须遵守以下约束：

- 不创建额外的布局子树。
- 只能在有效绘制阶段使用 Painter。
- 绘制回调的生命周期不得超出所属视图。
- 大规模数据应尽量通过 `std::span`、视图和只读缓冲区访问。
- 不允许跨线程直接提交 GPU 绘制命令。

业务数据与 GPU 资源的生命周期必须分离。特别注意：CPU 侧零拷贝不等于 GPU 侧零拷贝，上传、同步和资源所有权仍需由渲染模块管理。

### 7.4 脏区与重绘

渲染系统应区分：Layout Dirty、Paint Dirty、Resource Dirty、Full Window Dirty。

局部重绘的正确性优先于优化。初期可以先使用整窗口重绘，待绘制结果稳定后再增加脏矩形合成。

---

## 8. 文本与平台输入规范

### 8.1 文本系统

文本系统基于 HarfBuzz、FreeType、Glyph Atlas。

```
UTF-8 Input
    │
    ▼
Unicode Processing
    │
    ▼
Font Selection / Fallback
    │
    ▼
HarfBuzz Shaping
    │
    ▼
GlyphRun
    │
    ▼
Glyph Rasterization
    │
    ▼
Glyph Atlas
    │
    ▼
GPU Rendering
```

文本系统必须将字符、字形和字形位置区分开。不能假设一个 Unicode Code Point 对应一个 Glyph。

### 8.2 输入系统

平台输入事件统一转换为内部事件类型：

```cpp
struct InputEvent {
    EventType type;
    Timestamp timestamp;
    Modifier modifiers;
};
```

支持：Keyboard、Mouse、Scroll、Pointer、Focus、Text Input、Composition、Clipboard。

文本输入和键盘按键必须区分。

### 8.3 IME

IME 是第一阶段窗口层的架构要求。

| 平台 | 技术 |
|---|---|
| Windows | TSF |
| macOS | NSTextInputClient |
| Linux | Wayland text-input 协议 / X11 对应输入法接口 |

必须支持：Composition Start / Update / Commit / Cancel、候选窗口定位、光标位置同步、选区同步。

IME 事件不得简单地转换成普通字符按键事件。

### 8.4 DPI

统一使用逻辑坐标作为 UI 布局坐标，平台层负责逻辑坐标和物理像素转换。

```cpp
struct DisplayScale {
    float x;
    float y;
};
```

必须支持：Per-monitor DPI、窗口跨屏幕迁移、DPI 动态变化、逻辑像素与物理像素转换、Fractional Scaling。

---

## 9. 线程与内存模型

### 9.1 线程模型

Continui 不创建或管理用户的业务线程池。

```
                 Business Threads
                 ┌──────┬──────┐
                 │      │      │
                 ▼      ▼      ▼
               Channel / Patch Queue
                         │
                         ▼
                  Main Thread
                         │
               ┌─────────┴─────────┐
               │                   │
           UI Evaluation       Platform Events
               │                   │
               └─────────┬─────────┘
                         │
                    Render System
                         │
                         ▼
                      Present
```

主线程是 UI 状态快照的唯一构建者。

业务线程可以：进行任意业务计算、使用用户自己的线程池、生成业务补丁、通过 Channel 提交补丁。

业务线程不能：修改 UI 内部节点树、操作平台窗口、修改控件局部状态、直接调用 Painter。

### 9.2 内存分类

| 内存类型 | 所有者 | 生命周期 |
|---|---|---|
| Business State | 用户 | 用户决定 |
| View Tree | Continui | 帧内或受控持久期 |
| Reconciliation Data | Continui | 跨帧 |
| Local State | Continui | Key 生命周期 |
| GPU Resources | Renderer | GPU 资源生命周期 |
| Platform Resources | Platform | Window / App 生命周期 |
| Channel Queue | Channel | Channel 生命周期 |

禁止将所有对象都塞进帧 Arena。

### 9.3 GPU 同步

GPU 资源不能因为 CPU 帧结束而立即销毁。Renderer 必须使用适当的资源延迟回收机制，确保 GPU 已完成对应工作后再释放资源。第一版可以采用基于帧序号的延迟释放策略。

---

## 10. 错误处理规范

### 10.1 基本原则

公共 API 尽量使用 C++23 `std::expected` 处理可预期错误：

```cpp
std::expected<Window, WindowError>
create_window(const WindowDesc& desc);
```

适用范围：Window 创建、GPU 初始化、字体加载、资源加载、样式解析、布局约束错误、平台功能初始化。

### 10.2 错误分类

```cpp
enum class ErrorDomain {
    Core,
    Style,
    Layout,
    Text,
    Render,
    Backend,
    Platform,
    Resource
};
```

错误应携带：错误域、错误代码、可读描述、相关节点 Key、可选诊断上下文。

### 10.3 异常边界

不应将「不使用异常」理解为整个项目绝对不能存在异常。

规范：

- 公共可预期失败使用 `std::expected`。
- 内存耗尽等无法正常恢复的情况不承诺通过 expected 完整恢复。
- 不允许异常跨越平台 ABI 边界。
- 用户回调的异常必须在明确的边界处理，不能让异常无约束地穿越事件循环或 GPU 提交阶段。
- Debug 模式提供必要断言。
