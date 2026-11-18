#include "glyph_atlas.hpp"

#include <algorithm>

namespace continui::detail {

GlyphAtlas::GlyphAtlas(const int width, const int height)
    : width_(width), height_(height),
      pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0) {}

std::optional<GlyphAtlas::Cell> GlyphAtlas::insert(const int width,
                                                    const int height) {
    constexpr int kGutter = 1;  // transparent padding for linear sampling
    if (width <= 0 || height <= 0 || width > width_ || height > height_) {
        return std::nullopt;
    }
    const int cell_w = width + kGutter;
    const int cell_h = height + kGutter;
    if (cursor_x_ + cell_w > width_) {
        // Next shelf. A cell taller than the atlas never fits; that is
        // caught by the owner's size cap before insert is called.
        cursor_x_ = 0;
        shelf_y_ += shelf_h_;
        shelf_h_ = 0;
    }
    if (shelf_y_ + cell_h > height_) {
        return std::nullopt;  // full: the owner grows and rebuilds
    }
    const Cell cell{cursor_x_, shelf_y_};
    cursor_x_ += cell_w;
    shelf_h_ = std::max(shelf_h_, cell_h);
    return cell;
}

void GlyphAtlas::mark_dirty(const Rect pixel_rect) {
    if (pixel_rect.empty()) {
        return;
    }
    if (dirty_.empty()) {
        dirty_ = pixel_rect;
        return;
    }
    const float x0 = std::min(dirty_.x, pixel_rect.x);
    const float y0 = std::min(dirty_.y, pixel_rect.y);
    const float x1 = std::max(dirty_.right(), pixel_rect.right());
    const float y1 = std::max(dirty_.bottom(), pixel_rect.bottom());
    dirty_ = Rect{x0, y0, x1 - x0, y1 - y0};
}

Rect GlyphAtlas::take_dirty() {
    const Rect result = dirty_;
    dirty_ = Rect{};
    return result;
}

}  // namespace continui::detail
