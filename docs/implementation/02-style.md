# 02 — style：值类型样式与盒模型字段

## 职责边界

style 提供纯值类型：`Color`、`Edges`、`Align`、`Justify`、`Style`。不解析 CSS、不做单位换算、不做继承/主题（这些是 Phase 1）。所有尺寸都是**浮点像素**，整数逻辑单位尚未引入。

## 公共 API 地图

`include/continui/style.hpp`：

| 类型 | 字段 | 语义 |
|---|---|---|
| `Color` | r/g/b/a + `rgb()/rgba()` | **非预乘** sRGB 字节；batch 内部转预乘浮点 |
| `Edges` | left/top/right/bottom + `horizontal()/vertical()` | padding/margin 四边像素值（定义在 `geometry.hpp`） |
| `Align` | start/center/end/stretch | 交叉轴对齐 |
| `Justify` | start/center/end/space_between | 主轴剩余空间分配 |
| `Style` | background/foreground | 填充色/文字色（非预乘） |
| `Style` | width/height | < 0 表示 auto（测量内容）；>= 0 视为 **border-box** 尺寸 |
| `Style` | min_width/min_height/max_width/max_height | 解析后盒子的钳制区间（max 默认 `infinity`） |
| `Style` | padding/margin/gap | 盒内边距 / 盒外间距 / 子节点间隔 |
| `Style` | flex_grow/flex_shrink/flex_basis | 主轴伸缩；basis < 0 表示用测量值 |
| `Style` | align_items/justify_content | 容器对其子节点的布置 |
| `Style` | font_size/radius | 文本字号 / 绘制圆角半径 |

`operator==` 覆盖所有字段，便于测试与将来的样式去重。

## 内部实现

没有运行时多态、没有样式表、没有 StyleResolver。`ViewNode`（`src/layout/view_tree.hpp`）按值持有一个 `Style`，求解器和 UI 绘制代码直接读字段。组合子的显式 Key 重载不改变样式处理。

## 不变量

1. `width/height >= 0` 是最终 border-box 尺寸，求解器不再叠加 padding（`layout_solver.cpp:87` 附近）
2. `width/height < 0` 时 auto 尺寸 = 内容尺寸 + padding（同处）
3. `Color` 在进入 batcher 前始终是非预乘；预乘只发生在 `BatchBuilder::push_quad/text`（`src/render/batcher.cpp:112` 附近）
4. 所有 Style 字段都是值，复制/赋值不共享状态

## 错误/边界处理

`max_width/max_height` 默认 `infinity`，`clamp_value(value, lo, hi)` 用 `std::clamp(value, lo, max(lo,hi))` 防御 lo > hi 的非法输入（`layout_solver.cpp:17`）。gap 为负时按 0 处理。

## 已知限制

- 无 `Length` 单位、百分比、auto margin、inherit/theme

## 验证入口

`tests/layout/layout_test.cpp` 的 column gap/padding、flex grow、justify/align 用例；`Style` 默认值由 `geometry.hpp`/`style.hpp` 的常量直接给出。

## 溯源

架构 §5（样式系统规范）明确 Phase 1 才做完整值类型设计；0.6 只取布局必需子集（`cb7c152`）。
