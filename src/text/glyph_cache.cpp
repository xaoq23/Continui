#include "glyph_cache.hpp"

#include "font_face.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace continui::detail {

namespace {
constexpr int kInitialSize = 256;
constexpr int kMaxSize = 2048;  // Skia-sized ceiling; no eviction in 0.5
}  // namespace

GlyphCache::GlyphCache()
    : atlas_(std::make_unique<GlyphAtlas>(kInitialSize, kInitialSize)) {}

std::size_t GlyphCache::KeyHash::operator()(const Key& key) const noexcept {
    // Pointer and glyph id carry most of the entropy; combine with a 64-bit
    // mix so the size/bucket fields do not collide systematically.
    const std::uint64_t p = reinterpret_cast<std::uintptr_t>(key.face);
    const std::uint64_t mixed = (p ^ (p >> 4)) * 0x9e3779b97f4a7c15ULL
                                ^ (static_cast<std::uint64_t>(key.size_q) << 12)
                                ^ (static_cast<std::uint64_t>(key.bucket) << 8)
                                ^ key.glyph_id;
    return static_cast<std::size_t>(mixed ^ (mixed >> 32));
}

void GlyphCache::blit(GlyphAtlas& atlas, const GlyphAtlas::Cell& cell,
                      const GlyphBitmap& bitmap) {
    const std::size_t stride = static_cast<std::size_t>(atlas.width()) * 4;
    const std::size_t row_bytes = static_cast<std::size_t>(bitmap.width) * 4;
    for (int y = 0; y < bitmap.rows; ++y) {
        std::memcpy(atlas.pixels()
                        + static_cast<std::size_t>(cell.y + y) * stride
                        + static_cast<std::size_t>(cell.x) * 4,
                    bitmap.rgba.data() + static_cast<std::size_t>(y) * row_bytes,
                    row_bytes);
    }
}

GlyphCache::Entry GlyphCache::make_entry(const GlyphAtlas::Cell& cell,
                                         const GlyphBitmap& bitmap) noexcept {
    return Entry{Rect{static_cast<float>(cell.x), static_cast<float>(cell.y),
                      static_cast<float>(bitmap.width), static_cast<float>(bitmap.rows)},
                 bitmap.left, bitmap.top};
}

std::optional<GlyphCache::Entry> GlyphCache::glyph(const FontFace& face,
                                                   const float size_px,
                                                   const int x_bucket,
                                                   const std::uint32_t glyph_id) {
    // FontFace friends GlyphCache: the rasterizer needs its FaceImpl.
    if (face.impl_ == nullptr || !(size_px > 0.0f)) {
        return std::nullopt;
    }
    const Key key{&face,
                  static_cast<std::uint32_t>(std::lround(size_px * 4.0f)),
                  std::clamp(x_bucket, 0, 3), glyph_id};
    if (const auto found = cache_.find(key); found != cache_.end()) {
        return found->second.blank ? std::nullopt
                                   : std::optional<Entry>(found->second.entry);
    }

    // The strike is unique: rasterize once and cache both blank results and
    // failures so a missing glyph never re-rasterizes on every frame.
    const float phase = static_cast<float>(key.bucket) * 0.25f;
    const auto bitmap = rasterize_glyph(*face.impl_, glyph_id, size_px, phase);
    Cached cached;
    if (!bitmap || bitmap->empty()) {
        cached.blank = true;
        cache_.emplace(key, std::move(cached));
        return std::nullopt;
    }

    auto cell = atlas_->insert(bitmap->width, bitmap->rows);
    while (!cell) {
        // Flush pending pixels to the texture that earlier quads of this
        // frame already reference: grow() destroys this atlas and its dirty
        // rect, so waiting for the end-of-frame sync would lose them.
        if (before_growth_flush_) {
            before_growth_flush_();
        }
        if (!grow()) {
            return std::nullopt;  // no eviction in 0.5; capped atlas
        }
        cell = atlas_->insert(bitmap->width, bitmap->rows);
    }

    blit(*atlas_, *cell, *bitmap);
    atlas_->mark_dirty(Rect{static_cast<float>(cell->x), static_cast<float>(cell->y),
                            static_cast<float>(bitmap->width),
                            static_cast<float>(bitmap->rows)});
    cached.entry = make_entry(*cell, *bitmap);
    cached.bitmap = std::move(*bitmap);
    const auto [inserted, _] = cache_.emplace(key, std::move(cached));
    return inserted->second.entry;
}

bool GlyphCache::grow() {
    int width = atlas_->width();
    int height = atlas_->height();
    // Double the smaller side: shelf packing benefits from long shelves.
    if (height <= width && height < kMaxSize) {
        height *= 2;
    } else if (width < kMaxSize) {
        width *= 2;
    } else if (height < kMaxSize) {
        height *= 2;
    } else {
        return false;  // 2048x2048 cap reached
    }

    auto next = std::make_unique<GlyphAtlas>(width, height);
    // Two phases: if any glyph fails to re-pack, the old atlas and all
    // cached entries remain untouched (no half-migrated state).
    std::vector<std::pair<Cached*, GlyphAtlas::Cell>> placements;
    placements.reserve(cache_.size());
    for (auto& [key, cached] : cache_) {
        (void)key;
        if (cached.blank) {
            continue;
        }
        auto cell = next->insert(cached.bitmap.width, cached.bitmap.rows);
        if (!cell) {
            return false;
        }
        placements.emplace_back(&cached, *cell);
    }
    for (auto& [cached, cell] : placements) {
        blit(*next, cell, cached->bitmap);
        cached->entry = make_entry(cell, cached->bitmap);
    }
    // A rebuild replaces the GPU texture; mark everything dirty so the
    // backend's full-upload path stays consistent with its incremental one.
    next->mark_dirty(Rect{0.0f, 0.0f, static_cast<float>(width),
                          static_cast<float>(height)});
    atlas_ = std::move(next);
    ++generation_;
    return true;
}

}  // namespace continui::detail
