# platform: windows, input, IME, DPI, clipboard.
# Per-platform backends (Win32 first) arrive in Phase 0.2; this target
# stays out of the default continui umbrella until then.
add_library(continui_platform INTERFACE)
target_compile_features(continui_platform INTERFACE cxx_std_23)
