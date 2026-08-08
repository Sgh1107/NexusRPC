include(FetchContent)

find_package(GTest REQUIRED)
find_package(Protobuf REQUIRED)
include(GoogleTest)

# ---- Buffer unit tests ----------------------------------------------------
add_executable(buffer_test buffer_test.cpp)
target_link_libraries(buffer_test
  PRIVATE
    nexus_net
    GTest::gtest_main
)
target_include_directories(buffer_test PRIVATE ${PROJECT_SOURCE_DIR})
gtest_discover_tests(buffer_test)

# ---- Socket / InetAddress unit tests --------------------------------------
add_executable(socket_test socket_test.cpp)
target_link_libraries(socket_test
  PRIVATE
    nexus_net
    GTest::gtest_main
)
target_include_directories(socket_test PRIVATE ${PROJECT_SOURCE_DIR})
gtest_discover_tests(socket_test)

# ---- Channel unit tests ---------------------------------------------------
add_executable(channel_test channel_test.cpp)
target_link_libraries(channel_test
  PRIVATE
    nexus_net
    GTest::gtest_main
)
target_include_directories(channel_test PRIVATE ${PROJECT_SOURCE_DIR})
gtest_discover_tests(channel_test)

# ---- TcpConnection unit tests (construction only) -------------------------
add_executable(tcp_connection_test tcp_connection_test.cpp)
target_link_libraries(tcp_connection_test
  PRIVATE
    nexus_net
    GTest::gtest_main
)
target_include_directories(tcp_connection_test PRIVATE ${PROJECT_SOURCE_DIR})
gtest_discover_tests(tcp_connection_test)

# ---- TcpServer unit tests (construction only) -----------------------------
add_executable(tcp_server_test tcp_server_test.cpp)
target_link_libraries(tcp_server_test
  PRIVATE
    nexus_net
    GTest::gtest_main
)
target_include_directories(tcp_server_test PRIVATE ${PROJECT_SOURCE_DIR})
gtest_discover_tests(tcp_server_test)

# ---- RPC Frame Codec unit tests (TASK-012) --------------------------------
add_executable(frame_codec_test frame_codec_test.cpp)
target_link_libraries(frame_codec_test
  PRIVATE
    nexus_rpc
    GTest::gtest_main
)
target_include_directories(frame_codec_test PRIVATE ${PROJECT_SOURCE_DIR})
gtest_discover_tests(frame_codec_test)

# ---- RPC Server / Client SDK loopback tests (TASK-013, TASK-014) ----------
add_executable(rpc_sdk_test rpc_sdk_test.cpp)
target_link_libraries(rpc_sdk_test
  PRIVATE
    nexus_rpc
    GTest::gtest_main
)
target_include_directories(rpc_sdk_test PRIVATE ${PROJECT_SOURCE_DIR})
gtest_discover_tests(rpc_sdk_test)

# ---- JSON-RPC unit tests (TASK-020) ---------------------------------------
add_executable(json_rpc_test json_rpc_test.cpp)
target_link_libraries(json_rpc_test
  PRIVATE
    nexus_mcp
    GTest::gtest_main
)
target_include_directories(json_rpc_test PRIVATE ${PROJECT_SOURCE_DIR})
gtest_discover_tests(json_rpc_test)

# ---- Tool Registry unit tests (TASK-021) ----------------------------------
add_executable(tool_registry_test tool_registry_test.cpp)
target_link_libraries(tool_registry_test
  PRIVATE
    nexus_mcp
    nexus_proto
    GTest::gtest_main
)
protobuf_generate(
    LANGUAGE cpp
    TARGET tool_registry_test
    PROTOS schema_test.proto
    IMPORT_DIRS ${PROJECT_SOURCE_DIR}/proto
)
target_include_directories(tool_registry_test PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}/proto
    ${CMAKE_CURRENT_BINARY_DIR}
)
gtest_discover_tests(tool_registry_test)

# ---- MCP stdio Gateway tests (TASK-022) ------------------------------------
add_executable(mcp_server_test mcp_server_test.cpp)
target_link_libraries(mcp_server_test
  PRIVATE
    nexus_mcp
    nexus_rpc
    nexus_proto
    GTest::gtest_main
)
target_include_directories(mcp_server_test PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}/proto
)
gtest_discover_tests(mcp_server_test)

