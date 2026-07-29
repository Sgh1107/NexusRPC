/// @file mcp_server.h
/// @brief MCP stdio gateway (TASK-022).
///
/// Implements a Model Context Protocol server that communicates via stdin /
/// stdout.  Parses JSON-RPC 2.0 requests, manages session lifecycle, and
/// dispatches tools/list and tools/call through the RPC client.
///
/// All protocol output is written to stdout; diagnostics and errors go to
/// stderr so that the stdio transport is not corrupted.

#ifndef NEXUS_MCP_MCP_SERVER_H_
#define NEXUS_MCP_MCP_SERVER_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "nexus/mcp/json_rpc.h"
#include "nexus/mcp/tool_registry.h"
#include "nexus/rpc/rpc_client.h"
#include "nexus/rpc/rpc_message.h"
#include "nexus/rpc/status.h"

namespace nexus::mcp {

// ============================================================================
// McpSession
// ============================================================================

/// Internal representation of an MCP session for stdio transport.
///
/// There is exactly one session per stdio connection (the process lifetime).
/// Session state enforces the MCP lifecycle: initialize must succeed before
/// any other method is accepted.
struct McpSession {
  enum class State { kUninitialized, kInitialized, kShutdown };

  State state = State::kUninitialized;
  std::string server_name;
  std::string server_version;
};

// ============================================================================
// McpServer
// ============================================================================

/// MCP stdio server.
///
/// The server reads one JSON-RPC request per line from stdin, processes it,
/// and writes exactly one JSON-RPC response per non-notification request to
/// stdout.  Stderr is reserved for logging and diagnostics.
///
/// Supported methods:
///   - initialize      : MCP handshake, returns server capabilities.
///   - notifications/initialized : Acknowledged silently.
///   - ping            : Health-check, returns empty result.
///   - tools/list      : Returns all tools registered in the ToolRegistry.
///   - tools/call      : Forwards to the RPC backend and returns the result.
class McpServer {
 public:
  /// Constructs an MCP server.
  ///
  /// @param rpc_client  RPC client connected to the backend services.
  /// @param registry    Tool registry populated with registered tools.
  /// @param server_name Human-readable server name reported in initialize.
  /// @param server_version Semver string reported in initialize.
  McpServer(rpc::RpcClient* rpc_client, ToolRegistry* registry,
            std::string server_name, std::string server_version);

  ~McpServer();

  McpServer(const McpServer&) = delete;
  McpServer& operator=(const McpServer&) = delete;

  // ---- lifecycle ----------------------------------------------------------

  /// Runs the blocking stdin → stdout loop.
  ///
  /// Reads one line at a time until EOF or an unrecoverable error.
  void run();

  /// Signals the server to stop after the current request completes.
  void stop();

  /// Returns true when the server loop has exited.
  bool isStopped() const noexcept;

 private:
  /// Processes a single JSON-RPC request and returns the serialised response
  /// (empty string for notifications).
  std::string dispatch(const JsonRpcRequest& request);

  // ---- method handlers ----------------------------------------------------

  std::string handleInitialize(const JsonRpcRequest& request);
  std::string handleInitialized(const JsonRpcRequest& request);
  std::string handlePing(const JsonRpcRequest& request);
  std::string handleToolsList(const JsonRpcRequest& request);
  std::string handleToolsCall(const JsonRpcRequest& request);

  // ---- helpers ------------------------------------------------------------

  /// Builds the JSON Schema object for a single tool.
  static JsonValue buildToolSchema(const ToolDescriptor& desc);

  /// Builds the JSON Schema property object for a single parameter.
  static JsonValue buildPropertySchema(const ToolParameter& param);

  /// Converts a JSON object to a Protobuf binary payload using the supplied
  /// descriptor.  Returns the serialised bytes on success.
  rpc::Result<std::string> jsonToProtobuf(
      const JsonValue& json,
      const google::protobuf::Descriptor* descriptor);

  /// Converts a Protobuf binary payload to a JSON value.
  rpc::Result<JsonValue> protobufToJson(
      const std::string& binary,
      const google::protobuf::Descriptor* descriptor);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace nexus::mcp

#endif  // NEXUS_MCP_MCP_SERVER_H_
