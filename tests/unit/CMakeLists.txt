include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

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
