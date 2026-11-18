add_subdirectory(hello_world)
# The shapes example needs the real Dawn backend (opt-in via
# CONTINUI_BUILD_BACKEND_DAWN; the interface stub is not enough to build it).
if(CONTINUI_BUILD_BACKEND_DAWN AND WIN32)
    add_subdirectory(shapes)
    add_subdirectory(text)
endif()
