# Batcher tests exercise the pure-CPU geometry and merge behavior; the GPU
# side is covered by tests/backend.
add_executable(render_batcher_test batcher_test.cpp)
target_link_libraries(render_batcher_test PRIVATE continui_render)
target_include_directories(render_batcher_test PRIVATE "${CMAKE_SOURCE_DIR}/src/render")

add_test(NAME render_batcher_test COMMAND render_batcher_test)
