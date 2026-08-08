/// @file tool_registry_test.cpp
/// @brief Unit tests for the Protobuf-driven Tool Registry (TASK-021).

#include <gtest/gtest.h>

#include <google/protobuf/descriptor.h>

#include "examples/echo.pb.h"
#include "examples/weather.pb.h"
#include "nexus/mcp/tool_registry.h"
#include "schema_test.pb.h"

namespace nexus::mcp {
namespace {

// ============================================================================
// Helper: construct a minimal ToolDescriptor by hand
// ============================================================================

ToolDescriptor makeWeatherTool() {
  ToolDescriptor desc;
  desc.name = "weather.get_current";
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

  const auto* tool = registry.findTool("weather.get_current");
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
  // Sorted by name: "echo.echo" < "weather.get_current".
  EXPECT_EQ(tools[0].name, "echo.echo");
  EXPECT_EQ(tools[1].name, "weather.get_current");
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

  const auto* tool = registry.findTool("weather.get_current");
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

// ============================================================================
// Descriptor-driven registration and schema generation (TASK-021)
// ============================================================================

TEST(ToolRegistryTest, DescriptorDrivenWeatherTool) {
  ToolRegistry registry;
  // GetCurrentRequest::descriptor() also forces weather.pb.o to be linked,
  // keeping the generated file descriptor registered.
  const auto* service =
      nexus::examples::weather::GetCurrentRequest::descriptor()
          ->file()
          ->FindServiceByName("Weather");
  ASSERT_NE(service, nullptr);
  ASSERT_EQ(service->method_count(), 1);

  auto status = registry.registerTool(service, service->method(0));
  EXPECT_TRUE(status.ok()) << status.message();
  EXPECT_EQ(registry.size(), 1U);

  const auto* tool = registry.findTool("weather.get_current");
  ASSERT_NE(tool, nullptr);
  EXPECT_EQ(tool->service_name, "weather");
  EXPECT_EQ(tool->method_name, "GetCurrent");
  EXPECT_EQ(tool->description,
            "Returns the current weather for the requested city.");
  EXPECT_NE(tool->request_descriptor, nullptr);
  EXPECT_NE(tool->response_descriptor, nullptr);

  ASSERT_EQ(tool->input_schema.size(), 1U);
  const ToolParameter& city = tool->input_schema[0];
  EXPECT_EQ(city.name, "city");
  EXPECT_EQ(city.type, "string");
  EXPECT_TRUE(city.required);
  EXPECT_EQ(city.description,
            "City name (e.g. \"Beijing\", \"London\", \"Tokyo\")");
}

TEST(ToolRegistryTest, DescriptorDrivenEchoTool) {
  ToolRegistry registry;
  const auto* service =
      nexus::examples::echo::EchoRequest::descriptor()
          ->file()
          ->FindServiceByName("Echo");
  ASSERT_NE(service, nullptr);
  ASSERT_TRUE(registry.registerTool(service, service->method(0)).ok());

  const auto* tool = registry.findTool("echo.echo");
  ASSERT_NE(tool, nullptr);
  EXPECT_EQ(tool->service_name, "echo");
  EXPECT_EQ(tool->method_name, "Echo");
  EXPECT_EQ(tool->description,
            "Echoes the request payload unchanged.");
  ASSERT_EQ(tool->input_schema.size(), 1U);
  EXPECT_EQ(tool->input_schema[0].name, "message");
  EXPECT_TRUE(tool->input_schema[0].required);
}

TEST(ToolRegistryTest, DescriptorSchemaCoversAllTypes) {
  ToolRegistry registry;
  const auto* service =
      nexus::mcp::test::RunRequest::descriptor()
          ->file()
          ->FindServiceByName("SchemaTestService");
  ASSERT_NE(service, nullptr);
  ASSERT_TRUE(registry.registerTool(service, service->method(0)).ok());

  const auto* tool = registry.findTool("schema_test_service.run");
  ASSERT_NE(tool, nullptr);
  EXPECT_EQ(tool->description, "Runs the schema test.");

  const auto find = [&tool](const char* name) -> const ToolParameter* {
    for (const auto& p : tool->input_schema) {
      if (p.name == name) return &p;
    }
    return nullptr;
  };

  {
    const auto* p = find("name");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "string");
    EXPECT_TRUE(p->required);
    EXPECT_EQ(p->description, "Unique tool name.");
  }
  {
    const auto* p = find("count");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "integer");
    EXPECT_FALSE(p->required);
  }
  {
    const auto* p = find("ratio");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "number");
  }
  {
    const auto* p = find("enabled");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "boolean");
  }
  {
    const auto* p = find("payload");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "string");
    EXPECT_EQ(p->content_encoding, "base64");
  }
  {
    const auto* p = find("color");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "string");
    ASSERT_TRUE(p->enum_values.has_value());
    ASSERT_EQ(p->enum_values->size(), 3U);
    EXPECT_EQ((*p->enum_values)[0], "COLOR_UNSPECIFIED");
    EXPECT_EQ((*p->enum_values)[1], "RED");
    EXPECT_EQ((*p->enum_values)[2], "GREEN");
  }
  {
    // json_name conversion: created_at -> createdAt.
    const auto* p = find("createdAt");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "string");
    EXPECT_NE(p->description.find("RFC 3339"), std::string::npos);
  }
  {
    const auto* p = find("ttl");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "string");
  }
  {
    const auto* p = find("nickname");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "string");
  }
  {
    const auto* p = find("nested");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "object");
    ASSERT_EQ(p->properties.size(), 1U);
    EXPECT_EQ(p->properties[0].name, "key");
    EXPECT_EQ(p->properties[0].type, "string");
  }
  {
    const auto* p = find("tags");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "array");
    ASSERT_NE(p->items, nullptr);
    EXPECT_EQ(p->items->type, "string");
  }
  {
    const auto* p = find("scores");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "object");
  }
  {
    // repeated Timestamp unwraps to an array of RFC 3339 strings.
    const auto* p = find("events");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "array");
    ASSERT_NE(p->items, nullptr);
    EXPECT_EQ(p->items->type, "string");
  }
  {
    // repeated nested message -> array of objects with properties.
    const auto* p = find("children");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "array");
    ASSERT_NE(p->items, nullptr);
    EXPECT_EQ(p->items->type, "object");
    ASSERT_EQ(p->items->properties.size(), 1U);
    EXPECT_EQ(p->items->properties[0].name, "key");
    EXPECT_EQ(p->items->properties[0].type, "string");
  }
  {
    // proto3 optional field maps to a plain optional schema parameter.
    const auto* p = find("note");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->type, "string");
    EXPECT_FALSE(p->required);
  }
}

TEST(ToolRegistryTest, DescriptorDisabledToolIsSkipped) {
  ToolRegistry registry;
  const auto* service =
      nexus::mcp::test::RunRequest::descriptor()
          ->file()
          ->FindServiceByName("SchemaTestService");
  ASSERT_NE(service, nullptr);

  const google::protobuf::MethodDescriptor* hidden = nullptr;
  for (int i = 0; i < service->method_count(); ++i) {
    if (service->method(i)->name() == "Hidden") hidden = service->method(i);
  }
  ASSERT_NE(hidden, nullptr);

  EXPECT_TRUE(registry.registerTool(service, hidden).ok());
  EXPECT_EQ(registry.size(), 0U);
}

}  // namespace
}  // namespace nexus::mcp
