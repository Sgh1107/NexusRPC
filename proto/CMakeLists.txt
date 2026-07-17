add_library(nexus_proto INTERFACE)
add_library(NexusRPC::proto ALIAS nexus_proto)

# Protobuf generation is intentionally not configured in Phase 0.
# The target exists to reserve the ownership boundary for generated interfaces.
