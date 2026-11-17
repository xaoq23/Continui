# layout: box model, flex solver, text measuring, Canvas leaf.
# Must stay free of GPU and platform dependencies.
add_library(continui_layout INTERFACE)
target_compile_features(continui_layout INTERFACE cxx_std_23)
target_link_libraries(continui_layout INTERFACE continui_core continui_style)
