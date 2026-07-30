/// @file json_rpc.h
/// @brief JSON-RPC 2.0 types, parser, and serializer (TASK-020).

#ifndef NEXUS_MCP_JSON_RPC_H_
#define NEXUS_MCP_JSON_RPC_H_

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "json.hpp"

namespace nexus::mcp {

/** Preserves object insertion order in JSON-RPC and MCP responses. */
using JsonValue = nlohmann::ordered_json;

/// Standard JSON-RPC 2.0 error object.
struct JsonRpcError {
  int code = 0;
  std::string message;
  std::optional<JsonValue> data;

  std::string serialize(const JsonValue& id) const;
};

namespace ErrorCode {
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;
}  // namespace ErrorCode

/// Parsed JSON-RPC 2.0 request.
struct JsonRpcRequest {
  std::string method;
  JsonValue id;
  JsonValue params;

    /// A null identifier is a JSON-RPC notification.

  bool isNotification() const noexcept { return id.is_null(); }
};

/// JSON-RPC 2.0 response (success or error).
struct JsonRpcResponse {
  static JsonRpcResponse Success(JsonValue id, JsonValue result);
  static JsonRpcResponse Error(JsonValue id, int code, std::string message,
                               std::optional<JsonValue> data = std::nullopt);

  std::string serialize() const;

  JsonValue id;
  std::optional<JsonValue> result;
  std::optional<JsonRpcError> error;
};

/// Parses and validates one non-batch JSON-RPC 2.0 request.
class JsonRpcParser {
 public:
  std::optional<JsonRpcRequest> parseRequest(std::string_view json_text);
  const std::string& lastError() const noexcept { return last_error_; }

 private:
  std::string last_error_;
};

/// Builds well-formed JSON-RPC 2.0 response text.
class JsonRpcSerializer {
 public:
  static std::string serializeResponse(const JsonValue& id,
                                       const JsonValue& result);
  static std::string serializeError(const JsonValue& id, int code,
                                    std::string_view message,
                                    const JsonValue* data = nullptr);
  static std::string serializeParseError();
};

}  // namespace nexus::mcp

#endif  // NEXUS_MCP_JSON_RPC_H_
