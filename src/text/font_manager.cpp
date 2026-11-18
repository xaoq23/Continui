#include <continui/font.hpp>

#include "font_face.hpp"
#include "shaper.hpp"

#include <ft2build.h>
#include FT_MODULE_H  // FT_Done_Library (separate from the core FT header)

#include <algorithm>
#include <cctype>
#include <cmath>

namespace continui {

namespace {

bool same_family(std::string_view lhs, std::string_view rhs) noexcept {
    return lhs.size() == rhs.size()
           && std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                         [](const char a, const char b) {
                             return std::tolower(static_cast<unsigned char>(a))
                                    == std::tolower(static_cast<unsigned char>(b));
                         });
}

bool font_extension(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    auto lower = [](const char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    std::string normalized;
    normalized.reserve(ext.size());
    std::transform(ext.begin(), ext.end(), std::back_inserter(normalized), lower);
    return normalized == ".ttf" || normalized == ".otf" || normalized == ".ttc";
}

}  // namespace

// FontManager's pimpl: the FreeType library must outlive every face, so
// faces are destroyed first here (declaration order + clear()).
struct FontManager::Impl {
    FT_Library library = nullptr;
    std::vector<std::unique_ptr<FontFace>> faces;

    ~Impl() {
        faces.clear();
        if (library != nullptr) {
            FT_Done_Library(library);
        }
    }
};

FontFace::FontFace(std::unique_ptr<detail::FaceImpl> impl) noexcept
    : impl_(std::move(impl)) {}

GlyphRun FontFace::shape(const std::string_view utf8, const float size_px) const {
    if (impl_ == nullptr) {
        return {};
    }
    impl_->harfbuzz_face();  // lazy table-backed shaping face
    return detail::shape_text(*this, *impl_, utf8, size_px);
}

TextMetrics FontFace::metrics(const float size_px) const {
    if (impl_ == nullptr || size_px <= 0.0f) {
        return {};
    }
    if (FT_Set_Char_Size(impl_->face, 0,
                         static_cast<FT_F26Dot6>(std::lround(size_px * 64.0f)),
                         0, 0) != 0) {
        return {};
    }
    // FreeType's size metrics are 26.6 fixed; descender is negative in
    // font-space (y up), returned here as a positive distance downward.
    const FT_Size_Metrics& m = impl_->face->size->metrics;
    TextMetrics result;
    result.ascent = static_cast<float>(m.ascender) / 64.0f;
    result.descent = -static_cast<float>(m.descender) / 64.0f;
    result.line_gap = std::max(0.0f, static_cast<float>(m.height) / 64.0f
                                     - result.ascent - result.descent);
    return result;
}

std::string_view FontFace::family() const noexcept {
    return impl_ != nullptr ? std::string_view(impl_->family) : std::string_view{};
}

FontWeight FontFace::weight() const noexcept {
    return impl_ != nullptr && impl_->bold ? FontWeight::bold : FontWeight::normal;
}

FontManager::FontManager() : impl_(std::make_unique<Impl>()) {
    // A failed init (extreme OOM) leaves a manager whose loads fail; no
    // exception is thrown for a condition callers must handle anyway.
    FT_Init_FreeType(&impl_->library);
}

FontManager::~FontManager() = default;

std::expected<const FontFace*, FontError>
FontManager::load_file(const std::filesystem::path& path) {
    if (impl_->library == nullptr) {
        return std::unexpected(FontError::io_error);
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return std::unexpected(FontError::file_not_found);
    }
    FT_Face face = nullptr;
    // Phase 0.5: ASCII paths (Windows system font dir). Unicode paths need
    // a wide-to-FT_New_Memory_Face bridge in a later platform phase.
    if (FT_New_Face(impl_->library, path.string().c_str(), 0, &face) != 0
        || face == nullptr) {
        return std::unexpected(FontError::unsupported_format);
    }
    auto impl = std::make_unique<detail::FaceImpl>(impl_->library, face);
    auto* public_face = new FontFace(std::move(impl));  // private ctor: FontManager is a friend
    impl_->faces.emplace_back(public_face);
    return public_face;
}

std::size_t FontManager::load_directory(const std::filesystem::path& directory) {
    std::size_t loaded = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || !font_extension(entry.path())) {
            continue;
        }
        if (load_file(entry.path()).has_value()) {
            ++loaded;
        }
    }
    return loaded;
}

const FontFace* FontManager::find(const std::string_view family,
                                  const FontWeight weight) const noexcept {
    for (const auto& face : impl_->faces) {
        if (face->weight() == weight && same_family(face->family(), family)) {
            return face.get();
        }
    }
    return nullptr;
}

}  // namespace continui
