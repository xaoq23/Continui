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

# Project-wide warning policy. MSVC gets /W4 and /utf-8 since Phase 0.2
# (Win32 backend); deeper MSVC-specific passes remain for later phases.
add_compile_options(
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wextra>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)


# ---- Modules (interface targets until Phase 0 fills in implementations) ----

add_subdirectory(src/core)
add_subdirectory(src/style)
add_subdirectory(src/layout)
add_subdirectory(src/render)
add_subdirectory(src/text)
add_subdirectory(src/backend)
add_subdirectory(src/platform)

# The umbrella target users link against. The platform module joined in
# Phase 0.2 (Win32). The Dawn backend (Phase 0.3) is deliberately NOT in the
# umbrella: it is opt-in, so consumers without GPU needs do not pay for Dawn.
# The opt-in Dawn backend needs no umbrella entry: it is opt-in, so consumers
# without GPU needs do not pay for Dawn. The render module (Painter) joins
# here — without it, a consumer linking only `continui` gets undefined Painter
# symbols; examples currently mask that because backend_dawn PRIVATE-links
# continui_render. text joins with its Phase 0.5 implementation.
add_library(continui INTERFACE)
target_link_libraries(continui INTERFACE
    continui_core
    continui_style
    continui_layout
    continui_render
    continui_platform
    continui_text
)
target_compile_features(continui INTERFACE cxx_std_23)

# ---- Tests ----

enable_testing()
add_subdirectory(tests)

# ---- Examples ----

add_subdirectory(examples)
