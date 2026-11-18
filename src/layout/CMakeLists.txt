# layout: box model, flex solver, text measuring interface, Canvas leaf.
# Pure CPU and stateless; no GPU or platform dependencies.
add_library(continui_layout STATIC)
target_compile_features(continui_layout PUBLIC cxx_std_23)
target_sources(continui_layout PRIVATE
    view_tree.cpp
    layout_solver.cpp
)
target_link_libraries(continui_layout PUBLIC continui_core continui_style)
