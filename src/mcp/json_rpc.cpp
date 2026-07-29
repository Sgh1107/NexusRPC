/// @file json_rpc.cpp
/// @brief JSON-RPC 2.0 parser and serializer implementation (TASK-020).

#include "nexus/mcp/json_rpc.h"

#include <cassert>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace nexus::mcp {

// ============================================================================
// JsonValue
// ============================================================================

size_t JsonValue::size() const noexcept {
  if (type_ == Type::kObject) return members_.size();
  if (type_ == Type::kArray) return elements_.size();
  return 0;
}

bool JsonValue::hasMember(std::string_view key) const noexcept {
  if (type_ != Type::kObject) return false;
  for (const auto& [k, v] : members_) {
    if (k == key) return true;
  }
  return false;
}

const JsonValue& JsonValue::operator[](std::string_view key) const {
  assert(type_ == Type::kObject);
  for (const auto& [k, v] : members_) {
    if (k == key) return v;
  }
  throw std::out_of_range(std::string("JsonValue key not found: ") +
                          std::string(key));
}

JsonValue& JsonValue::operator[](std::string_view key) {
  assert(type_ == Type::kObject);
  for (auto& [k, v] : members_) {
    if (k == key) return v;
  }
  throw std::out_of_range(std::string("JsonValue key not found: ") +
                          std::string(key));
}

JsonValue& JsonValue::set(std::string key, JsonValue value) {
  assert(type_ == Type::kObject);
  for (auto& [k, v] : members_) {
    if (k == key) {
      v = std::move(value);
      return *this;
    }
  }
  members_.emplace_back(std::move(key), std::move(value));
  return *this;
}

const JsonValue& JsonValue::operator[](size_t index) const {
  assert(type_ == Type::kArray);
  return elements_.at(index);
}

JsonValue& JsonValue::operator[](size_t index) {
  assert(type_ == Type::kArray);
  return elements_.at(index);
}

JsonValue& JsonValue::push(JsonValue value) {
  assert(type_ == Type::kArray);
  elements_.push_back(std::move(value));
  return *this;
}

// ---- serialization --------------------------------------------------------

namespace {

void serializeValue(const JsonValue& value, std::string& out);

void serializeString(const std::string& s, std::string& out) {
  out += '"';
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b";  break;
      case '\f': out += "\\f";  break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x",
                        static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += buf;
        } else {
          out += c;
        }
        break;
    }
  }
  out += '"';
}

void serializeValue(const JsonValue& value, std::string& out) {
  switch (value.type()) {
    case JsonValue::Type::kNull:
      out += "null";
      break;
    case JsonValue::Type::kBool:
      out += value.toBool() ? "true" : "false";
      break;
    case JsonValue::Type::kInteger:
      out += std::to_string(value.toInteger());
      break;
    case JsonValue::Type::kDouble: {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.17g", value.toDouble());
      out += buf;
      break;
    }
    case JsonValue::Type::kString:
      serializeString(value.toString(), out);
      break;
    case JsonValue::Type::kObject: {
      out += '{';
      bool first = true;
      for (const auto& [k, v] : value.members()) {
        if (!first) out += ',';
        first = false;
        serializeString(k, out);
        out += ':';
        serializeValue(v, out);
      }
      out += '}';
      break;
    }
    case JsonValue::Type::kArray: {
      out += '[';
      bool first = true;
      for (const auto& elem : value.elements()) {
        if (!first) out += ',';
        first = false;
        serializeValue(elem, out);
      }
      out += ']';
      break;
    }
  }
}

}  // namespace

std::string JsonValue::serialize() const {
  std::string result;
  serializeValue(*this, result);
  return result;
}

// ============================================================================
// JsonRpcError
// ============================================================================

std::string JsonRpcError::serialize(const JsonValue& id) const {
  auto err = JsonValue::MakeObject();
  err.set("code", JsonValue(static_cast<int64_t>(code)));
  err.set("message", JsonValue(message));
  if (data.has_value()) {
    err.set("data", *data);
  }

  auto resp = JsonValue::MakeObject();
  resp.set("jsonrpc", JsonValue("2.0"));
  resp.set("id", id);
  resp.set("error", std::move(err));
  return resp.serialize();
}

// ============================================================================
// JsonRpcResponse
// ============================================================================

JsonRpcResponse JsonRpcResponse::Success(JsonValue id, JsonValue result) {
  JsonRpcResponse r;
  r.id = std::move(id);
  r.result = std::move(result);
  return r;
}

JsonRpcResponse JsonRpcResponse::Error(JsonValue id, int code,
                                       std::string message,
                                       std::optional<JsonValue> data) {
  JsonRpcResponse r;
  r.id = std::move(id);
  r.error = JsonRpcError{code, std::move(message), std::move(data)};
  return r;
}

std::string JsonRpcResponse::serialize() const {
  auto resp = JsonValue::MakeObject();
  resp.set("jsonrpc", JsonValue("2.0"));
  resp.set("id", id);
  if (result.has_value()) {
    resp.set("result", *result);
  }
  if (error.has_value()) {
    auto err = JsonValue::MakeObject();
    err.set("code", JsonValue(static_cast<int64_t>(error->code)));
    err.set("message", JsonValue(error->message));
    if (error->data.has_value()) {
      err.set("data", *error->data);
    }
    resp.set("error", std::move(err));
  }
  return resp.serialize();
}

// ============================================================================
// JsonRpcSerializer
// ============================================================================

std::string JsonRpcSerializer::serializeResponse(const JsonValue& id,
                                                  const JsonValue& result) {
  return JsonRpcResponse::Success(id, result).serialize();
}

std::string JsonRpcSerializer::serializeError(const JsonValue& id, int code,
                                              std::string_view message,
                                              const JsonValue* data) {
  std::optional<JsonValue> opt_data;
  if (data != nullptr) opt_data = *data;
  return JsonRpcResponse::Error(id, code, std::string(message),
                                std::move(opt_data))
      .serialize();
}

std::string JsonRpcSerializer::serializeParseError() {
  return JsonRpcResponse::Error(JsonValue(), ErrorCode::kParseError,
                                "Parse error")
      .serialize();
}

// ============================================================================
// JsonRpcParser -- JSON lexer and recursive-descent parser
// ============================================================================

namespace {

/// Minimal JSON recursive-descent parser.
///
/// This parser is intentionally single-pass and does not allocate an AST
/// for values that can be stored inline.  It rejects:
///   - top-level arrays (batch JSON-RPC not supported)
///   - duplicate object keys (non-standard)
///   - trailing garbage
class JsonParserImpl {
 public:
  explicit JsonParserImpl(std::string_view input)
      : input_(input), pos_(0), line_(1), col_(1) {}

  /// Parses a complete JSON value.  Returns kNull on error; call error().
  JsonValue parse() {
    skipWhitespace();
    if (pos_ >= input_.size()) {
      setError("unexpected end of input");
      return JsonValue();
    }
    JsonValue result = parseValue();
    if (!error_.empty()) return JsonValue();
    skipWhitespace();
    if (pos_ < input_.size()) {
      setError("trailing characters after JSON value");
      return JsonValue();
    }
    return result;
  }

  const std::string& error() const noexcept { return error_; }

 private:
  // ---- lexer helpers ------------------------------------------------------

  void skipWhitespace() {
    while (pos_ < input_.size()) {
      const char c = input_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        advance();
      } else {
        break;
      }
    }
  }

  char peek() const {
    return pos_ < input_.size() ? input_[pos_] : '\0';
  }

  char advance() {
    if (pos_ >= input_.size()) return '\0';
    const char c = input_[pos_++];
    if (c == '\n') {
      ++line_;
      col_ = 1;
    } else {
      ++col_;
    }
    return c;
  }

  void setError(std::string msg) {
    if (error_.empty()) {
      error_ = std::to_string(line_) + ":" + std::to_string(col_) + ": " +
               std::move(msg);
    }
  }

  void expect(char expected) {
    if (peek() != expected) {
      setError(std::string("expected '") + expected + "'");
      return;
    }
    advance();
  }

  // ---- parser methods -----------------------------------------------------

  JsonValue parseValue() {
    skipWhitespace();
    const char c = peek();
    switch (c) {
      case '{': return parseObject();
      case '[': return parseArray();
      case '"': return parseString();
      case 't': case 'f': return parseBool();
      case 'n': return parseNull();
      case '-': case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
        return parseNumber();
      default:
        setError(std::string("unexpected character '") + c + "'");
        return JsonValue();
    }
  }

  JsonValue parseObject() {
    expect('{');
    auto obj = JsonValue::MakeObject();

    skipWhitespace();
    if (peek() == '}') {
      advance();
      return obj;
    }

    while (true) {
      skipWhitespace();
      if (peek() != '"') {
        setError("expected string key in object");
        return JsonValue();
      }
      JsonValue key = parseString();
      if (!error_.empty()) return JsonValue();

      skipWhitespace();
      expect(':');
      if (!error_.empty()) return JsonValue();

      skipWhitespace();
      JsonValue value = parseValue();
      if (!error_.empty()) return JsonValue();

      obj.set(key.toString(), std::move(value));

      skipWhitespace();
      const char next = peek();
      if (next == '}') {
        advance();
        return obj;
      }
      if (next != ',') {
        setError("expected ',' or '}' in object");
        return JsonValue();
      }
      advance();
    }
  }

  JsonValue parseArray() {
    expect('[');
    auto arr = JsonValue::MakeArray();

    skipWhitespace();
    if (peek() == ']') {
      advance();
      return arr;
    }

    while (true) {
      skipWhitespace();
      JsonValue value = parseValue();
      if (!error_.empty()) return JsonValue();
      arr.push(std::move(value));

      skipWhitespace();
      const char next = peek();
      if (next == ']') {
        advance();
        return arr;
      }
      if (next != ',') {
        setError("expected ',' or ']' in array");
        return JsonValue();
      }
      advance();
    }
  }

  JsonValue parseString() {
    expect('"');
    std::string result;
    result.reserve(64);

    while (pos_ < input_.size()) {
      char c = advance();
      if (c == '"') {
        return JsonValue(std::move(result));
      }
      if (c == '\\') {
        if (pos_ >= input_.size()) {
          setError("unterminated string escape");
          return JsonValue();
        }
        c = advance();
        switch (c) {
          case '"':  result += '"';  break;
          case '\\': result += '\\'; break;
          case '/':  result += '/';  break;
          case 'b':  result += '\b'; break;
          case 'f':  result += '\f'; break;
          case 'n':  result += '\n'; break;
          case 'r':  result += '\r'; break;
          case 't':  result += '\t'; break;
          case 'u': {
            unsigned codepoint = 0;
            for (int i = 0; i < 4; ++i) {
              if (pos_ >= input_.size()) {
                setError("unterminated \\u escape");
                return JsonValue();
              }
              const char hex = advance();
              codepoint <<= 4;
              if (hex >= '0' && hex <= '9')
                codepoint |= static_cast<unsigned>(hex - '0');
              else if (hex >= 'a' && hex <= 'f')
                codepoint |= static_cast<unsigned>(hex - 'a' + 10);
              else if (hex >= 'A' && hex <= 'F')
                codepoint |= static_cast<unsigned>(hex - 'A' + 10);
              else {
                setError("invalid hex digit in \\u escape");
                return JsonValue();
              }
            }
            // Encode as UTF-8.
            if (codepoint <= 0x7F) {
              result += static_cast<char>(codepoint);
            } else if (codepoint <= 0x7FF) {
              result += static_cast<char>(0xC0 | (codepoint >> 6));
              result += static_cast<char>(0x80 | (codepoint & 0x3F));
            } else {
              result += static_cast<char>(0xE0 | (codepoint >> 12));
              result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
              result += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            break;
          }
          default:
            setError(std::string("invalid escape character '\\") + c + "'");
            return JsonValue();
        }
      } else if (static_cast<unsigned char>(c) < 0x20) {
        setError("unescaped control character in string");
        return JsonValue();
      } else {
        result += c;
      }
    }
    setError("unterminated string");
    return JsonValue();
  }

  JsonValue parseNumber() {
    const char* start = input_.data() + pos_;
    char* end = nullptr;
    errno = 0;

    // Try integer first (faster and more precise for common case).
    const int64_t int_val = std::strtoll(start, &end, 10);
    if (end != start && errno == 0) {
      // Check if we consumed a valid integer and the next char is not
      // a fraction / exponent indicator.
      const size_t consumed = static_cast<size_t>(end - start);
      if (consumed > 0 &&
          (*end != '.' && *end != 'e' && *end != 'E')) {
        // Valid integer.
        for (size_t i = 0; i < consumed; ++i) advance();
        return JsonValue(int_val);
      }
    }

    // Fall back to double.
    errno = 0;
    const double dbl_val = std::strtod(start, &end);
    if (end == start || errno != 0) {
      setError("invalid number literal");
      return JsonValue();
    }
    const size_t consumed = static_cast<size_t>(end - start);
    for (size_t i = 0; i < consumed; ++i) advance();
    return JsonValue(dbl_val);
  }

  JsonValue parseBool() {
    if (peek() == 't') {
      if (pos_ + 4 <= input_.size() &&
          input_.substr(pos_, 4) == "true") {
        for (int i = 0; i < 4; ++i) advance();
        return JsonValue(true);
      }
    } else {
      if (pos_ + 5 <= input_.size() &&
          input_.substr(pos_, 5) == "false") {
        for (int i = 0; i < 5; ++i) advance();
        return JsonValue(false);
      }
    }
    setError("invalid literal (expected true/false)");
    return JsonValue();
  }

  JsonValue parseNull() {
    if (pos_ + 4 <= input_.size() && input_.substr(pos_, 4) == "null") {
      for (int i = 0; i < 4; ++i) advance();
      return JsonValue();
    }
    setError("invalid literal (expected null)");
    return JsonValue();
  }

  // ---- fields -------------------------------------------------------------

  std::string_view input_;
  size_t pos_;
  size_t line_;
  size_t col_;
  std::string error_;
};

}  // namespace

// ---- public parse API -----------------------------------------------------

std::optional<JsonRpcRequest> JsonRpcParser::parseRequest(
    std::string_view json_text) {
  last_error_.clear();

  // Step 1: parse the JSON value.
  JsonParserImpl parser(json_text);
  JsonValue root = parser.parse();
  if (!parser.error().empty()) {
    last_error_ = parser.error();
    return std::nullopt;
  }

  // Step 2: must be an object (batch arrays not supported).
  if (!root.isObject()) {
    last_error_ = "JSON-RPC request must be a JSON object";
    return std::nullopt;
  }

  // Step 3: validate jsonrpc == "2.0".
  if (!root.hasMember("jsonrpc") || !root["jsonrpc"].isString() ||
      root["jsonrpc"].toString() != "2.0") {
    last_error_ = "missing or invalid 'jsonrpc' field (expected \"2.0\")";
    return std::nullopt;
  }

  // Step 4: validate method.
  if (!root.hasMember("method") || !root["method"].isString()) {
    last_error_ = "missing or invalid 'method' field";
    return std::nullopt;
  }

  JsonRpcRequest request;
  request.method = root["method"].toString();

  // Step 5: extract id (string, number, or null).
  if (!root.hasMember("id")) {
    last_error_ = "missing 'id' field";
    return std::nullopt;
  }
  const JsonValue& id_value = root["id"];
  if (!id_value.isNull() && !id_value.isString() && !id_value.isInteger() &&
      !id_value.isDouble()) {
    last_error_ =
        "'id' must be a string, number, or null (Notification)";
    return std::nullopt;
  }
  request.id = id_value;

  // Step 6: extract optional params.
  if (root.hasMember("params")) {
    const JsonValue& params = root["params"];
    if (!params.isObject() && !params.isArray()) {
      last_error_ =
          "'params' must be a JSON object or array when present";
      return std::nullopt;
    }
    request.params = params;
  }

  return request;
}

}  // namespace nexus::mcp
