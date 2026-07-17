add_library(nexus_examples INTERFACE)
add_library(NexusRPC::examples ALIAS nexus_examples)

target_link_libraries(nexus_examples INTERFACE
    nexus_net
    nexus_rpc
    nexus_mcp
    nexus_registry
    nexus_observability
)

# Executable examples are added after the corresponding SDK implementations exist.
