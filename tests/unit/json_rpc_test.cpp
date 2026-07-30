/// @file json_rpc_test.cpp
/// @brief Unit tests for the JSON-RPC 2.0 parser and serializer (TASK-020).

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "nexus/mcp/json_rpc.h"

namespace nexus::mcp {
namespace {

// ============================================================================
// nlohmann::ordered_json integration
// ============================================================================

TEST(JsonValueTest, Null) {
  JsonValue value;
  EXPECT_TRUE(value.is_null());
  EXPECT_EQ(value.dump(), "null");
}

TEST(JsonValueTest, PreservesObjectInsertionOrder) {
  JsonValue value = JsonValue::object();
  value["name"] = "test";
  value["count"] = 3;

  EXPECT_TRUE(value.is_object());
  EXPECT_TRUE(value.contains("name"));
  EXPECT_FALSE(value.contains("missing"));
  EXPECT_EQ(value.dump(), R"({"name":"test","count":3})");
}

TEST(JsonValueTest, ParsesUnicodeAndEscapes) {
  JsonValue value = JsonValue::parse(R"({"text":"\u4f60\u597d\nworld"})");
  EXPECT_EQ(value.at("text").get<std::string>(),
            "\xE4\xBD\xA0\xE5\xA5\xBD\nworld");
}

// ============================================================================
// JsonRpcRequest parsing
// ============================================================================

TEST(JsonRpcParserTest, ParseValidRequest) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"test.echo","id":1,"params":{}})");
  ASSERT_TRUE(req.has_value());
  EXPECT_EQ(req->method, "test.echo");
  EXPECT_TRUE(req->has_id);
  EXPECT_FALSE(req->isNotification());
  EXPECT_TRUE(req->id.is_number_integer());
  EXPECT_EQ(req->id.get<int64_t>(), 1);
}

TEST(JsonRpcParserTest, ParseRequestStringId) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"ping","id":"req-42"})");
  ASSERT_TRUE(req.has_value());
  EXPECT_EQ(req->method, "ping");
  EXPECT_TRUE(req->has_id);
  EXPECT_FALSE(req->isNotification());
  EXPECT_TRUE(req->id.is_string());
  EXPECT_EQ(req->id.get<std::string>(), "req-42");
}

TEST(JsonRpcParserTest, ParseNotificationWithoutId) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
  ASSERT_TRUE(req.has_value());
  EXPECT_FALSE(req->has_id);
  EXPECT_TRUE(req->isNotification());
}

TEST(JsonRpcParserTest, ParseNullIdRequest) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"ping","id":null})");
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->has_id);
  EXPECT_FALSE(req->isNotification());
  EXPECT_TRUE(req->id.is_null());
}

TEST(JsonRpcParserTest, RejectMissingJsonrpc) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(R"({"method":"foo","id":1})");
  EXPECT_FALSE(req.has_value());
  EXPECT_EQ(parser.lastErrorCode(), ErrorCode::kInvalidRequest);
  EXPECT_NE(parser.lastError().find("jsonrpc"), std::string::npos);
  EXPECT_EQ(parser.lastErrorId(), 1);
}

TEST(JsonRpcParserTest, RejectWrongVersion) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"1.0","method":"foo","id":1})");
  EXPECT_FALSE(req.has_value());
  EXPECT_EQ(parser.lastErrorCode(), ErrorCode::kInvalidRequest);
}

TEST(JsonRpcParserTest, RejectMissingMethod) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(R"({"jsonrpc":"2.0","id":1})");
  EXPECT_FALSE(req.has_value());
  EXPECT_EQ(parser.lastErrorCode(), ErrorCode::kInvalidRequest);
}

TEST(JsonRpcParserTest, RejectBatchArray) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(R"([{"jsonrpc":"2.0","method":"a","id":1}])");
  EXPECT_FALSE(req.has_value());
  EXPECT_EQ(parser.lastErrorCode(), ErrorCode::kInvalidRequest);
}

TEST(JsonRpcParserTest, RejectInvalidIdType) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"x","id":true})");
  EXPECT_FALSE(req.has_value());
  EXPECT_EQ(parser.lastErrorCode(), ErrorCode::kInvalidRequest);
}

TEST(JsonRpcParserTest, RejectInvalidParamsWithInvalidParamsCode) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"x","id":"req-1","params":true})");
  EXPECT_FALSE(req.has_value());
  EXPECT_EQ(parser.lastErrorCode(), ErrorCode::kInvalidParams);
  EXPECT_EQ(parser.lastErrorId(), "req-1");
  EXPECT_FALSE(parser.lastErrorIsNotification());
}

TEST(JsonRpcParserTest, MarksInvalidNotificationErrors) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"x","params":true})");
  EXPECT_FALSE(req.has_value());
  EXPECT_EQ(parser.lastErrorCode(), ErrorCode::kInvalidParams);
  EXPECT_TRUE(parser.lastErrorId().is_null());
  EXPECT_TRUE(parser.lastErrorIsNotification());
}

TEST(JsonRpcParserTest, RejectInvalidJson) {
  JsonRpcParser parser;
  auto req = parser.parseRequest("not json");
  EXPECT_FALSE(req.has_value());
  EXPECT_EQ(parser.lastErrorCode(), ErrorCode::kParseError);
  EXPECT_TRUE(parser.lastErrorId().is_null());
}

// ============================================================================
// Serialization
// ============================================================================

TEST(JsonRpcSerializerTest, SerializeSuccessResponse) {
  JsonValue id = 1;
  JsonValue result = JsonValue::object();
  result["value"] = "ok";

  const std::string text =
      JsonRpcSerializer::serializeResponse(id, result);
  EXPECT_NE(text.find("\"result\""), std::string::npos);
  EXPECT_NE(text.find("\"jsonrpc\":\"2.0\""), std::string::npos);
}

TEST(JsonRpcSerializerTest, SerializeErrorResponse) {
  JsonValue id = "req-1";

  const std::string text = JsonRpcSerializer::serializeError(
      id, -32601, "Method not found");
  EXPECT_NE(text.find("\"error\""), std::string::npos);
  EXPECT_NE(text.find("-32601"), std::string::npos);
}

TEST(JsonRpcSerializerTest, SerializeParseError) {
  const std::string text = JsonRpcSerializer::serializeParseError();
  EXPECT_NE(text.find("\"code\":-32700"), std::string::npos);
  EXPECT_NE(text.find("\"id\":null"), std::string::npos);
}

// ============================================================================
// JSON round-trip
// ============================================================================

TEST(JsonValueRoundTripTest, NestedObject) {
  JsonValue inner = JsonValue::object();
  inner["key"] = "val";
  JsonValue outer = JsonValue::object();
  outer["inner"] = std::move(inner);

  JsonRpcParser parser;
  auto request = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"_","id":1,"params":)" +
      outer.dump() + "}");
  ASSERT_TRUE(request.has_value());
  EXPECT_TRUE(request->params.is_object());
  EXPECT_TRUE(request->params.contains("inner"));
}

}  // namespace
}  // namespace nexus::mcp
