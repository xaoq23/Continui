// Strike cache + atlas owner (Phase 0.5, ADR-010). Pure CPU: rasterizes
// through FontFace on a miss, packs into GlyphAtlas, tracks dirty pixels for
// incremental GPU upload. The backend owns one instance and turns entries
// into normalized UVs; no GPU types appear here.
#pragma once

#include "glyph_bitmap.hpp"
#include "glyph_atlas.hpp"

#include <continui/font.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

namespace continui::detail {

class GlyphCache {
public:
    // A cached rasterization's placement. uv_px is in atlas pixels; the GPU
    // owner normalizes it by the atlas size when building text quads.
    struct Entry {
        Rect uv_px{};
        float left = 0;  // pen -> bitmap left, px (phase compensated)
        float top = 0;   // baseline -> bitmap top, px
    };

    GlyphCache();

    // Resolves (face, size, x-bucket, glyph) to a placement, rasterizing and
    // packing on a miss. Empty for blank glyphs, rasterization failures, or
    // glyphs too large for the (capped) atlas. May rebuild/grow the atlas on
    // a miss — callers must re-read generation() after any call.
    [[nodiscard]] std::optional<Entry> glyph(const FontFace& face, float size_px,
                                             int x_bucket,
                                             std::uint32_t glyph_id);

    [[nodiscard]] GlyphAtlas& atlas() noexcept { return *atlas_; }
    [[nodiscard]] const GlyphAtlas& atlas() const noexcept { return *atlas_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

    // Called synchronously just before a growth rebuild destroys the old
    // atlas. The GPU owner uses it to upload that atlas's pending dirty
    // pixels to the texture that earlier quads of this frame still sample.
    // No call when no growth happens.
    void set_before_growth_flush(std::function<void()> callback) {
        before_growth_flush_ = std::move(callback);
    }

private:
    struct Key {
        const FontFace* face = nullptr;
        std::uint32_t size_q = 0;  // size_px * 4, rounded (stable key across call sites)
        int bucket = 0;
        std::uint32_t glyph_id = 0;

        bool operator==(const Key& other) const noexcept {
            return face == other.face && size_q == other.size_q
                   && bucket == other.bucket && glyph_id == other.glyph_id;
        }
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept;
    };

    struct Cached {
        Entry entry{};
        GlyphBitmap bitmap;  // retained so growth can re-pack without the face
        bool blank = false;
    };

    // Grows the smaller dimension first (shelves prefer wide atlases), up
    // to 2048 per side; re-inserts every cached bitmap. False when capped.
    bool grow();
    static void blit(GlyphAtlas& atlas, const GlyphAtlas::Cell& cell,
                     const GlyphBitmap& bitmap);
    static Entry make_entry(const GlyphAtlas::Cell& cell,
                            const GlyphBitmap& bitmap) noexcept;

    std::unique_ptr<GlyphAtlas> atlas_;
    std::unordered_map<Key, Cached, KeyHash> cache_;
    std::uint64_t generation_ = 0;
    std::function<void()> before_growth_flush_;
};

}  // namespace continui::detail
