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

add_library(nexus_rpc INTERFACE)
add_library(NexusRPC::rpc ALIAS nexus_rpc)
target_link_libraries(nexus_rpc INTERFACE nexus_net)
target_include_directories(nexus_rpc INTERFACE
    ${PROJECT_SOURCE_DIR}/include
)

add_library(nexus_mcp INTERFACE)
add_library(NexusRPC::mcp ALIAS nexus_mcp)
target_link_libraries(nexus_mcp INTERFACE nexus_rpc)
target_include_directories(nexus_mcp INTERFACE
    ${PROJECT_SOURCE_DIR}/include
)

add_library(nexus_registry INTERFACE)
add_library(NexusRPC::registry ALIAS nexus_registry)
target_link_libraries(nexus_registry INTERFACE nexus_rpc)
target_include_directories(nexus_registry INTERFACE
    ${PROJECT_SOURCE_DIR}/include
)

add_library(nexus_observability INTERFACE)
add_library(NexusRPC::observability ALIAS nexus_observability)
target_include_directories(nexus_observability INTERFACE
    ${PROJECT_SOURCE_DIR}/include
)
