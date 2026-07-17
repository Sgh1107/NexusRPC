add_library(nexus_benchmarks INTERFACE)
add_library(NexusRPC::benchmarks ALIAS nexus_benchmarks)
target_link_libraries(nexus_benchmarks INTERFACE nexus_rpc)

# Benchmark executables are added after the RPC implementation exists.
