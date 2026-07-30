if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)

    add_library(nexus_net STATIC
        net/buffer.cpp
        net/channel.cpp
        net/event_loop.cpp
        net/socket.cpp
        net/acceptor.cpp
        net/tcp_connection.cpp
        net/tcp_server.cpp
    )
    target_link_libraries(nexus_net PUBLIC Threads::Threads)
    target_include_directories(nexus_net PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    )
else()
    add_library(nexus_net INTERFACE)
    target_include_directories(nexus_net INTERFACE
        ${PROJECT_SOURCE_DIR}/include
    )
endif()

add_library(NexusRPC::net ALIAS nexus_net)

# ---- nexus_observability --------------------------------------------------
add_library(nexus_observability STATIC
    observability/logging.cpp
)
target_link_libraries(nexus_observability PUBLIC spdlog::spdlog)
target_include_directories(nexus_observability PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
add_library(NexusRPC::observability ALIAS nexus_observability)

# ---- nexus_rpc ------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(nexus_rpc STATIC
        rpc/frame_codec.cpp
        rpc/rpc_client.cpp
        rpc/rpc_server.cpp
    )
    target_link_libraries(nexus_rpc PUBLIC
        nexus_net
        nexus_observability
        nexus_proto
    )
    target_include_directories(nexus_rpc PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    )
else()
    add_library(nexus_rpc INTERFACE)
    target_link_libraries(nexus_rpc INTERFACE
        nexus_net
        nexus_observability
        nexus_proto
    )
    target_include_directories(nexus_rpc INTERFACE
        ${PROJECT_SOURCE_DIR}/include
    )
endif()

add_library(NexusRPC::rpc ALIAS nexus_rpc)

# ---- nexus_mcp ------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(nexus_mcp STATIC
        mcp/json_rpc.cpp
        mcp/tool_registry.cpp
        mcp/mcp_server.cpp
    )
    target_link_libraries(nexus_mcp PUBLIC nexus_rpc)
    target_include_directories(nexus_mcp PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
    )
else()
    add_library(nexus_mcp INTERFACE)
    target_link_libraries(nexus_mcp INTERFACE nexus_rpc)
    target_include_directories(nexus_mcp INTERFACE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
    )
endif()

add_library(NexusRPC::mcp ALIAS nexus_mcp)

add_library(nexus_registry INTERFACE)
add_library(NexusRPC::registry ALIAS nexus_registry)
target_link_libraries(nexus_registry INTERFACE nexus_rpc)
target_include_directories(nexus_registry INTERFACE
    ${PROJECT_SOURCE_DIR}/include
)





