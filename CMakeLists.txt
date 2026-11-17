cmake_minimum_required(VERSION 3.25)

project(continui
    VERSION 0.1.0
    DESCRIPTION "Function-driven layout, GPU-rendered, Chromium-free, library-first."
    LANGUAGES CXX
)

# Global language settings: C++23 baseline, no compiler extensions.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile command database for clangd by default (developer convenience).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Project-wide warning policy (GCC/Clang only; MSVC flags land with its first
# portability pass). Interface targets have no compile units today, but this
# keeps every future code target consistent without per-target repetition.
add_compile_options(
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wextra>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wpedantic>
)


# ---- Modules (interface targets until Phase 0 fills in implementations) ----

add_subdirectory(src/core)
add_subdirectory(src/style)
add_subdirectory(src/layout)
add_subdirectory(src/render)
add_subdirectory(src/platform)

# The umbrella target users link against. render/platform are excluded until
# their first real implementation lands (Phase 0.2 platform, Phase 0.3
# backend); until then they carry no public API a consumer could use.
add_library(continui INTERFACE)
target_link_libraries(continui INTERFACE
    continui_core
    continui_style
    continui_layout
)
target_compile_features(continui INTERFACE cxx_std_23)

# ---- Tests ----

enable_testing()
add_subdirectory(tests)

# ---- Examples ----

add_subdirectory(examples)
