# core: view contract, keys, invalidation, local state, arena, channel.
# Interface-only until Phase 0 fills in implementations.
add_library(continui_core INTERFACE)
target_compile_features(continui_core INTERFACE cxx_std_23)
target_include_directories(continui_core INTERFACE
    "${CMAKE_SOURCE_DIR}/include"
)
