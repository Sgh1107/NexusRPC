/// @file json_rpc.h
/// @brief JSON-RPC 2.0 types, parser, and serializer (TASK-020).
///
/// Implements a minimal but conformant JSON DOM and recursive-descent parser
/// sufficient for MCP stdio.  The parser rejects batch arrays (not supported
/// in v1) and returns structured errors with line / column information.

#ifndef NEXUS_MCP_JSON_RPC_H_
#define NEXUS_MCP_JSON_RPC_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace nexus::mcp {

// ============================================================================
// JsonValue -- minimal JSON DOM
// ============================================================================

/// Discriminated JSON value used by the JSON-RPC layer.
///
/// All types except Object and Array are stored by value.  Object preserves
/// insertion order because MCP clients may depend on it for display.
class JsonValue {
 public:
  /// JSON type tag.
  enum class Type { kNull, kBool, kInteger, kDouble, kString, kObject, kArray };

  // ---- constructors -------------------------------------------------------

  JsonValue() : type_(Type::kNull) {}                               ///< null
  explicit JsonValue(bool v) : type_(Type::kBool), bool_(v) {}     ///< bool
  explicit JsonValue(int64_t v) : type_(Type::kInteger), int_(v) {}///< integer
  explicit JsonValue(double v) : type_(Type::kDouble), double_(v) {}///< double
  explicit JsonValue(std::string v)                                ///< string
      : type_(Type::kString), string_(std::move(v)) {}

  /// Convenience constructor from a C-string literal.
  JsonValue(const char* v) : type_(Type::kString), string_(v) {}

  /// Factory for an empty object.
  static JsonValue MakeObject() {
    JsonValue v;
    v.type_ = Type::kObject;
    return v;
  }

  /// Factory for an empty array.
  static JsonValue MakeArray() {
    JsonValue v;
    v.type_ = Type::kArray;
    return v;
  }

  // ---- accessors ----------------------------------------------------------

  Type type() const noexcept { return type_; }

  bool isNull() const noexcept { return type_ == Type::kNull; }
  bool isBool() const noexcept { return type_ == Type::kBool; }
  bool isInteger() const noexcept { return type_ == Type::kInteger; }
  bool isDouble() const noexcept { return type_ == Type::kDouble; }
  bool isNumber() const noexcept {
    return type_ == Type::kInteger || type_ == Type::kDouble;
  }
  bool isString() const noexcept { return type_ == Type::kString; }
  bool isObject() const noexcept { return type_ == Type::kObject; }
  bool isArray() const noexcept { return type_ == Type::kArray; }

  bool toBool() const noexcept { return bool_; }
  int64_t toInteger() const noexcept { return int_; }
  double toDouble() const noexcept {
    return type_ == Type::kInteger ? static_cast<double>(int_) : double_;
  }
  const std::string& toString() const& noexcept { return string_; }
  std::string toString() && noexcept { return std::move(string_); }

  /// Number of members (Object) or elements (Array); 0 otherwise.
  size_t size() const noexcept;

  /// Returns true when the object contains @p key.
  bool hasMember(std::string_view key) const noexcept;

  /// Object member access (must be Type::kObject).
  const JsonValue& operator[](std::string_view key) const;
  JsonValue& operator[](std::string_view key);

  /// Insert or overwrite a member.  Returns *this for chaining.
  JsonValue& set(std::string key, JsonValue value);

  /// Array element access (must be Type::kArray).
  const JsonValue& operator[](size_t index) const;
  JsonValue& operator[](size_t index);

  /// Append an element to an array.  Returns *this for chaining.
  JsonValue& push(JsonValue value);

  /// Range access for objects.
  const std::vector<std::pair<std::string, JsonValue>>& members() const
      noexcept {
    return members_;
  }

  /// Range access for arrays.
  const std::vector<JsonValue>& elements() const noexcept { return elements_; }

  // ---- serialization ------------------------------------------------------

  /// Returns the canonical JSON text representation.
  std::string serialize() const;

 private:
  Type type_ = Type::kNull;
  bool bool_ = false;
  int64_t int_ = 0;
  double double_ = 0.0;
  std::string string_;
  std::vector<std::pair<std::string, JsonValue>> members_;
  std::vector<JsonValue> elements_;
};

// ============================================================================
// JsonRpcError
// ============================================================================

/// Standard JSON-RPC 2.0 error object.
struct JsonRpcError {
  int code = 0;
  std::string message;
  std::optional<JsonValue> data;

  /// Returns a JSON-RPC 2.0 error response serialized as a JSON string.
  std::string serialize(const JsonValue& id) const;
};

/// Pre-defined JSON-RPC 2.0 error codes.
namespace ErrorCode {
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;
}  // namespace ErrorCode

// ============================================================================
// JsonRpcRequest / JsonRpcResponse
// ============================================================================

/// Parsed JSON-RPC 2.0 request.
struct JsonRpcRequest {
  std::string method;
  JsonValue id;        ///< null for Notifications
  JsonValue params;    ///< absent → JsonValue::kNull

  /// True when this is a Notification (no response expected).
  bool isNotification() const noexcept { return id.isNull(); }
};

/// JSON-RPC 2.0 response (success or error).
struct JsonRpcResponse {
  /// Constructs a successful response.
  static JsonRpcResponse Success(JsonValue id, JsonValue result);

  /// Constructs an error response.
  static JsonRpcResponse Error(JsonValue id, int code, std::string message,
                               std::optional<JsonValue> data = std::nullopt);

  /// Serializes to the JSON-RPC 2.0 wire format.
  std::string serialize() const;

  JsonValue id;
  std::optional<JsonValue> result;
  std::optional<JsonRpcError> error;
};

// ============================================================================
// JsonRpcParser -- validates and parses JSON-RPC 2.0 messages
// ============================================================================

/// Parses a single JSON-RPC 2.0 request from a JSON text.
///
/// Returns std::nullopt on parse / validation failure; the error message is
/// available via `lastError()`.  Notification detection is automatic when the
/// `id` field is null.
class JsonRpcParser {
 public:
  /// Attempts to parse a JSON-RPC 2.0 request from @p json_text.
  std::optional<JsonRpcRequest> parseRequest(std::string_view json_text);

  /// Returns a human-readable description of the last parse error.
  const std::string& lastError() const noexcept { return last_error_; }

 private:
  std::string last_error_;
};

// ============================================================================
// JsonRpcSerializer -- constructs well-formed JSON-RPC 2.0 messages
// ============================================================================

/// Builds JSON-RPC 2.0 responses from structured data.
class JsonRpcSerializer {
 public:
  /// Serializes a successful method result.
  static std::string serializeResponse(const JsonValue& id,
                                       const JsonValue& result);

  /// Serializes an error response.
  static std::string serializeError(const JsonValue& id, int code,
                                    std::string_view message,
                                    const JsonValue* data = nullptr);

  /// Serializes an error response for a parse failure (id is null).
  static std::string serializeParseError();
};

}  // namespace nexus::mcp

#endif  // NEXUS_MCP_JSON_RPC_H_
