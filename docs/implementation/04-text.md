# 04 — text：FreeType / HarfBuzz / 字形图集 / strike 缓存

## 职责边界

text 负责：字体文件加载、UTF-8 shaping、字形光栅化、CPU 侧图集与 strike 缓存。它不依赖 GPU（GPU atlas 纹理与上传在 backend），不暴露 FT/hb 类型到公共头（`include/continui/font.hpp`、`glyph_run.hpp` 均无第三方 include）。

## 公共 API 地图

- `FontManager`：拥有 `FT_Library` 与所有 `FT_Face`；`load_file(path) -> expected<const FontFace*, FontError>`、`load_directory(dir) -> size_t`、`find(family, weight) -> const FontFace*`
- `FontFace`：非拷贝非移动，由 manager 拥有；`shape(utf8, size_px) -> GlyphRun`、`metrics(size_px) -> TextMetrics`、`family()/weight()`
- `GlyphRun{face*, size_px, glyphs[id,advance,offset_x,offset_y], width}`：值是 y-down 浮点像素；face 必须在绘制时仍存活
- `TextMetrics{ascent, descent, line_gap}`：正距离，`line_height = ascent+descent+line_gap`

## 内部实现

### FaceImpl（`src/text/font_face.hpp:13`、`font_face.cpp:9`）

`FT_Library`（借用 manager 的）+ `FT_Face` + 懒创建的 `hb_face_t*` + family/bold/upem。析构顺序：`hb_face_destroy` → `FT_Done_Face`；manager 在 faces 清空后才 `FT_Done_Library`（`font_manager.cpp:41`）。

`harfbuzz_face()`（`font_face.cpp:25`）用 `hb_face_create_for_tables` + `FT_Load_Sfnt_Table` 回调读取 cmap/hmtx/GPOS 等表：每次调用把表字节 `malloc` 复制进 hb 自己拥有的 blob，避免悬空。

### Shaper（`src/text/shaper.cpp`）

- `reference_table()` `:11`：表查询 → malloc 副本 → `hb_blob_create(HB_MEMORY_MODE_READONLY, free)`；表不存在返回空 blob
- `make_shaping_face()` `:35`：`hb_face_create_for_tables` + `hb_face_set_upem(FT face->units_per_EM)`
- `shape_text()` `:38`：`hb_buffer_add_utf8` → `guess_segment_properties`（拉丁→LTR）→ `hb_font_set_scale(font, upem, upem)` → `hb_shape`。因为 scale = upem，hb 位置量就是**字体单位**，最后统一乘 `size_px/upem` 转像素；HarfBuzz 的 y-up offset 在此翻转（`-y`）

### 光栅化（`src/text/font_face.cpp:32`）

`rasterize_glyph(face, gid, size_px, phase_x)`：
1. `FT_Set_Char_Size(0, round(size_px*64), 0, 0)` 支持小数 ppem
2. `FT_Set_Transform(face, nullptr, delta={round(phase_x*64),0})` 把轮廓按 x 次像素相位平移
3. `FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP | FT_LOAD_NO_SVG` 加载并 `FT_Render_Glyph(..., NORMAL)`
4. `slot->bitmap_left - phase_x` 作为 pen→bitmap 左距（扣除注入的相位），`bitmap_top` 作 baseline→上沿距离
5. 灰度像素展开成 RGBA `(255,255,255,coverage)` —— 与 0.4 的 straight→premul 管线无缝衔接
6. 空白字形返回空 bitmap；失败返回 nullopt；`size_px > 512` 直接拒绝

### GlyphAtlas（`src/text/glyph_atlas.cpp`）

shelf packer：`insert(w,h)`（`:11`）按行放置，单元格额外 +1px 透明 gutter（线性采样时防邻字串色）；`mark_dirty/take_dirty`（`:35/:50`）维护脏像素并集，供增量上传。像素缓冲是 `vector<uint8_t>`（RGBA8，零初始化）。

### GlyphCache（`src/text/glyph_cache.cpp`）

- strike key = `{face*, lround(size_px*4), bucket 0..3, glyph_id}`；同 key 命中即返回旧 placement（`:52`）
- miss → 光栅化 → 空白/失败也缓存（避免空格每帧重新光栅化）→ `atlas_->insert` → blit + mark_dirty → 返回 `{uv_px, left, top}`
- 图集满 → 调用 `before_growth_flush_()`（**增长前**把旧图集脏像素上传到旧纹理）→ `grow(w,h)` 一次重建到能容纳该字形的尺寸（`:110`，每边上限 2048）→ 重新 pack 所有缓存 bitmap → `generation_++`
- 缓存条目保留 bitmap 副本：增长重排不依赖 face 仍存活
- `grow` 用 do-while：至少翻倍一次（图集"满"也要增长），再循环到目标 cell 能放下

## 不变量

1. 同一 strike 只光栅化一次（除非图集增长导致重新 pack，但也不重新光栅化）
2. atlas gutter 恒为 1px 透明；UV 以像素边界给出（后端归一化）
3. 增长只有一次 `generation_++`，且 flush 回调在销毁旧 atlas 之前调用
4. 空白 glyph 的 slot 为空 → batcher 跳过 quad，不占图集空间
5. strike key 中的 face 指针必须在渲染器生命周期内有效（文档化契约）

## 所有权与生命周期

`FontManager` ≫ `FontFace*` ≫ `GlyphRun`（非拥有引用）与 `GlyphCache` key。后端 `GlyphCache` 按 face 指针建 key；应用若在渲染器之前销毁 FontManager 会造成悬空 key —— 契约要求两者同生命周期（示例满足）。hb blob 由 HarfBuzz 持有 malloc 副本，生命周期独立于 FT_Face。

## 线程

主线程专用：FT 不是线程安全的；shaping/rasterization 全在 paint 回调内。

## 错误路径

- `FT_Init_FreeType` 失败 → manager 的 library=null，load_file 返回 `io_error`（无异常）
- 字体文件不存在 → `file_not_found`；格式错误 → `unsupported_format`
- 表读取失败 → 空 hb_blob（该表特性失效但不崩溃）
- 光栅化失败 / 字形过大 / 图集封顶（2048）→ 返回空 slot，跳过该 glyph

## 已知限制

- 字体路径仅 ASCII（`FT_New_Face` 的 char* 接口）；TTC 只取第 0 个 face
- 无 fallback 链（缺字显示 .notdef）；无色彩字形 / LCD 亚像素 / 双向文本 / 复杂脚本
- 图集无淘汰，封顶 2048；与 0.4 共用 16384 quad/帧的 uint16 索引上限
- `size_px` 量化到 1/4 px 作为 key，但光栅化用原始 float（两个相近 size 可能共用第一次的位图）

## 验证入口

`tests/text/text_test.cpp`：系统字体加载、shaping glyph 数/advance/缩放、metrics、目录枚举、图集增长（generation/尺寸/无重叠）、512px 单字形一次增长一次 flush。后端测试的文本块会实际触发增长并断言无 GPU 错误。

## 溯源

ADR-010（Accepted）：RGBA 白 mask、strike key、FreeType-only、gamma、subpixel 4 分桶、无淘汰。审计历史见 `09-traceability.md`（0.5 的两次崩溃级缺陷与残留多增长问题）。
