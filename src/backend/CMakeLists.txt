# backend: gfx device abstraction over the chosen GPU API.
# Phase 0.3 ships the Dawn renderer (ADR-009). The backend target is opt-in
# (not part of the umbrella): consumers without GPU needs do not pay for it.
# The Dawn FetchContent is heavy (vendored dependency + its own third-party
# fetch), so the option defaults OFF and the clang presets turn it on
# (ADR-020: clang is the primary toolchain). Non-Windows builds get an
# interface stub until a second platform exists.
option(CONTINUI_BUILD_BACKEND_DAWN "Build the Dawn backend (Windows/D3D12 first)" OFF)

if(CONTINUI_BUILD_BACKEND_DAWN AND WIN32)
    add_subdirectory(dawn)
else()
    add_library(continui_backend_dawn INTERFACE)
    target_compile_features(continui_backend_dawn INTERFACE cxx_std_23)
endif()
