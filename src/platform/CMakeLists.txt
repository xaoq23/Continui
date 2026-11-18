# platform: windows, input, IME, DPI, clipboard.
# Phase 0.2 ships the Win32 backend (App/Window lifecycle, event pump).
# Further platform backends arrive in Phase 2; this file is the only place
# that selects them. Public API must never expose Win32 types.

if(WIN32)
    add_library(continui_platform STATIC)
    target_sources(continui_platform PRIVATE
        windows/internal.hpp
        windows/platform_app.cpp
        windows/platform_window.cpp
    )
    target_compile_features(continui_platform PUBLIC cxx_std_23)
    target_link_libraries(continui_platform PUBLIC continui_core user32 gdi32)
    target_compile_definitions(continui_platform PRIVATE UNICODE _UNICODE)
else()
    add_library(continui_platform INTERFACE)
    target_compile_features(continui_platform INTERFACE cxx_std_23)
endif()
