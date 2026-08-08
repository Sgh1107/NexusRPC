/// @file mcp_server_test.cpp
/// @brief MCP stdio gateway tests (TASK-022).
///
/// Drives McpServer::run() with redirected stdin/stdout/stderr pipes:
///   - initialize lifecycle enforcement and notification semantics;
///   - tools/list schema output from descriptor-driven registration;
///   - tools/call round trip through a real RPC backend;
///   - stdout/stderr isolation (protocol on stdout, diagnostics on stderr).

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

#include <google/protobuf/descriptor.h>

#include "examples/weather.pb.h"
#include "nexus/mcp/mcp_server.h"
#include "nexus/mcp/tool_registry.h"
#include "nexus/rpc/rpc_client.h"
#include "nexus/rpc/rpc_server.h"
#include "nexus/rpc/status.h"

namespace nexus::mcp {
namespace {

using nexus::examples::weather::GetCurrentRequest;
using nexus::examples::weather::GetCurrentResponse;
using nexus::rpc::RpcClient;
using nexus::rpc::RpcServer;
using nexus::rpc::Status;

/** Resolves the generated Weather service descriptor from the descriptor pool. */
const google::protobuf::ServiceDescriptor* weatherServiceDescriptor() {
  // Referencing the generated message descriptor also guarantees the
  // weather.pb.o object is linked, so its file descriptor is registered.
  return nexus::examples::weather::GetCurrentRequest::descriptor()
      ->file()
      ->FindServiceByName("Weather");
}

/** Reserves an ephemeral loopback port long enough to obtain its number. */
std::uint16_t allocateLoopbackPort() {
  const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  EXPECT_GE(fd, 0);

  struct sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  EXPECT_EQ(::bind(fd, reinterpret_cast<const struct sockaddr*>(&address),
                   sizeof(address)),
            0);

  socklen_t address_length = sizeof(address);
  EXPECT_EQ(::getsockname(fd, reinterpret_cast<struct sockaddr*>(&address),
                          &address_length),
            0);
  const std::uint16_t port = ntohs(address.sin_port);
  EXPECT_EQ(::close(fd), 0);
  return port;
}

/** Reads a pipe until EOF. */
void readFully(int fd, std::string* output) {
  char buffer[4096];
  for (;;) {
    const ssize_t count = ::read(fd, buffer, sizeof(buffer));
    if (count > 0) {
      output->append(buffer, static_cast<std::size_t>(count));
      continue;
    }
    if (count == 0) {
      return;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    return;
  }
}

struct GatewayRunResult {
  bool ok = false;
  std::string stdout_text;
  std::string stderr_text;
};

/**
 * Runs the gateway with @p lines fed to its stdin, capturing stdout and
 * stderr separately.  stdin is closed after the input to trigger EOF, so the
 * server exits before the original descriptors are restored.
 */
GatewayRunResult runGateway(McpServer& server,
                            const std::vector<std::string>& lines) {
  GatewayRunResult result;
  std::cout.flush();
  std::cerr.flush();
  std::fflush(nullptr);

  const int saved_in = ::dup(STDIN_FILENO);
  const int saved_out = ::dup(STDOUT_FILENO);
  const int saved_err = ::dup(STDERR_FILENO);
  if (saved_in < 0 || saved_out < 0 || saved_err < 0) {
    if (saved_in >= 0) ::close(saved_in);
    if (saved_out >= 0) ::close(saved_out);
    if (saved_err >= 0) ::close(saved_err);
    return result;
  }

  int in_pipe[2] = {-1, -1};
  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0 ||
      ::pipe(err_pipe) != 0) {
    ::close(saved_in);
    ::close(saved_out);
    ::close(saved_err);
    return result;
  }

  if (::dup2(in_pipe[0], STDIN_FILENO) < 0 ||
      ::dup2(out_pipe[1], STDOUT_FILENO) < 0 ||
      ::dup2(err_pipe[1], STDERR_FILENO) < 0) {
    ::dup2(saved_in, STDIN_FILENO);
    ::dup2(saved_out, STDOUT_FILENO);
    ::dup2(saved_err, STDERR_FILENO);
    ::close(saved_in);
    ::close(saved_out);
    ::close(saved_err);
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    ::close(err_pipe[0]);
    ::close(err_pipe[1]);
    return result;
  }
  ::close(in_pipe[0]);
  ::close(out_pipe[1]);
  ::close(err_pipe[1]);

  std::thread server_thread([&server]() { server.run(); });

  for (const auto& line : lines) {
    const std::string request = line + "\n";
    std::size_t offset = 0;
    while (offset < request.size()) {
      const ssize_t count =
          ::write(in_pipe[1], request.data() + offset,
                  request.size() - offset);
      if (count < 0 && errno == EINTR) {
        continue;
      }
      if (count <= 0) {
        break;
      }
      offset += static_cast<std::size_t>(count);
    }
  }
  ::close(in_pipe[1]);

  server_thread.join();

  // Flush any pending output into the pipes, then close the process-side
  // copies so the readers below observe EOF.
  std::cout.flush();
  std::cerr.flush();
  std::fflush(stdout);
  std::fflush(stderr);
  ::close(STDOUT_FILENO);
  ::close(STDERR_FILENO);

  ::dup2(saved_out, STDOUT_FILENO);
  ::dup2(saved_err, STDERR_FILENO);
  ::dup2(saved_in, STDIN_FILENO);
  ::close(saved_in);
  ::close(saved_out);
  ::close(saved_err);

  readFully(out_pipe[0], &result.stdout_text);
  readFully(err_pipe[0], &result.stderr_text);
  ::close(out_pipe[0]);
  ::close(err_pipe[0]);

  result.ok = true;
  return result;
}

/** Extracts protocol response lines (JSON) from captured stdout. */
std::vector<std::string> jsonLines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.front() == '{') {
      lines.push_back(line);
    }
  }
  return lines;
}

// ============================================================================
// Lifecycle and stdio isolation
// ============================================================================

TEST(McpServerTest, InitializeLifecycleAndStdioIsolation) {
  ToolRegistry registry;
  McpServer server(nullptr, &registry, "nexus-test", "0.1.0");

  const GatewayRunResult result = runGateway(server, {
      R"({"jsonrpc":"2.0","method":"tools/list","id":1})",
      R"({"jsonrpc":"2.0","method":"initialize","id":2,"params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"0.0.0"}}})",
      R"({"jsonrpc":"2.0","method":"ping","id":3})",
      R"({"jsonrpc":"2.0","method":"initialize","id":4})",
      R"({"jsonrpc":"2.0","method":"notifications/initialized"})",
      R"({"jsonrpc":"2.0","method":"unknown.method","id":5})",
      "not json",
  });
  ASSERT_TRUE(result.ok);

  const std::vector<std::string> responses = jsonLines(result.stdout_text);
  ASSERT_EQ(responses.size(), 6U);

  // tools/list before initialize -> -32002 (Server not initialized).
  EXPECT_NE(responses[0].find("-32002"), std::string::npos);
  // initialize -> success with protocol version, server info, capabilities.
  EXPECT_NE(responses[1].find("\"protocolVersion\":\"2024-11-05\""),
            std::string::npos);
  EXPECT_NE(responses[1].find("\"serverInfo\""), std::string::npos);
  EXPECT_NE(responses[1].find("\"capabilities\""), std::string::npos);
  // ping -> empty result.
  EXPECT_NE(responses[2].find("\"id\":3"), std::string::npos);
  EXPECT_NE(responses[2].find("\"result\""), std::string::npos);
  // duplicate initialize -> -32600 InvalidRequest.
  EXPECT_NE(responses[3].find("-32600"), std::string::npos);
  // unknown method -> -32601 MethodNotFound.
  EXPECT_NE(responses[4].find("-32601"), std::string::npos);
  // parse failure -> -32700 ParseError with null id.
  EXPECT_NE(responses[5].find("-32700"), std::string::npos);
  EXPECT_NE(responses[5].find("\"id\":null"), std::string::npos);

  // Stdio isolation: diagnostics go to stderr, never stdout.
  EXPECT_NE(result.stderr_text.find("unknown method"), std::string::npos);
  EXPECT_NE(result.stderr_text.find("[mcp] parse error"), std::string::npos);
  EXPECT_EQ(result.stdout_text.find("[mcp]"), std::string::npos);
}

// ============================================================================
// tools/list
// ============================================================================

TEST(McpServerTest, ToolsListFromWeatherDescriptor) {
  ToolRegistry registry;
  const auto* service = weatherServiceDescriptor();
  ASSERT_NE(service, nullptr);
  ASSERT_TRUE(registry.registerTool(service, service->method(0)).ok());

  McpServer server(nullptr, &registry, "nexus-test", "0.1.0");
  const GatewayRunResult result = runGateway(server, {
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2024-11-05"}})",
      R"({"jsonrpc":"2.0","method":"tools/list","id":2})",
  });
  ASSERT_TRUE(result.ok);

  const std::vector<std::string> responses = jsonLines(result.stdout_text);
  ASSERT_EQ(responses.size(), 2U);
  EXPECT_NE(responses[1].find("weather.get_current"), std::string::npos);
  EXPECT_NE(responses[1].find("inputSchema"), std::string::npos);
  EXPECT_NE(responses[1].find("\"city\""), std::string::npos);
  EXPECT_NE(responses[1].find("required"), std::string::npos);
  EXPECT_NE(responses[1].find("Returns the current weather"),
            std::string::npos);
}

// ============================================================================
// tools/call
// ============================================================================

TEST(McpServerTest, ToolsCallRoundTrip) {
  const std::uint16_t port = allocateLoopbackPort();
  RpcServer rpc_server(2);
  ASSERT_TRUE((rpc_server.registerService<GetCurrentRequest,
                                          GetCurrentResponse>(
                   "weather", "GetCurrent",
                   [](const GetCurrentRequest& request,
                      GetCurrentResponse* response) {
                     response->set_city(request.city());
                     response->set_temperature_celsius(28.5);
                     response->set_humidity_percent(55.0);
                     response->set_condition("Sunny");
                     return Status::Ok();
                   })).ok());
  ASSERT_TRUE(rpc_server.start(port).ok());

  RpcClient client({"127.0.0.1", port});
  ToolRegistry registry;
  const auto* service = weatherServiceDescriptor();
  ASSERT_NE(service, nullptr);
  ASSERT_TRUE(registry.registerTool(service, service->method(0)).ok());

  McpServer server(&client, &registry, "nexus-test", "0.1.0");
  const GatewayRunResult result = runGateway(server, {
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2024-11-05"}})",
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,"params":{"name":"weather.get_current","arguments":{"city":"Beijing"}}})",
  });
  rpc_server.stop();

  ASSERT_TRUE(result.ok);
  const std::vector<std::string> responses = jsonLines(result.stdout_text);
  ASSERT_EQ(responses.size(), 2U);

  // Success result with text content and structuredContent.
  EXPECT_NE(responses[1].find("\"result\""), std::string::npos);
  EXPECT_NE(responses[1].find("structuredContent"), std::string::npos);
  EXPECT_NE(responses[1].find("\"type\":\"text\""), std::string::npos);
  EXPECT_NE(responses[1].find("temperatureCelsius"), std::string::npos);
  EXPECT_NE(responses[1].find("28.5"), std::string::npos);
  EXPECT_NE(responses[1].find("Beijing"), std::string::npos);
}

TEST(McpServerTest, ToolsCallParamErrors) {
  ToolRegistry registry;
  const auto* service = weatherServiceDescriptor();
  ASSERT_NE(service, nullptr);
  ASSERT_TRUE(registry.registerTool(service, service->method(0)).ok());

  McpServer server(nullptr, &registry, "nexus-test", "0.1.0");
  const GatewayRunResult result = runGateway(server, {
      R"({"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2024-11-05"}})",
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,"params":{}})",
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,"params":{"name":"does.not.exist"}})",
  });
  ASSERT_TRUE(result.ok);

  const std::vector<std::string> responses = jsonLines(result.stdout_text);
  ASSERT_EQ(responses.size(), 3U);
  // Missing name -> -32602 InvalidParams.
  EXPECT_NE(responses[1].find("-32602"), std::string::npos);
  // Unknown tool -> -32601 MethodNotFound.
  EXPECT_NE(responses[2].find("-32601"), std::string::npos);
}

}  // namespace
}  // namespace nexus::mcp
