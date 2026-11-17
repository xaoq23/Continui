# Continui — Engineering & Process Specification

> 配套文档：[architecture.md](architecture.md)、[decisions.md](decisions.md)

---

## 11. 工程组织与构建规范

### 11.1 推荐目录结构

```
continui/
│
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE
│
├── include/
│   └── continui/
│       ├── continui.hpp
│       ├── app.hpp
│       ├── window.hpp
│       ├── view.hpp
│       ├── style.hpp
│       ├── key.hpp
│       ├── channel.hpp
│       ├── painter.hpp
│       └── widgets/
│
├── src/
│   ├── core/
│   ├── style/
│   ├── layout/
│   ├── text/
│   ├── render/
│   ├── backend/
│   │   └── dawn/
│   ├── platform/
│   │   └── windows/
│   └── extras/
│
├── tests/
│   ├── core/
│   ├── style/
│   ├── layout/
│   ├── text/
│   ├── render/
│   └── integration/
│
├── examples/
│   ├── hello_world/
│   ├── counter/
│   ├── flex_demo/
│   ├── text_input/
│   └── canvas_demo/
│
├── benchmarks/
│
├── docs/
│   ├── architecture.md
│   ├── development.md
│   ├── decisions.md
│   └── design_decisions/
│
└── third_party/
```

### 11.2 构建要求

最低标准：

- C++23。
- CMake。
- 明确的第三方依赖版本。
- 支持静态链接。
- 支持 `add_subdirectory()`。
- 不强制使用包管理器。
- 不强制使用 C++ Modules。

建议构建目标：

```
continui_core
continui_style
continui_layout
continui_text
continui_render
continui_backend_dawn
continui_platform
continui
```

各模块应尽量维持单向依赖。禁止：

- core 依赖 backend。
- layout 依赖 platform。
- style 依赖 render。
- 公共头文件无必要地暴露第三方库实现细节。

### 11.3 第三方依赖策略

| 依赖 | 用途 | 原则 |
|---|---|---|
| Dawn | GPU 抽象 | 初始默认后端 |
| HarfBuzz | 文本 shaping | 核心文本依赖 |
| FreeType | 字体加载 | 核心文本依赖 |
| fmt | 格式化 | 可选或内部依赖 |
| Taffy | Flex 算法参考 | 优先参考和对照，不强制运行时依赖 |

第三方依赖应尽量通过内部适配层隔离。

---

## 12. 开发路线

开发采用纵向切片（Vertical Slicing）策略：优先打通一条完整运行链路，而不是同时建设所有模块。

### Phase 0：打穿最小闭环

目标：Windows + Dawn，完成最小可运行 GUI。

**0.1 工程初始化**

- 建立 CMake 工程、模块目录。
- 接入 Dawn。
- 建立 Windows Platform Backend。
- 建立基础错误处理。
- 配置 Debug / Release。

验收：项目能够稳定构建；第三方依赖版本固定；提供独立的 Hello World 工程。

**0.2 Window 与事件循环**

- 创建原生窗口。
- 处理窗口关闭、尺寸变化。
- 处理键盘、鼠标事件。
- 建立可嵌入的单帧运行接口。

验收：创建、显示、关闭窗口；不依赖额外 GUI 框架；外部代码可以驱动单帧运行。

**0.3 GPU 初始化**

- 创建 Dawn Instance、Adapter、Device、Surface。
- 创建 SwapChain 对应的呈现资源。
- 建立基础 Pipeline，绘制三角形。

验收：稳定显示三角形；正确响应窗口尺寸变化；正确释放 GPU 资源。

**0.4 基础绘制**

- Rect、Rounded Rect、Color、Texture。
- Alpha Blending、DrawBatch。

验收：绘制多个 UI 基础图形；减少不必要的 Draw Call；基础混合和裁剪正确。

**0.5 文本渲染**

- 接入 FreeType，建立 Glyph Atlas。
- 接入 HarfBuzz，绘制拉丁字符。
- 建立基本字体缓存。

验收：显示正常文本；字号、字重和位置正确；字形图集支持动态增长。

**0.6 最小布局**

- View、ViewNode、Arena。
- Box Model、基础 Flex、Text Measure。

验收：能构建嵌套布局；布局结果符合约束；求解器独立测试通过。

**0.7 最小交互**

- Button、Label、单行 Text Input。
- Focus、Keyboard Input、IME。

验收：鼠标交互正确；键盘输入正确；Windows 中文输入法能够正常使用；文本选区、光标和基本编辑行为正确。

**0.8 失效闭环**

- Explicit Invalidation、View Re-evaluation。
- Key Matching、Tree Diff。
- Layout Dirty、Paint Dirty。

验收：状态变化能够更新 UI；不修改状态时不重复执行不必要的视图计算；局部变化不会无条件触发所有模块的完整重建；多次失效能够合并。

**Phase 0 最终交付物**：一个使用 Continui 构建的可交互 Counter Demo，包含窗口、文本、按钮、布局、输入和状态更新。

### Phase 1：建立可用的 UI 基础设施

目标：从技术原型升级为可以开发小型桌面应用的 UI 库。

开发内容：

- 完整 Flex 基础能力。
- 样式继承与作用域。
- 稳定 Key、局部状态池。
- Focus 链、Scroll Container。
- Canvas、Painter Matrix。
- Channel、局部重绘。
- Layout Inspector、错误诊断。

验收：可以构建多页面结构；可以实现设置页面、列表和滚动容器；可以实现实时 Canvas 绘制；多线程业务补丁可以安全进入 UI；可以通过布局检查器定位节点尺寸问题。

本阶段结束后，应具备一个能够展示自身功能的 Continui Showcase。

### Phase 2：跨平台与完整文本体验

目标：建立真正可用的跨平台 GUI 基础设施。

开发内容：

- macOS Platform Backend、Linux X11 Backend、Linux Wayland Backend。
- CJK 字体 fallback、复杂文本 shaping。
- 图片资源、主题系统、外部样式文件。
- 原生图形后端实验、Hot Reload、Render Trace、性能分析工具。

验收：Windows、macOS、Linux 能运行相同示例；CJK 显示和输入正常；多屏 DPI 变化正常；字体 fallback 正确；平台差异不会泄漏到公共 API。

### Phase 3：生态与高级能力

目标：从 UI 库发展为完整的桌面 UI 基础设施。

开发内容：

- Grid、Animation、Accessibility。
- Component Package、独立 DevTools。
- Script Binding（可选）、C++ Modules、更多图形后端。
- 组件文档与示例仓库。

验收：提供完整的公共 API 文档；提供无障碍基础能力；提供可扩展组件机制；建立稳定版本发布流程。

---

## 13. 测试与质量规范

### 13.1 测试层级

| 级别 | 测试内容 |
|---|---|
| Unit | 独立模块算法和数据结构 |
| Integration | 模块之间的接口协作 |
| System | 完整窗口、输入、渲染链路 |
| Benchmark | 性能、内存、延迟 |

### 13.2 核心测试

必须覆盖：

| 模块 | 测试内容 |
|---|---|
| Core | Key 稳定性、Arena 生命周期、Invalidation 合并、Diff 正确性、LocalState 回收 |
| Style | 属性解析、继承、单位转换、样式作用域 |
| Layout | Flex 方向、尺寸约束、Margin/Padding、文本折行、Min/Max Size、嵌套布局 |
| Text | UTF-8、CJK、Font Fallback、Glyph Position、字体缺失、混合文字方向 |
| Render | Alpha Blending、Clipping、Transform、Texture Upload、GPU Resource Lifetime、Resize |
| Platform | Window Lifecycle、Input Events、IME、DPI、Clipboard |

### 13.3 性能指标

性能目标必须使用可重复的 Benchmark 测量。

| 指标 | 测量目标 |
|---|---|
| View Evaluation | 单次视图计算耗时 |
| Diff | 节点比较耗时 |
| Layout | 单次布局求解耗时 |
| Paint | 绘制命令生成耗时 |
| GPU | GPU 执行耗时 |
| Present | 帧呈现延迟 |
| Memory | CPU / GPU 内存占用 |
| Startup | 冷启动耗时 |

原始方案提出的 30ms 目标，应拆分为明确的测试条件：

- 固定硬件、固定窗口尺寸、固定节点规模。
- 固定失效范围、固定渲染后端。
- 分别记录 CPU 和 GPU 耗时。

不能把 30ms 作为所有复杂度和硬件条件下的绝对保证。

---

## 14. API 与代码风格规范

### 14.1 命名规范

公共命名空间：

```cpp
namespace continui {}
```

- 类型使用 PascalCase：`class View;` `struct LayoutResult;` `class Painter;`
- 函数使用 snake_case：`create_window();` `invalidate();` `run_one_frame();`
- 常量使用明确的命名形式：`constexpr float default_font_size = 14.0f;`

### 14.2 头文件

公共头文件只暴露必要接口。

优先使用：Forward Declaration、PImpl（确有必要时）、值类型、`std::span`、`std::string_view`、`std::expected`、`std::optional`。

避免无必要地暴露：Dawn 内部类型、平台 SDK 类型、第三方字体库类型、内部 Arena 实现、内部容器实现。

### 14.3 资源所有权

必须明确：谁创建、谁持有、谁销毁、是否共享、是否跨帧、是否跨线程。

优先采用 RAII。不允许在公共 API 中出现所有权不明确的裸指针。

### 14.4 API 稳定性

项目早期允许 API 调整，但必须遵守：

1. 公共 API 与内部实现分离。
2. 破坏性 API 变更必须更新文档和示例。
3. 不在没有必要时承诺稳定 ABI。
4. 第三方依赖类型不能无边界地泄漏到公共接口。
5. API 设计必须考虑未来多个 Window 和多个 App 实例。
