find_package(GTest REQUIRED)
include(GoogleTest)

# ---- Weather / Echo service integration tests (TASK-015) -------------------
add_executable(service_integration_test service_integration_test.cpp)
target_link_libraries(service_integration_test
  PRIVATE
    nexus_rpc
    nexus_proto
    GTest::gtest_main
)
target_include_directories(service_integration_test PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}/proto
)
gtest_discover_tests(service_integration_test)
