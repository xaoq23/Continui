<div align="center">

# Continui

**Function-driven layout, GPU-rendered, Chromium-free, library-first.**

/kənˈtɪnjuː.aɪ/ —— 取自 *continue / continuous*：视图不是一堆实例化后等死的控件，而是从状态、样式、输入里持续生成的投影流。

一个 C++23 的现代、轻量、跨平台 PC 桌面 GUI 库。

</div>

---

## 为什么造这个轮子

桌面端 UI 已经被浏览器殖民了很多年。原因很简单：布局的黄金标准在浏览器里——盒模型、Flex、样式层叠，几十年打磨出来的心智模型，没人愿意再手写一遍坐标计算。于是大家纷纷把整个 Chromium 塞进安装包，用一台虚拟机来画按钮。

现有路线各有各的代价：

- **Qt**：体验完整，但它是框架不是库。元对象系统、信号槽所有权规则、构建链、资源系统，五套知识里四套是它自加的。学习曲线陡，大多不是 UI 本身难，而是框架在教你它的宗教。
- **Electron / WebView 套壳**：开发确实舒服，但内嵌浏览器带来的内存占用、启动速度、IPC 序列化开销躲不掉。计算密集的任务想高性能，还得写原生插件，跨语言跨进程传数据，两头不讨好。
- **ImGui**：轻快透明，但工具向，盒模型、样式、富视觉这些网页级的表达力基本没有。

Continui 想要的是三者交集：Qt 的完成度、Electron 的开发顺滑、ImGui 的轻盈透明——但用 C++23 从头写。我们认同 React 的函数式哲学，并想把它推到比 React 自己更彻底的位置：纯函数视图、显式依赖、没有调度黑盒。这在 JS 里做不到，在 C++23 里恰好能做满。

最终形态：业务计算和 UI 在同一个 C++ 内核里，同一个内存域，免序列化。你写交互式高性能系统时，不必再在「好看方便」和「原生高性能」之间二选一。

## 设计宪法

1. **库，不是框架**。不接管 main、不开隐式线程、不管理你的业务线程池、不提供网络/JSON/脚本引擎。违者视为 bug。
2. **单一世界观**。视图永远是纯函数投影；缓存、diff、脏区全是内部优化，对用户不可见。没有「即时/快照」两种范式——高频可视化用 `Canvas` 叶控件，不是另一个模式。
3. **显式失效，不做自动响应式**。谁改状态谁声明失效，没有依赖追踪图。一切「为什么重算了」都有链路可查。
4. **同内存域是红利，不是通行证**。业务、视图、渲染同一份 C++，免序列化；但边界不靠纪律靠签名：视图函数只拿 `const State&`，业务线程只提交补丁，渲染只消费绘制批。
5. **不绑架构建**。头文件 / CMake 子模块即可用；C++ Modules、vcpkg 是选项，不是门槛。
6. **不做全能 CSS**。守住实用子集，边界由「框架自己能解释每个样式值的求值过程」定义。
7. **报错诚实**。崩溃栈落到你写的代码；布局、样式解析、资源加载走 `std::expected`，不用异常。
8. **跨平台是红利不是负担**。一份代码多平台，该调平台特有接口时留口子。

## 核心范式

```
View = render(State, Style)   // 纯函数，每次失效后求值
```

失效 → 重算视图树（Arena 临时对象，帧末回收）→ 与上一棵树按稳定 Key 结构共享 / diff
→ 只对脏子树重布局、脏矩形重绘 → GPU 批渲染。

几个关键决定：

- **布局树不是 DOM**。只存盒、样式值、子节点引用、失效标记、无障碍元数据。没有事件监听器树、没有 GC、没有历史。
- **业务状态由你持有**。交互回调直接改它，改完显式 `invalidate()`。没有全局 Store，没有 Msg variant 模板噪音，没有 lambda 捕获式的绑定玄学。单向数据流，分层但不分离——业务和视图是两个纯函数边界，不是两套语言两个进程。
- **控件局部状态**（滚动、光标、焦点）由稳定 Key 寻址，框架持有，类型化、可检查。
- **多线程只有一个交点**。continui 不知道你的业务时钟存在，也不管理你的线程池。业务线程唯一能做的事是向显式创建的 `channel` 提交状态补丁：无锁队列，非阻塞，主线程帧首单点消费。计算可以并行，视图投影是状态的单时间线快照。

```cpp
#include <continui/continui.hpp>

auto counter_view(Counter& s) -> continui::View {
    using namespace continui;
    return Column({
        Text(fmt::to_string(s.n), Style{}.font_size(24).color(Colors::White)),
        Button("Click me", [&]{ ++s.n; s.invalidate(); }),
    }, Style{}.padding(16).gap(8).background(Color::Rgb(30, 30, 40)));
}

int main() {
    auto app = continui::App::create();
    Counter state;                                  // 状态是我的
    app.show(continui::make_window(state, counter_view));
    return app.run();                               // 事件循环可嵌入我的循环，非托管
}
```

### 同内存域，免序列化

仿真结果、轨迹、矩阵、点云不需要 JSON 打包丢给另一个运行时。视图函数直接拿 `const State&`、`std::span`、矩阵引用；绘制批可直接引用底层缓冲区，经持久映射缓冲上传 GPU。Electron 的时间大头往往不是渲染慢，是把计算结果搬进浏览器。

高频可视化不建布局子树，走 `Canvas` 叶控件——拿画布和只读状态，直接填充渲染批，盒模型、Flex、样式照常包在外面：

```cpp
Canvas(Size{800, 600}, [](Painter& p, const SimState& s) {
    p.transform(s.viewport);
    p.path(s.trajectory);
    p.heatmap(s.cost_field);
});
```

边界不靠文档纪律，靠类型：视图函数签名收死 `View(const State&)`，想改也改不了；状态补丁走 `channel`，主线程串行消费，用户无需理解任何锁；Arena 借据类型挡住布局树逃逸出帧。线程模型在文档里是规格，不是建议。

## 分层

```
continui
├── core     视图契约（View / ViewNode concept）、Key、失效调度、局部状态池、
│            Arena / 借据类型、channel（跨线程补丁队列，线程模型的唯一交点）
├── style    Style 值类型、继承与作用域、单位、主题变量
├── layout   盒模型、Flex 求解器（参考 taffy，独立纯函数模块）、文本折行、Canvas 叶控件
├── text     HarfBuzz + FreeType 封装、字形图集、字重 fallback 链
├── render   gfx::Device 抽象、GPU 批渲染、图集缓存、脏矩形合成、Painter 矩阵变换
├── backend  Dawn / wgpu-native（默认）→ D3D12 / Metal / GL（二期原生替换）
├── platform Win32 / Cocoa / X11 + Wayland：窗口、输入、剪贴板、IME、DPI
└── extras   hot_reload、devtools、msg-store、a11y 导出、可选脚本绑定（非强制）
```

布局求解器是无状态纯函数（节点树 + 样式 → 盒树），独立单测，可单独复用。

## 渲染与平台

- 渲染后端可插拔，业务 UI 代码只见 `gfx::Device`。首个后端用 Dawn / wgpu-native（静态链接，MB 级），原生后端（D3D12 / Metal）二期有 profiler 数据后替换，OpenGL / Vulkan 作降级与进阶选项。
- 绘制原语：批量圆角矩形、矢量路径、渐变、阴影、纹理、文字图集。GPU 批渲染，无独立合成器进程。
- Day 1 设计约束：
  - **IME**：Windows TSF / macOS NSTextInputClient / Linux text-input 协议，属于平台层 API 的一部分，后补等于重写窗口层；
  - **高 DPI**：物理像素 / 逻辑像素双坐标系贯穿全库，Per-monitor DPI、fractional scaling 一等支持；
  - **无障碍口子**：ViewNode 携带 `role / label` 元数据，`extras/a11y` 生成 UIA / NSAccessibility / AT-SPI 树。

## 构建

```bash
git clone --recurse-submodules https://github.com/<you>/continui.git
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

无巨型 SDK、无强制包管理器、无 moc 式预编译；`add_subdirectory` / 子模块 / 源码编译皆可。默认静态链接，目标是一个可执行、基础 runtime 几 MB。代码按模块组织，头文件分发；`import continui.*` 为可选构建目标。

**语言门槛固定为 C++23**，不因 C++26 提高门槛：核心范式 23 已足够，未来通过 feature-test macro 渐进启用反射、contracts 等 26 特性（有则增强，无则手写兜底），且设计上保持 26-ready——不用宏注册表之类会阻碍反射接入的写法。明确不采用 `std::execution`：调度归用户，continui 只管 UI 时间线。

## 路线

- **Phase 0｜打穿一条线**（Windows + Dawn）：窗口 + 输入 → 画三角形 → 文本渲染（拉丁）→ 盒模型 + 基础 Flex → 按钮 / 标签 / 单行输入 → 失效调度闭环。
  验收标准：改一行代码，30ms 内看到正确结果；文本框能接系统输入法。
- **Phase 1**：完整 Flex、样式作用域、焦点链、滚动容器、布局检查器（用 continui 画自己）、`Canvas` + Painter 矩阵变换。
- **Phase 2**：复杂文本（CJK / fallback）、图片、主题、外部样式文件、原生渲染后端、热重载、trace。
- **Phase 3**：组件包、动画插值、Grid、无障碍实现、独立 DevTools。

## 边界

Continui 不提供：网络、数据库、JSON、音频、脚本引擎、资源打包系统、任何全局单例、平台 WebView（可选插件，不是内核）、完整 CSS / HTML / JS 兼容层。

每当有人提议加个新特性，对着宪法问一句：框架还能解释它的求值过程吗？答不上来就不加。

## 胜负手

Demo 一周就能好看。Continui 真正的护城河是布局、文本、失效调度这三件无聊事的工程质量——让「三十毫秒内正确响应一次失效」稳定成立，其他一切都是加法。

---

<div align="center">

<em>最好的 GUI 库，是它存在，但你感觉不到它在教你一套新宗教。</em>

</div>

## 文档

- [架构规范（architecture.md）](docs/architecture.md)——定位、宪法、运行模型、核心对象、样式/布局/渲染/文本/输入规范、线程与内存模型、错误处理。
- [工程与流程规范（development.md）](docs/development.md)——目录结构、构建要求、开发路线、测试与质量、API 风格。
- [架构决策与原则（decisions.md）](docs/decisions.md)——ADR、开发纪律、交付目标、成功标准。
