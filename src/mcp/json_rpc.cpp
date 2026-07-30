/// @file json_rpc.cpp
/// @brief JSON-RPC 2.0 parser and serializer implementation (TASK-020).

#include "nexus/mcp/json_rpc.h"

#include <optional>
#include <string>
#include <utility>

namespace nexus::mcp {
namespace {

JsonValue makeErrorObject(const JsonRpcError& error) {
  JsonValue value = JsonValue::object();
  value["code"] = error.code;
  value["message"] = error.message;
  if (error.data.has_value()) {
    value["data"] = *error.data;
  }
  return value;
}

bool isValidId(const JsonValue& id) {
  return id.is_null() || id.is_string() || id.is_number();
}

}  // namespace

std::string JsonRpcError::serialize(const JsonValue& id) const {
  JsonValue response = JsonValue::object();
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  response["error"] = makeErrorObject(*this);
  return response.dump();
}

JsonRpcResponse JsonRpcResponse::Success(JsonValue id, JsonValue result) {
  JsonRpcResponse response;
  response.id = std::move(id);
  response.result = std::move(result);
  return response;
}

JsonRpcResponse JsonRpcResponse::Error(JsonValue id, int code,
                                       std::string message,
                                       std::optional<JsonValue> data) {
  JsonRpcResponse response;
  response.id = std::move(id);
  response.error = JsonRpcError{code, std::move(message), std::move(data)};
  return response;
}

std::string JsonRpcResponse::serialize() const {
  JsonValue response = JsonValue::object();
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  if (result.has_value()) {
    response["result"] = *result;
  } else if (error.has_value()) {
    response["error"] = makeErrorObject(*error);
  }
  return response.dump();
}

std::optional<JsonRpcRequest> JsonRpcParser::parseRequest(
    std::string_view json_text) {
  last_error_.clear();
  last_error_code_ = ErrorCode::kParseError;
  last_error_id_ = JsonValue();
  last_error_is_notification_ = false;

  JsonValue root = JsonValue::parse(std::string(json_text), nullptr, false);
  if (root.is_discarded()) {
    last_error_ = "invalid JSON";
    return std::nullopt;
  }

  last_error_code_ = ErrorCode::kInvalidRequest;
  if (!root.is_object()) {
    last_error_ = "JSON-RPC request must be a JSON object";
    return std::nullopt;
  }

  if (root.contains("id") && isValidId(root["id"])) {
    last_error_id_ = root["id"];
  }

  if (!root.contains("jsonrpc") || !root["jsonrpc"].is_string() ||
      root["jsonrpc"] != "2.0") {
    last_error_ = "missing or invalid 'jsonrpc' field (expected \"2.0\")";
    return std::nullopt;
  }
  if (!root.contains("method") || !root["method"].is_string()) {
    last_error_ = "missing or invalid 'method' field";
    return std::nullopt;
  }
  last_error_is_notification_ = !root.contains("id");

  JsonRpcRequest request;
  request.method = root["method"].get<std::string>();
  if (root.contains("id")) {
    const JsonValue& id = root["id"];
    if (!isValidId(id)) {
      last_error_ = "'id' must be a string, number, or null";
      return std::nullopt;
    }
    request.id = id;
    request.has_id = true;
  }

  if (root.contains("params")) {
    const JsonValue& params = root["params"];
    if (!params.is_object() && !params.is_array()) {
      last_error_code_ = ErrorCode::kInvalidParams;
      last_error_ = "'params' must be a JSON object or array when present";
      return std::nullopt;
    }
    request.params = params;
  }
  return request;
}

std::string JsonRpcSerializer::serializeResponse(const JsonValue& id,
                                                 const JsonValue& result) {
  return JsonRpcResponse::Success(id, result).serialize();
}

std::string JsonRpcSerializer::serializeError(const JsonValue& id, int code,
                                              std::string_view message,
                                              const JsonValue* data) {
  std::optional<JsonValue> error_data;
  if (data != nullptr) {
    error_data = *data;
  }
  return JsonRpcResponse::Error(id, code, std::string(message),
                                std::move(error_data)).serialize();
}

std::string JsonRpcSerializer::serializeParseError() {
  return JsonRpcResponse::Error(JsonValue(), ErrorCode::kParseError,
                                "Parse error").serialize();
}

}  // namespace nexus::mcp
