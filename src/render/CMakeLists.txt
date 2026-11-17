# render: painter, draw batches, dirty regions, compositing.
# gfx::Device abstraction (Dawn backend) arrives in Phase 0.3; this target
# stays out of the default continui umbrella until then.
add_library(continui_render INTERFACE)
target_compile_features(continui_render INTERFACE cxx_std_23)
target_link_libraries(continui_render INTERFACE continui_layout)
