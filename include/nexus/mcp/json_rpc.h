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
  bool has_id = false;

  /// A JSON-RPC notification omits the id member entirely.
  bool isNotification() const noexcept { return !has_id; }
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
  int lastErrorCode() const noexcept { return last_error_code_; }
  const JsonValue& lastErrorId() const noexcept { return last_error_id_; }
  bool lastErrorIsNotification() const noexcept {
    return last_error_is_notification_;
  }

 private:
  std::string last_error_;
  int last_error_code_ = ErrorCode::kParseError;
  JsonValue last_error_id_;
  bool last_error_is_notification_ = false;
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
