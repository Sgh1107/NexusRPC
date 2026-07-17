cmake_minimum_required(VERSION 3.15)

project(NexusRPC VERSION 0.1.0 LANGUAGES CXX)

option(NEXUSRPC_BUILD_EXAMPLES "Create example target boundaries" ON)
option(NEXUSRPC_BUILD_TESTS "Create test target boundaries" ON)
option(NEXUSRPC_BUILD_BENCHMARKS "Create benchmark target boundaries" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src)
add_subdirectory(proto)

if(NEXUSRPC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(NEXUSRPC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NEXUSRPC_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
