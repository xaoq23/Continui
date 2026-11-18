# text: fonts, shaping, glyph runs, glyph atlas (ADR-010). FreeType and
# HarfBuzz are vendored through FetchContent at pinned release tags, the
# same pattern as Dawn (ADR-009): pin + trim, no bundled copies.

include(FetchContent)

# ---- FreeType: font file parsing + glyph rasterization --------------------
# Trimmed: PNG (color bitmap glyphs), Brotli/Bzip2 (compressed font
# containers) and FreeType's own HarfBuzz linkage are all off the Phase 0.5
# path. Disabling HarfBuzz here also keeps FreeType 2.14+ from optionally
# dynamic-loading a system harfbuzz at runtime; shaping is our own harfbuzz
# target below (ADR-010: one shaping engine, statically ours). Zlib stays
# enabled (bundled fallback) so gzip/WOFF font files still open.
set(FT_DISABLE_ZLIB FALSE CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)

FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype
    GIT_TAG VER-2-15-0
    GIT_SHALLOW TRUE
)

# ---- HarfBuzz: text shaping -----------------------------------------------
# Trimmed: subset/tests/utils and every optional integration. Notably
# HB_HAVE_FREETYPE stays off — harfbuzz never links freetype here; font
# tables reach it through our own table callbacks (see shaper.cpp), which
# keeps the dependency direction one-way (text owns both, no linkage cycle).
# Raster/vector/gpu are 15.x add-on libraries, unused for shaping. (Tests
# are not a CMake option in 15.x.)
set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(HB_BUILD_RASTER OFF CACHE BOOL "" FORCE)
set(HB_BUILD_VECTOR OFF CACHE BOOL "" FORCE)
set(HB_BUILD_GPU OFF CACHE BOOL "" FORCE)
set(HB_BUILD_GPU_DEMO OFF CACHE STRING "" FORCE)
set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
set(HB_HAVE_ICU_BUILTIN OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
set(HB_HAVE_FREETYPE OFF CACHE BOOL "" FORCE)
# Introspection/gobject are off by default but emit a Windows-only warning
# when probed; pin them so both toolchains configure silently.
set(HB_HAVE_GOBJECT OFF CACHE BOOL "" FORCE)
set(HB_HAVE_INTROSPECTION OFF CACHE BOOL "" FORCE)

FetchContent_Declare(harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz
    GIT_TAG 15.1.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(freetype harfbuzz)

add_library(continui_text STATIC)
target_sources(continui_text PRIVATE
    font_face.cpp
    font_manager.cpp
    glyph_atlas.cpp
    glyph_cache.cpp
    shaper.cpp
)
target_compile_features(continui_text PUBLIC cxx_std_23)
# text -> core only (ADR-010): FreeType/HarfBuzz stay PRIVATE so no
# third-party header escapes into consumers of the public text API.
target_link_libraries(continui_text
    PUBLIC continui_core
    PRIVATE freetype harfbuzz
)
