add_executable(layout_test layout_test.cpp)
target_link_libraries(layout_test PRIVATE continui_layout)
add_test(NAME layout COMMAND layout_test)
