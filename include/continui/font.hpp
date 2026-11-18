// Font loading and shaping entry points (Phase 0.5, ADR-010).
//
// FreeType and HarfBuzz stay entirely behind the pimpl; this header has no
// third-party includes. FontManager owns an FT_Library and all faces it
// loads; callers hold stable const references to faces (never owning them),
// which is why FontFace is neither copyable nor movable.
#pragma once

#include <continui/glyph_run.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace continui {

namespace detail {
struct FaceImpl;       // FreeType face + HarfBuzz table accessor
class GlyphCache;      // text-internal consumer of rasterization
}  // namespace detail

// Phase 0.5 selects binary weights of one family; variable axes are later.
enum class FontWeight { normal, bold };

// Face-level vertical metrics at a pixel size. All values are positive
// distances in pixels: ascent goes above the baseline, descent below it,
// line_gap is extra leading. line_height = ascent + descent + line_gap.
struct TextMetrics {
    float ascent = 0;
    float descent = 0;
    float line_gap = 0;
};

enum class FontError { file_not_found, unsupported_format, io_error };

class FontFace {
public:
    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;

    // Shapes UTF-8 text at size_px. Phase 0.5 scope: one latin LTR run, no
    // font fallback (missing characters resolve to the face's .notdef).
    [[nodiscard]] GlyphRun shape(std::string_view utf8, float size_px) const;

    [[nodiscard]] TextMetrics metrics(float size_px) const;

    // Names/flags reported by the font itself. The string view is valid for
    // the face's lifetime.
    [[nodiscard]] std::string_view family() const noexcept;
    [[nodiscard]] FontWeight weight() const noexcept;

private:
    friend class FontManager;
    friend class detail::GlyphCache;  // rasterizes cached glyphs via FaceImpl
    explicit FontFace(std::unique_ptr<detail::FaceImpl> impl) noexcept;

    std::unique_ptr<detail::FaceImpl> impl_;
};

class FontManager {
public:
    FontManager();
    ~FontManager();
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    // Loads the first face of a font file. The face lives as long as this
    // manager; ids/family names come from the font itself.
    [[nodiscard]] std::expected<const FontFace*, FontError>
    load_file(const std::filesystem::path& path);

    // Registers every loadable font file in a directory (first face each).
    // System directories contain stale/broken files; those are skipped, not
    // errors. Returns how many faces were added; calling it twice re-scans
    // and may add duplicate families.
    std::size_t load_directory(const std::filesystem::path& directory);

    // Lookup by the font's own family name and weight; nullptr when absent.
    [[nodiscard]] const FontFace* find(std::string_view family,
                                       FontWeight weight) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace continui
