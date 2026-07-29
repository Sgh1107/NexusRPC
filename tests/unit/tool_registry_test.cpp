/// @file tool_registry_test.cpp
/// @brief Unit tests for the Protobuf-driven Tool Registry (TASK-021).

#include <gtest/gtest.h>

#include "nexus/mcp/tool_registry.h"

namespace nexus::mcp {
namespace {

// ============================================================================
// Helper: construct a minimal ToolDescriptor by hand
// ============================================================================

ToolDescriptor makeWeatherTool() {
  ToolDescriptor desc;
  desc.name = "weather.getcurrent";
  desc.service_name = "Weather";
  desc.method_name = "GetCurrent";
  desc.description = "Get current weather for a city";

  ToolParameter city;
  city.name = "city";
  city.type = "string";
  city.description = "City name";
  city.required = true;
  desc.input_schema.push_back(std::move(city));

  return desc;
}

ToolDescriptor makeEchoTool() {
  ToolDescriptor desc;
  desc.name = "echo.echo";
  desc.service_name = "Echo";
  desc.method_name = "Echo";
  desc.description = "Echo the request message";

  ToolParameter msg;
  msg.name = "message";
  msg.type = "string";
  msg.description = "Message to echo";
  desc.input_schema.push_back(std::move(msg));

  return desc;
}

// ============================================================================
// Registration
// ============================================================================

TEST(ToolRegistryTest, RegisterManualDescriptor) {
  ToolRegistry registry;
  auto desc = makeWeatherTool();
  auto status = registry.registerTool(std::move(desc));
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(registry.size(), 1U);

  const auto* tool = registry.findTool("weather.getcurrent");
  ASSERT_NE(tool, nullptr);
  EXPECT_EQ(tool->service_name, "Weather");
  EXPECT_EQ(tool->method_name, "GetCurrent");
}

TEST(ToolRegistryTest, ListToolsSorted) {
  ToolRegistry registry;
  ASSERT_TRUE(registry.registerTool(makeWeatherTool()).ok());
  ASSERT_TRUE(registry.registerTool(makeEchoTool()).ok());
  EXPECT_EQ(registry.size(), 2U);

  auto tools = registry.listTools();
  ASSERT_EQ(tools.size(), 2U);
  // Sorted by name: "echo.echo" < "weather.getcurrent".
  EXPECT_EQ(tools[0].name, "echo.echo");
  EXPECT_EQ(tools[1].name, "weather.getcurrent");
}

TEST(ToolRegistryTest, FindMissingTool) {
  ToolRegistry registry;
  EXPECT_EQ(registry.findTool("nonexistent.tool"), nullptr);
}

TEST(ToolRegistryTest, RejectDuplicate) {
  ToolRegistry registry;
  EXPECT_TRUE(registry.registerTool(makeEchoTool()).ok());
  auto duplicate = registry.registerTool(makeEchoTool());
  EXPECT_FALSE(duplicate.ok());
  EXPECT_EQ(duplicate.code(), rpc::StatusCode::kInvalidArgument);
}

// ============================================================================
// Schema validation
// ============================================================================

TEST(ToolRegistryTest, WeatherSchemaHasCityParameter) {
  ToolRegistry registry;
  ASSERT_TRUE(registry.registerTool(makeWeatherTool()).ok());

  const auto* tool = registry.findTool("weather.getcurrent");
  ASSERT_NE(tool, nullptr);
  EXPECT_EQ(tool->input_schema.size(), 1U);
  EXPECT_EQ(tool->input_schema[0].name, "city");
  EXPECT_EQ(tool->input_schema[0].type, "string");
  EXPECT_TRUE(tool->input_schema[0].required);
}

TEST(ToolRegistryTest, EchoSchemaGeneratesCorrectToolName) {
  ToolRegistry registry;
  ASSERT_TRUE(registry.registerTool(makeEchoTool()).ok());

  const auto* tool = registry.findTool("echo.echo");
  ASSERT_NE(tool, nullptr);
  EXPECT_FALSE(tool->description.empty());
  EXPECT_EQ(tool->input_schema[0].name, "message");
}

}  // namespace
}  // namespace nexus::mcp
