# 03 — layout：View 树与纯函数 Flex 求解器

## 职责边界

layout 把声明式 `View` 树转换成 `LayoutResult`（每个节点的盒子几何 + Key + 是否按钮），提供 `hit_test`。它是纯 CPU、无状态、无 GPU/平台/业务状态依赖；相同输入必须产生相同输出。文本测量通过 `TextMeasurer` 接口注入，layout 不链接 text。

## 公共 API 地图（`include/continui/layout.hpp`）

- `Constraints{min/max width/height}`：父级约束
- `LayoutBox{bounds, content, key, node_index, is_button}`：`bounds` = border box（含 padding，不含 margin），`content` = 内区
- `LayoutResult{boxes（前序遍历，下标 == node_index）, root_bounds, find(Key)}`
- `TextMeasurer::measure(text, font_size) const -> Size`：注入点
- `solve(view, constraints, measurer) -> LayoutResult`
- `hit_test(result, PointF) -> optional<node_index>`

## 内部实现

### 节点树（`src/layout/view_tree.hpp:11`）

`ViewNode`（arena 内）字段：`type{row,column,text,button,rect}`、`Key`、`Style`、`std::string text`、`Callback callback`、`ViewNode** children`（arena 数组，非拥有）、`child_count`、`index`（前序下标缓存）。`ViewTreeAccess` 是内部桥：`node(View)` / `wrap(ViewNode*)`；公共 `View` 不暴露树。组合子在 `view_tree.cpp:36–66`：`make()` 一次性填好节点字段。

### 求解器（`src/layout/layout_solver.cpp`）

两趟算法：

1. **测量**（`measure` `:77`，`measure_children` `:54`）
   - text/button：`TextMeasurer::measure` → 像素尺寸
   - 容器：主轴 = 子节点主轴之和 + gap；交叉轴 = 子节点交叉轴最大值
   - `apply_style_size` `:38`：显式 width/height 覆盖（border-box），否则加 padding；再按 min/max 与父约束钳制
2. **布置**（`place` `:242`，`place_children` `:130`）
   - `collect_preorder` `:103` 用显式栈给每个节点写 `index`，并生成与 boxes 同序的列表
   - 内容区 = `inset(bounds, padding)`（`:24`）
   - 主轴：`bases[]` = flex_basis 或测量值（flex_basis 按主轴选 min/max 字段，Column 用 height），`free = main_span - used`
   - `free > 0` 且有 grow → 按 `flex_grow` 比例分配（`:176`）；`free < 0` 且有 shrink → 按 `flex_shrink * basis` 比例收缩（`:178`）
   - 没有 grow 且 `free > 0` 时，`justify_content` 生效（`:186–200`）：center 偏移 free/2，end 偏移 free，space_between 把 free 均分到 gap 增量
   - 交叉轴：`align_items` 中 stretch 用整条交叉跨度，center/end 做偏移（`:201–211`）
   - 子节点 margin 计入容器固有尺寸（`measure_children`），并在分配到的槽位内再次内缩（`:213–222`）
   - 递归 `place()` 写 `LayoutBox`，容器继续布置子节点

`hit_test`（`:284`）逆序遍历前序 boxes → 最后一个包含点的盒子（即最深的节点）。`LayoutResult::find(Key)`（`:257`）线性查找。

## 不变量

1. `boxes.size() == 节点数` 且 `boxes[i].node_index == i`
2. `bounds` 不含 margin；margin 通过改变子节点槽位体现
3. `solve` 不修改 Style/业务状态；唯一写入是 `ViewNode::index` 缓存（纯函数性 caveat：树对象被写入缓存字段，但结果只取决于输入值）
4. 对同一 `(View 树, Constraints, Measurer)` 结果确定

## 所有权与生命周期

`solve` 只借用 View 树（arena 内）与 measurer（引用）；`LayoutResult` 是独立值类型（vector），可跨帧保存——但 0.6 的 UI 每帧重算，不复用旧结果。`hit_test` 使用上一帧的 LayoutResult（事件在 paint 前泵出）。

## 错误/边界处理

`gap < 0` 按 0；`width/height <= 0` 的盒子被 UI 绘制阶段跳过；`max` 为 `infinity` 时 `clamp_value` 的 `max(lo,hi)` 防非法区间；`total_grow == 0` 时才让 justify 分配 free；shrink 分母 `total_shrink` 为 0 时不进入分支（无除零）。

## 已知限制

- 只支持 Row/Column 的 Flex 子集；无 wrap、cross-axis reverse、auto margin、aspect ratio
- `align_items` 的 stretch 会覆盖子节点的显式交叉尺寸（如 Row 内显式 height 的按钮被拉伸到行高）
- `ViewNode::index` 写入让 `solve` 严格说来不是 const-pure；改为外部索引映射可消除
- 无 TextMeasure 缓存失效协议；字体/字号变化由 UI 整树重算兜底

## 验证入口

`tests/layout/layout_test.cpp`：Arena 生命周期、文字固有尺寸、max-width 钳制、column gap/padding、flex grow 平分、justify end + align center、hit test 命中按钮、invalidation 合并。求解器测试不依赖字体/GPU。

## 溯源

架构 §6（布局系统规范）、ADR-013（Flex 参考 Taffy，Proposed — 本实现为自写最小子集，未引入依赖）、0.6 提交 `cb7c152`。
