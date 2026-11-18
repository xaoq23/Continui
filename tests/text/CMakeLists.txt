add_executable(text_test text_test.cpp)
# Internal text headers (glyph_atlas/glyph_cache) are deliberately not part
# of the public include tree; unit tests get a private path, like the render
# tests do for batcher.hpp.
target_include_directories(text_test PRIVATE "${CMAKE_SOURCE_DIR}/src/text")
target_link_libraries(text_test PRIVATE continui_text)
add_test(NAME text COMMAND text_test)
