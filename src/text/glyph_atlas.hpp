// CPU-only glyph atlas packing (Phase 0.5, ADR-010). Pure data: no FreeType,
// no GPU types — unit-testable on its own. Pixels are RGBA8 straight white
// masks (255,255,255,coverage), matching the Phase 0.4 straight->premul
// shader path with zero shader changes for solid colors.
#pragma once

#include <continui/geometry.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace continui::detail {

class GlyphAtlas {
public:
    GlyphAtlas(int width, int height);

    struct Cell {
        int x = 0;
        int y = 0;
    };

    // Shelf-packs one w*h cell; nullopt when this atlas is full (the owner
    // then grows/rebuilds and re-inserts). Cells get a 1px transparent
    // gutter so linear-filtered glyph quads at fractional positions never
    // sample a neighboring glyph.
    [[nodiscard]] std::optional<Cell> insert(int width, int height);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] std::uint8_t* pixels() noexcept { return pixels_.data(); }
    [[nodiscard]] const std::uint8_t* pixels() const noexcept { return pixels_.data(); }

    // Incremental-upload bookkeeping: union of all changed pixel rects since
    // take_dirty() (empty when nothing changed).
    void mark_dirty(Rect pixel_rect);
    [[nodiscard]] Rect take_dirty();

private:
    int width_ = 0;
    int height_ = 0;
    int shelf_y_ = 0;    // top of the active shelf
    int shelf_h_ = 0;    // tallest cell on the active shelf (incl. gutter)
    int cursor_x_ = 0;   // next free x on the active shelf
    Rect dirty_{};
    std::vector<std::uint8_t> pixels_;
};

}  // namespace continui::detail
