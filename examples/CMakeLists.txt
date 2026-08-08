# ---------------------------------------------------------------------------
# Weather service executable (port 9601)
# ---------------------------------------------------------------------------
add_executable(weather_server weather_service/main.cpp)
target_link_libraries(weather_server PRIVATE nexus_rpc nexus_proto)
target_include_directories(weather_server PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}/proto
)

# ---------------------------------------------------------------------------
# Echo service executable (port 9602)
# ---------------------------------------------------------------------------
add_executable(echo_server echo_service/main.cpp)
target_link_libraries(echo_server PRIVATE nexus_rpc nexus_proto)
target_include_directories(echo_server PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}/proto
)

# ---------------------------------------------------------------------------
# MCP stdio gateway executable (TASK-022; connects to weather_server:9601)
# ---------------------------------------------------------------------------
add_executable(mcp_gateway mcp_gateway/main.cpp)
target_link_libraries(mcp_gateway PRIVATE nexus_mcp nexus_rpc nexus_proto)
target_include_directories(mcp_gateway PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}/proto
)
