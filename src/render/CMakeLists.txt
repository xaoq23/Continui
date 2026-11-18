# render: painter, draw batches, dirty regions, compositing.
# The batcher is pure CPU data; GPU consumption lives in the backend module.
add_library(continui_render STATIC)
target_compile_features(continui_render PUBLIC cxx_std_23)
target_sources(continui_render PRIVATE
    batcher.cpp
    painter.cpp
    ui.cpp
)
# Layout's view_tree.hpp is internal; render reaches it through a private
# include path (same pattern as the backend's access to batcher.hpp).
target_include_directories(continui_render PRIVATE
    "${CMAKE_SOURCE_DIR}/src/layout"
)
# Layout sits between render and core/style in the dependency chain
# (render -> layout -> style -> core); the core include dir arrives
# transitively.
target_link_libraries(continui_render
    PUBLIC continui_layout
    PRIVATE continui_text  # UI paints text and measures via FontFace
)
