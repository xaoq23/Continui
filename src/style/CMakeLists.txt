# style: style values, units, inheritance, themes.
add_library(continui_style INTERFACE)
target_compile_features(continui_style INTERFACE cxx_std_23)
target_link_libraries(continui_style INTERFACE continui_core)
