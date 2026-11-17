# Core module tests. Assertions use plain asserts (Phase 0 policy: the test
# framework choice is deferred; cizon test would only check exit codes anyway).
add_executable(core_key_test key_test.cpp)
target_link_libraries(core_key_test PRIVATE continui_core)

add_test(NAME core_key_test COMMAND core_key_test)
