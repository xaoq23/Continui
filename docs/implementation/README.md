# Continui 实现细节文档索引

本目录记录**当前代码实际如何工作**，不是愿景文档。架构宪法见 `docs/architecture.md`，决策依据见 `docs/design_decisions/` 与 `docs/decisions.md`，问题/审计闭环见 `09-traceability.md`。

## 阅读顺序（建议拆黑盒时按此走）

1. `08-end-to-end-frame.md` — 一帧从事件到显示的完整数据流（先建立全局图景）
2. `01-core.md` — 状态、窗口、事件、Arena、显式失效
3. `02-style.md` — 值类型样式与盒模型字段
4. `03-layout.md` — View 树与纯函数 Flex 求解器
5. `05-render.md` — 绘制批、Painter、UI 编排、文本 quad
6. `04-text.md` — FreeType/HarfBuzz/字形图集/strike 缓存
7. `06-backend-dawn.md` — WGSL 管线、上传、atlas 增长时的纹理替换
8. `07-platform.md` — Win32 窗口与事件泵
9. `09-traceability.md` — 历史缺陷、审计发现、修复、代码位置

## 文档约定

- 每份模块文档结构一致：职责边界 → 公共 API 地图 → 内部实现与算法 → 不变量 → 所有权/生命周期 → 线程 → 错误路径 → 已知限制 → 验证入口 → 溯源
- 所有代码锚点写成 `path:line`，行号以文档写作时的提交为准；重构后以符号名重新定位
- 已知问题写入对应模块的"已知限制"与 `09-traceability.md`，不要静默修改代码后失忆
- 状态词汇沿用 `docs/development.md`：Implemented / In Progress / Planned / Proposed / Experimental / Deprecated

## 当前实现状态快照（文档写作时）

| 阶段 | 状态 | 关键能力 |
|---|---|---|
| 0.1–0.3 | Implemented | CMake、Win32 窗口/事件、Dawn GPU/Surface |
| 0.4 | Implemented | Rect/Rounded/Texture/混合/scissor/合批 |
| 0.5 | Implemented | FreeType + HarfBuzz、glyph atlas、动态增长、gamma |
| 0.6 | Implemented（本地提交 `cb7c152`，未推送） | Arena/View/Flex/文本测量、命中、显式失效、Counter 闭环 |
| 0.7 | Planned | 键盘输入、Focus、Text Input、IME、fallback 链 |
| 0.8 | Planned | Key 匹配、Tree Diff、脏区分级、局部重算 |
