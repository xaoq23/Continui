# Continui — Architecture Decisions & Principles

> 配套文档：[architecture.md](architecture.md)、[development.md](development.md)

---

## 15. 架构决策记录（ADR）

以下决策构成 Continui 的长期设计方向。

| ID | 决策 | 状态 |
|---|---|---|
| ADR-001 | Library-first，而非 Framework-first | Accepted |
| ADR-002 | Function-driven Declarative View | Accepted |
| ADR-003 | Explicit Invalidation | Accepted |
| ADR-004 | 用户拥有业务状态 | Accepted |
| ADR-005 | Arena 管理帧内临时对象 | Accepted |
| ADR-006 | Key 寻址控件身份和局部状态 | Accepted |
| ADR-007 | GPU Batch Rendering | Accepted |
| ADR-008 | Canvas 作为高频绘制叶节点 | Accepted |
| ADR-009 | Dawn 作为首个图形后端 | Accepted |
| ADR-010 | HarfBuzz + FreeType 文本系统 | Proposed |
| ADR-011 | Channel 作为跨线程业务数据入口 | Accepted |
| ADR-012 | Channel 底层采用无锁队列 | TBD |
| ADR-013 | Flex 算法参考 Taffy | Proposed |
| ADR-014 | D3D12 / Metal 原生后端 | TBD |
| ADR-015 | 全平台 IME 支持 | Accepted |
| ADR-016 | 逻辑像素作为公共布局单位 | Accepted |
| ADR-017 | 构建体系以 CMake 为唯一正式支持；自研 cizon2 暂不接入 | Accepted |
| ADR-018 | App/Window 实现置于 platform 模块（Win32 优先），公共契约留在 core 头文件 | Accepted |
| ADR-019 | Phase 0.2 异常边界：事件回调异常传播到 run()/run_one_frame() 的调用方，泵内不捕获 | Accepted |
| ADR-020 | 主力工具链 clang++（MSVC ABI），cl.exe 作周期性对照，不支持 MinGW ABI | Accepted |

### ADR 规范

每个重要架构决策必须记录：

- 背景。
- 要解决的问题。
- 候选方案。
- 最终决定。
- 优缺点。
- 对其他模块的影响。
- 是否允许未来修改。

---

## 16. 开发纪律

### 16.1 不允许的架构行为

以下行为视为架构违规：

1. 在 core 中直接调用平台 API。
2. 在 layout 中直接调用 GPU API。
3. 业务线程直接修改 UI 内部状态。
4. 在视图求值阶段执行有副作用的业务操作。
5. 使用全局单例保存业务状态。
6. 让控件局部状态依赖不稳定的节点位置。
7. 将跨帧 GPU 资源直接分配到帧 Arena。
8. 为追求性能而提前引入无法证明必要性的复杂机制。
9. 将平台特殊行为无条件泄漏到公共 API。
10. 为了兼容 CSS 而不断扩大样式系统范围。

### 16.2 开发优先级

```
Correctness
    ↓
Lifecycle Safety
    ↓
API Clarity
    ↓
Cross-platform Consistency
    ↓
Performance
    ↓
Convenience Features
```

性能优化不得破坏生命周期和接口边界。

### 16.3 AI 辅助开发规范

后续使用 AI 编程代理开发时，每个任务必须包含：

- 所属模块。
- 修改目标。
- 允许修改的文件范围。
- 依赖模块。
- 公共接口约束。
- 生命周期约束。
- 测试要求。
- 验收标准。

AI 不得擅自：

- 改变核心架构范式。
- 引入新的全局状态管理系统。
- 修改其他模块的公共接口而不说明原因。
- 引入未经批准的第三方依赖。
- 用临时实现替代正式接口后不留下记录。

开发任务应尽量独立、可编译、可测试。

---

## 17. 当前阶段的关键交付目标

当前项目不应同时追求完整的跨平台能力和完整控件生态。

第一阶段只需要证明以下链路：

```
C++ Application
      │
      ▼
Continui::App
      │
      ▼
Native Window
      │
      ▼
Dawn Device
      │
      ▼
GPU Rendering
      │
      ▼
View Tree
      │
      ▼
Flex Layout
      │
      ▼
Text + Button
      │
      ▼
Input + Invalidation
      │
      ▼
Interactive Application
```

最终应完成一个真正由 Continui 自己构建的桌面 Demo，建议包含：

- 一个窗口。
- 一个侧边栏。
- 一个顶部导航区。
- 多个文本和按钮。
- 一个输入框。
- 一个滚动区域。
- 一个实时 Canvas。
- 一个后台计算任务。
- 一个通过 Channel 更新的数据显示区域。

这个 Demo 应当能够同时证明：

1. 声明式 UI 的开发体验。
2. 原生 C++ 业务逻辑集成。
3. GPU 绘制能力。
4. 显式失效机制。
5. 跨线程状态更新。
6. 基础布局和文本系统。

---

## 18. 项目成功标准

Continui 不以控件数量、API 数量或者代码规模作为主要成功指标。

项目需要证明五件事：

1. **开发体验**：用户能够用简单、直观的 C++ 代码构建复杂 UI，而不需要学习一套庞大的框架专有机制。
2. **架构透明**：状态如何进入视图、视图如何生成布局、布局如何转换为绘制命令，整个过程可解释、可追踪。
3. **原生性能**：高频数据可视化能够直接访问 C++ 数据结构，避免不必要的跨运行时复制和序列化。
4. **平台能力**：窗口、输入、IME、DPI、文本等底层能力能够稳定工作，而不只是展示效果良好的 Demo。
5. **长期可维护性**：模块边界清晰，生命周期明确，公共 API 不被底层实现绑架。

---

## 19. 最终设计原则

Continui 的设计哲学可以归纳为：

- 用函数描述 UI，而不是手动维护 UI 对象。
- 用显式失效替代隐式依赖追踪。
- 用稳定 Key 管理节点身份。
- 用 Arena 管理短生命周期对象。
- 用纯函数布局求解器隔离几何计算。
- 用 Painter 隔离绘制表达与 GPU API。
- 用 Platform Backend 隔离操作系统差异。
- 用 Channel 隔离业务线程和 UI 主线程。
- 用 RAII 管理资源生命周期。
- 用有限且可解释的抽象控制复杂度。

Continui 不需要重新发明所有底层技术。它需要做的是把成熟的布局、文本、GPU 和平台能力组织成一套统一、透明、符合现代 C++ 使用习惯的开发接口。

«最好的 GUI 库，是它存在，但你感觉不到它在教你一套新宗教。»

这既是 Continui 的产品目标，也是整个工程必须长期遵守的架构原则。
