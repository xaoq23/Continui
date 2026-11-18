// Text unit tests (Phase 0.5): pure-CPU atlas packing, shaping, metrics,
// font lookup, and strike-cache growth. Runs on both toolchains. A missing
// system font skips the FT/HB-dependent cases loudly (never silently green).
#include <continui/font.hpp>

#include "glyph_atlas.hpp"
#include "glyph_cache.hpp"

#include <array>
#include <filesystem>
#include <cstdint>
#include <initializer_list>
#include <print>
#include <string>
#include <string_view>
#include <vector>

using continui::detail::GlyphAtlas;
using continui::detail::GlyphCache;

namespace {

int failures = 0;

void check(const bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::println(stderr, "FAIL: {}", what);
    }
}

bool approx(const float got, const float want, const float eps = 1e-4f) {
    return std::fabs(got - want) <= eps;
}

bool overlaps(const continui::Rect& a, const continui::Rect& b) {
    return a.x < b.right() && a.right() > b.x && a.y < b.bottom()
           && a.bottom() > b.y;
}

std::filesystem::path system_font_path() {
#ifdef _WIN32
    return "C:/Windows/Fonts/segoeui.ttf";
#else
    return "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
}

void test_atlas_packer() {
    GlyphAtlas atlas{64, 64};
    std::vector<continui::Rect> cells;
    for (int i = 0; i < 20; ++i) {
        const auto cell = atlas.insert(8, 10);
        check(cell.has_value(), "small glyph cells fit the atlas");
        if (cell) {
            cells.push_back({static_cast<float>(cell->x), static_cast<float>(cell->y),
                             8.0f, 10.0f});
        }
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        check(cells[i].x >= 0 && cells[i].right() <= atlas.width()
              && cells[i].y >= 0 && cells[i].bottom() <= atlas.height(),
              "cell lies inside the atlas");
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            check(!overlaps(cells[i], cells[j]), "packed cells never overlap");
        }
    }

    atlas.mark_dirty({2, 3, 4, 5});
    atlas.mark_dirty({20, 10, 8, 8});
    const continui::Rect dirty = atlas.take_dirty();
    check(dirty == continui::Rect{2, 3, 26, 15}, "dirty rects union");
    check(atlas.take_dirty().empty(), "take_dirty resets the tracker");

    GlyphAtlas small{32, 32};
    int placed = 0;
    while (small.insert(6, 6)) {
        ++placed;
    }
    check(placed > 0 && placed < 100, "a full atlas rejects further cells");
}

void test_fonts_and_growth() {
    const auto font_path = system_font_path();
    if (!std::filesystem::exists(font_path)) {
        std::println("SKIP: system font {} not found; FT/HB cases skipped",
                     font_path.string());
        return;
    }

    using namespace continui;
    FontManager manager;
    const auto loaded = manager.load_file(font_path);
    check(loaded.has_value(), "system font loads through FreeType");
    if (!loaded) {
        return;
    }
    const FontFace* face = *loaded;
    check(!face->family().empty(), "face reports a family name");
    check(face->weight() == FontWeight::normal || face->weight() == FontWeight::bold,
          "face reports a valid binary weight");

    const GlyphRun run = face->shape("Hello, World!", 16.0f);
    check(run.face == face && run.size_px == 16.0f, "run carries face and size");
    check(run.glyphs.size() == 13, "one glyph per simple latin character");
    float sum = 0.0f;
    bool advances_positive = true;
    for (const Glyph& glyph : run.glyphs) {
        advances_positive = advances_positive && glyph.advance > 0.0f;
        sum += glyph.advance;
    }
    check(advances_positive, "shaped advances are positive");
    check(approx(run.width, sum), "run width is the sum of advances");
    const GlyphRun larger = face->shape("Hello, World!", 32.0f);
    check(std::fabs(larger.width - run.width * 2.0f) < 0.5f,
          "doubling the size roughly doubles the advance width");

    const TextMetrics metrics = face->metrics(16.0f);
    check(metrics.ascent > 0.0f && metrics.descent > 0.0f,
          "ascent/descent are positive distances");
    check(metrics.ascent + metrics.descent + metrics.line_gap > 20.0f,
          "line height exceeds 20px at 16px size");

    const auto bad = manager.load_file(font_path.parent_path() / "no-such-font.ttf");
    check(!bad.has_value() && bad.error() == FontError::file_not_found,
          "missing font file reports file_not_found");
    const std::size_t count = manager.load_directory(font_path.parent_path());
    check(count > 0, "system font directory registers faces");
    check(manager.find("Definitely Not A Font Family", FontWeight::normal) == nullptr,
          "unknown family lookup returns nullptr");

    // Strike cache growth: many distinct (size, bucket, glyph) strikes at
    // 16..24px overflow the 256x256 initial atlas and force a rebuild.
    GlyphCache cache;
    check(cache.generation() == 0 && cache.atlas().width() == 256,
          "strike cache starts with a 256px atlas");
    std::vector<std::uint32_t> ids;
    for (const char c : std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZ")) {
        const GlyphRun one = face->shape(std::string_view(&c, 1), 16.0f);
        check(!one.glyphs.empty(), "ASCII glyph resolves in the font");
        if (!one.glyphs.empty()) {
            ids.push_back(one.glyphs.front().id);
        }
    }
    std::vector<Rect> placements;
    std::uint64_t generation = cache.generation();
    for (int size = 16; size <= 24; ++size) {
        for (int bucket = 0; bucket < 4; ++bucket) {
            for (const std::uint32_t id : ids) {
                const auto entry = cache.glyph(*face, static_cast<float>(size),
                                               bucket, id);
                if (cache.generation() != generation) {
                    // Positions recorded before a rebuild belong to the old
                    // atlas; only compare cells from the same generation.
                    generation = cache.generation();
                    placements.clear();
                }
                if (entry) {
                    placements.push_back(entry->uv_px);
                }
            }
        }
    }
    check(cache.generation() >= 1, "enough strikes force an atlas rebuild");
    check(cache.atlas().width() > 256 || cache.atlas().height() > 256,
          "growth enlarges the atlas");
    for (const Rect& rect : placements) {
        check(rect.x >= 0 && rect.right() <= cache.atlas().width()
              && rect.y >= 0 && rect.bottom() <= cache.atlas().height(),
              "re-packed cells stay inside the grown atlas");
    }
    for (std::size_t i = 0; i < placements.size(); ++i) {
        for (std::size_t j = i + 1; j < placements.size(); ++j) {
            check(!overlaps(placements[i], placements[j]),
                  "re-packed cells still never overlap");
        }
    }
    // A single large glyph must force exactly one rebuild/flush. Multiple
    // rebuilds inside one glyph() call would make the backend flush a newer
    // atlas size into the still-registered older texture (GPU validation).
    GlyphCache big_cache;
    int flushes = 0;
    big_cache.set_before_growth_flush([&flushes] { ++flushes; });
    const auto big = big_cache.glyph(*face, 512.0f, 0, ids.front());
    check(big.has_value(), "a 512px glyph fits the capped atlas");
    check(big_cache.generation() == 1 && flushes == 1,
          "one large glyph triggers exactly one rebuild and one flush");

    // A repeated lookup hits the strike cache with identical placement.
    const auto first = cache.glyph(*face, 16.0f, 0, ids.front());
    const auto again = cache.glyph(*face, 16.0f, 0, ids.front());
    check(first.has_value() && again.has_value()
          && first->uv_px == again->uv_px,
          "cached strike keeps a stable placement");
}

}  // namespace

void test_fallback() {
#ifdef _WIN32
    const std::filesystem::path latin_path = "C:/Windows/Fonts/segoeui.ttf";
    const std::filesystem::path cjk_path = "C:/Windows/Fonts/msyh.ttc";
    if (!std::filesystem::exists(latin_path) || !std::filesystem::exists(cjk_path)) {
        std::println("SKIP: fallback test needs segoeui.ttf + msyh.ttc");
        return;
    }
    continui::FontManager fonts;
    const auto latin = fonts.load_file(latin_path);
    const auto cjk = fonts.load_file(cjk_path);
    if (!latin || !cjk) {
        check(false, "fallback test fonts load");
        return;
    }
    check(!(*latin)->covers(U'中'), "latin face does not cover CJK");
    check((*cjk)->covers(U'中'), "CJK fallback face covers CJK");

    const std::array<const continui::FontFace* const, 1> fallbacks{*cjk};
    const continui::GlyphRun run = continui::shape_with_fallback(
        *latin, fallbacks, "Hi中", 24.0f);
    check(run.glyphs.size() == 3, "fallback run has one glyph per codepoint");
    check(run.glyphs.size() == 3 && run.glyphs[0].face == *latin
          && run.glyphs[1].face == *latin,
          "latin codepoints keep the primary face");
    check(run.glyphs.size() == 3 && run.glyphs[2].face == *cjk
          && run.glyphs[2].id != 0,
          "CJK codepoint uses the fallback face with a real glyph");
    check(run.width > 0.0f, "fallback run has a positive total advance");

    // The batcher accumulates pen_x from glyph.advance itself. If fallback
    // folds the running pen into offset_x, spacing doubles ("42" -> "4 2",
    // text overflows boxes). Assert per-glyph offsets stay font-local by
    // comparing against direct shaping of the same latin text.
    const continui::GlyphRun fallback_latin = continui::shape_with_fallback(
        *latin, fallbacks, "Hi", 24.0f);
    const continui::GlyphRun direct_latin = (*latin)->shape("Hi", 24.0f);
    check(fallback_latin.glyphs.size() == direct_latin.glyphs.size(),
          "fallback run keeps one glyph per shaped codepoint");
    check(fallback_latin.glyphs.size() == 2
          && std::fabs(fallback_latin.glyphs[1].offset_x
                       - direct_latin.glyphs[1].offset_x) < 1e-3f,
          "fallback does not double-count advances in glyph offsets");
    check(fallback_latin.glyphs.size() == 2
          && std::fabs((fallback_latin.glyphs[0].advance
                        + fallback_latin.glyphs[1].advance)
                       - fallback_latin.width) < 1e-3f,
          "fallback width equals the sum of glyph advances");
#else
    std::println("SKIP: fallback test uses Windows system fonts");
#endif
}

int main() {
    test_atlas_packer();
    test_fonts_and_growth();
    test_fallback();
    if (failures == 0) {
        std::println("text tests passed");
    }
    return failures == 0 ? 0 : 1;
}
