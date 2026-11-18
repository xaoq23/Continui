# Dawn backend tests. Windows-only and only when the real backend was built
# (the interface stub cannot compile backend_dawn.hpp's Dawn headers).
if(NOT WIN32 OR NOT CONTINUI_BUILD_BACKEND_DAWN)
    return()
endif()

add_executable(backend_dawn_renderer_test dawn_renderer_test.cpp)
target_link_libraries(backend_dawn_renderer_test PRIVATE continui continui_backend_dawn)

add_test(NAME backend_dawn_renderer_test COMMAND backend_dawn_renderer_test)
