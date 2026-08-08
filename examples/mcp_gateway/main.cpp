/// @file mcp_gateway/main.cpp
/// @brief MCP stdio gateway example (TASK-022).
///
/// Connects to the Weather RPC service on 127.0.0.1:9601, exposes
/// `weather.get_current` over the MCP stdio transport, then blocks reading
/// one JSON-RPC request per line from stdin.  Start the Weather service
/// first:
///
///   ./weather_server &
///   echo '{"jsonrpc":"2.0","method":"initialize","id":1,"params":{}}' |
///       ./mcp_gateway
///
/// All protocol responses are written to stdout; diagnostics go to stderr.

#include <cstdlib>
#include <iostream>

#include <google/protobuf/descriptor.h>

#include "examples/weather.pb.h"
#include "nexus/mcp/mcp_server.h"
#include "nexus/mcp/tool_registry.h"
#include "nexus/rpc/rpc_client.h"
#include "nexus/rpc/status.h"

int main() {
  // v1.0 MVP uses a single static backend (docs/development_decisions.md #3).
  nexus::rpc::RpcClient client({"127.0.0.1", 9601});

  nexus::mcp::ToolRegistry registry;
  // GetCurrentRequest::descriptor() also forces weather.pb.o to be linked,
  // keeping the generated file descriptor registered in the pool.
  const auto* service =
      nexus::examples::weather::GetCurrentRequest::descriptor()
          ->file()
          ->FindServiceByName("Weather");
  if (service == nullptr) {
    std::cerr << "Failed to load Weather service descriptor" << std::endl;
    return EXIT_FAILURE;
  }
  for (int i = 0; i < service->method_count(); ++i) {
    const auto status = registry.registerTool(service, service->method(i));
    if (!status.ok()) {
      std::cerr << "Failed to register tool: " << status.message()
                << std::endl;
      return EXIT_FAILURE;
    }
  }

  nexus::mcp::McpServer server(&client, &registry,
                               "NexusRPC Weather Gateway", "0.1.0");
  server.run();
  return EXIT_SUCCESS;
}
