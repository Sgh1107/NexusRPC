find_package(Protobuf REQUIRED)

# ---------------------------------------------------------------------------
# Define the target first so protobuf_generate can attach sources to it
# ---------------------------------------------------------------------------
add_library(nexus_proto STATIC)
add_library(NexusRPC::proto ALIAS nexus_proto)

# ---------------------------------------------------------------------------
# Generate .pb.cc / .pb.h from protocol buffer definitions
# ---------------------------------------------------------------------------
protobuf_generate(
    LANGUAGE cpp
    TARGET nexus_proto
    PROTOS
        nexus/options.proto
        examples/weather.proto
        examples/echo.proto
    IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(nexus_proto PUBLIC protobuf::libprotobuf)
target_include_directories(nexus_proto PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
