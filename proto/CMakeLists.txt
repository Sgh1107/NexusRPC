find_package(Protobuf REQUIRED)

add_library(nexus_proto INTERFACE)
add_library(NexusRPC::proto ALIAS nexus_proto)
target_link_libraries(nexus_proto INTERFACE protobuf::libprotobuf)

# Generated application .pb.cc files will be added to this target by TASK-015.
