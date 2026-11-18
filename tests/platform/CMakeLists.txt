# Platform (Win32) tests. Windows-only by definition; skipped elsewhere.
if(NOT WIN32)
    return()
endif()

add_executable(platform_window_test window_test.cpp)
target_link_libraries(platform_window_test PRIVATE continui_platform)

add_test(NAME platform_window_test COMMAND platform_window_test)
