# Dawn backend (ADR-009). Dawn is vendored through FetchContent at a pinned
# release tag; its own dependencies are fetched by Dawn's scripts at
# configure time (requires git and network). See
# docs/design_decisions/adr-009-dawn-backend.md.

include(FetchContent)

# GIT_SUBMODULES "" skips Dawn's 68 submodules (Chromium build/test infra,
# several GB). The FetchContent build path does not need them:
# tools/fetch_dawn_dependencies.py pulls the required third_party repos
# itself. CMP0097 NEW makes the empty list disable submodule init outright.
cmake_policy(SET CMP0097 NEW)

set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_PROTOBUF OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_IR_BINARY OFF CACHE BOOL "" FORCE)
# Tint's own toggles are not derived from DAWN_BUILD_TESTS; left alone, the
# default-on test / cmd-tool targets pull tint_lang_hlsl_validate into the
# default build, which needs ATL headers (atlbase.h) absent from a
# clang-only toolchain. None of these are on the runtime path (the WGSL and
# HLSL writers stay on).
set(TINT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_CMD_TOOLS OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_GLSL_VALIDATOR OFF CACHE BOOL "" FORCE)
# Load the OS-shipped d3dcompiler_47.dll from System32 (Windows Error 87
# verified for the default path: the SDK-copy lands next to Dawn's library
# output only, and our executables live in tests/ and examples/
# subdirectories the search never reaches).
set(DAWN_FORCE_SYSTEM_COMPONENT_LOAD ON CACHE BOOL "" FORCE)
set(DAWN_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_VULKAN OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_OPENGLES OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_DESKTOP_GL OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_METAL OFF CACHE BOOL "" FORCE)
set(DAWN_USE_X11 OFF CACHE BOOL "" FORCE)
set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)

FetchContent_Declare(dawn
    GIT_REPOSITORY https://github.com/google/dawn
    GIT_TAG v20261113.214538
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(dawn)

add_library(continui_backend_dawn STATIC)
target_sources(continui_backend_dawn PRIVATE
    dawn_renderer.hpp
    dawn_renderer.cpp
)
target_compile_features(continui_backend_dawn PUBLIC cxx_std_23)
# PRIVATE continui_platform: the renderer reads the native window handle
# through <continui/platform_windows.hpp> (backend -> platform, no cycle).
# PRIVATE continui_render: the renderer consumes the paint phase's batches
# (backend -> render, no cycle); the batcher header stays internal to it.
target_link_libraries(continui_backend_dawn
    PUBLIC continui_core dawn::webgpu_dawn
    PRIVATE continui_platform continui_render
)
# The batcher header is internal to render; the backend gets a private
# include path for it instead of render widening its own INTERFACE dirs.
target_include_directories(continui_backend_dawn PRIVATE
    "${CMAKE_SOURCE_DIR}/src/render"
)
