/// @file json_rpc_test.cpp
/// @brief Unit tests for the JSON-RPC 2.0 parser and serializer (TASK-020).

#include <gtest/gtest.h>

#include "nexus/mcp/json_rpc.h"

namespace nexus::mcp {
namespace {

// ============================================================================
// JsonValue tests
// ============================================================================

TEST(JsonValueTest, Null) {
  JsonValue v;
  EXPECT_TRUE(v.isNull());
  EXPECT_EQ(v.serialize(), "null");
}

TEST(JsonValueTest, Bool) {
  EXPECT_EQ(JsonValue(true).serialize(), "true");
  EXPECT_EQ(JsonValue(false).serialize(), "false");
}

TEST(JsonValueTest, Integer) {
  JsonValue v(static_cast<int64_t>(42));
  EXPECT_TRUE(v.isInteger());
  EXPECT_EQ(v.toInteger(), 42);
  EXPECT_EQ(v.serialize(), "42");
}

TEST(JsonValueTest, NegativeInteger) {
  EXPECT_EQ(JsonValue(static_cast<int64_t>(-7)).serialize(), "-7");
}

TEST(JsonValueTest, Double) {
  JsonValue v(3.14);
  EXPECT_TRUE(v.isDouble());
  EXPECT_DOUBLE_EQ(v.toDouble(), 3.14);
}

TEST(JsonValueTest, String) {
  JsonValue v("hello");
  EXPECT_TRUE(v.isString());
  EXPECT_EQ(v.toString(), "hello");
  EXPECT_EQ(v.serialize(), "\"hello\"");
}

TEST(JsonValueTest, StringEscape) {
  JsonValue v("line1\nline2\t\"quoted\"");
  EXPECT_EQ(v.serialize(), "\"line1\\nline2\\t\\\"quoted\\\"\"");
}

TEST(JsonValueTest, EmptyObject) {
  auto obj = JsonValue::MakeObject();
  EXPECT_TRUE(obj.isObject());
  EXPECT_EQ(obj.size(), 0U);
  EXPECT_EQ(obj.serialize(), "{}");
}

TEST(JsonValueTest, ObjectWithMembers) {
  auto obj = JsonValue::MakeObject();
  obj.set("name", JsonValue("test"));
  obj.set("count", JsonValue(static_cast<int64_t>(3)));
  EXPECT_EQ(obj.size(), 2U);
  EXPECT_TRUE(obj.hasMember("name"));
  EXPECT_FALSE(obj.hasMember("missing"));
}

TEST(JsonValueTest, ArrayPushAndAccess) {
  auto arr = JsonValue::MakeArray();
  arr.push(JsonValue(static_cast<int64_t>(1)));
  arr.push(JsonValue("two"));
  EXPECT_EQ(arr.size(), 2U);
  EXPECT_EQ(arr[0].toInteger(), 1);
  EXPECT_EQ(arr[1].toString(), "two");
  EXPECT_EQ(arr.serialize(), "[1,\"two\"]");
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
  EXPECT_TRUE(req->id.isInteger());
  EXPECT_EQ(req->id.toInteger(), 1);
}

TEST(JsonRpcParserTest, ParseRequestStringId) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"ping","id":"req-42"})");
  ASSERT_TRUE(req.has_value());
  EXPECT_EQ(req->method, "ping");
  EXPECT_TRUE(req->id.isString());
  EXPECT_EQ(req->id.toString(), "req-42");
}

TEST(JsonRpcParserTest, ParseNotification) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"notifications/initialized","id":null})");
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->isNotification());
}

TEST(JsonRpcParserTest, ParseNotificationWithoutId) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->isNotification());
  EXPECT_TRUE(req->id.isNull());
}

TEST(JsonRpcParserTest, RejectMissingJsonrpc) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(R"({"method":"foo","id":1})");
  EXPECT_FALSE(req.has_value());
  EXPECT_NE(parser.lastError().find("jsonrpc"), std::string::npos);
}

TEST(JsonRpcParserTest, RejectWrongVersion) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"1.0","method":"foo","id":1})");
  EXPECT_FALSE(req.has_value());
}

TEST(JsonRpcParserTest, RejectMissingMethod) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(R"({"jsonrpc":"2.0","id":1})");
  EXPECT_FALSE(req.has_value());
}

TEST(JsonRpcParserTest, RejectBatchArray) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(R"([{"jsonrpc":"2.0","method":"a","id":1}])");
  EXPECT_FALSE(req.has_value());
}

TEST(JsonRpcParserTest, RejectInvalidIdType) {
  JsonRpcParser parser;
  auto req = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"x","id":true})");
  EXPECT_FALSE(req.has_value());
}

TEST(JsonRpcParserTest, RejectInvalidJson) {
  JsonRpcParser parser;
  auto req = parser.parseRequest("not json");
  EXPECT_FALSE(req.has_value());
}

// ============================================================================
// Serialization
// ============================================================================

TEST(JsonRpcSerializerTest, SerializeSuccessResponse) {
  auto id = JsonValue(static_cast<int64_t>(1));
  auto result = JsonValue::MakeObject();
  result.set("value", JsonValue("ok"));
  const std::string text =
      JsonRpcSerializer::serializeResponse(id, result);
  EXPECT_NE(text.find("\"result\""), std::string::npos);
  EXPECT_NE(text.find("\"jsonrpc\":\"2.0\""), std::string::npos);
}

TEST(JsonRpcSerializerTest, SerializeErrorResponse) {
  auto id = JsonValue("req-1");
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
// JsonValue round-trip (serialize then parse)
// ============================================================================

TEST(JsonValueRoundTripTest, NestedObject) {
  auto inner = JsonValue::MakeObject();
  inner.set("key", JsonValue("val"));
  auto outer = JsonValue::MakeObject();
  outer.set("inner", std::move(inner));

  const std::string text = outer.serialize();
  JsonRpcParser parser;
  // Wrap in a valid JSON-RPC envelope to re-parse via the existing parser.
  auto req = parser.parseRequest(
      R"({"jsonrpc":"2.0","method":"_","id":1,"params":)" + text + "}");
  ASSERT_TRUE(req.has_value());
  EXPECT_TRUE(req->params.isObject());
  EXPECT_TRUE(req->params.hasMember("inner"));
}

}  // namespace
}  // namespace nexus::mcp
