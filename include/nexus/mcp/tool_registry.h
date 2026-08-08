/// @file tool_registry.h
/// @brief Protobuf-driven Tool Registry (TASK-021).

#ifndef NEXUS_MCP_TOOL_REGISTRY_H_
#define NEXUS_MCP_TOOL_REGISTRY_H_

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "nexus/mcp/json_rpc.h"
#include "nexus/rpc/rpc_client.h"
#include "nexus/rpc/rpc_message.h"
#include "nexus/rpc/status.h"

namespace nexus::mcp {

// ============================================================================
// ToolParameter
// ============================================================================

struct ToolParameter {
  std::string name;
  std::string type;
  std::string description;
  bool required = false;
  std::vector<ToolParameter> properties;
  std::unique_ptr<ToolParameter> items;
  std::optional<std::vector<std::string>> enum_values;
  std::string content_encoding;
  std::string default_value;

  ToolParameter() = default;

  /// Deep-copy constructor (clones the owned `items` sub-tree).
  ToolParameter(const ToolParameter& other)
      : name(other.name),
        type(other.type),
        description(other.description),
        required(other.required),
        properties(other.properties),
        items(other.items
                  ? std::make_unique<ToolParameter>(*other.items)
                  : nullptr),
        enum_values(other.enum_values),
        content_encoding(other.content_encoding),
        default_value(other.default_value) {}

  ToolParameter& operator=(const ToolParameter& other) {
    if (this != &other) {
      name = other.name;
      type = other.type;
      description = other.description;
      required = other.required;
      properties = other.properties;
      items = other.items
                  ? std::make_unique<ToolParameter>(*other.items)
                  : nullptr;
      enum_values = other.enum_values;
      content_encoding = other.content_encoding;
      default_value = other.default_value;
    }
    return *this;
  }

  ToolParameter(ToolParameter&&) = default;
  ToolParameter& operator=(ToolParameter&&) = default;
};

// ============================================================================
// ToolDescriptor
// ============================================================================

struct ToolDescriptor {
  std::string name;
  std::string service_name;
  std::string method_name;
  std::string description;
  std::vector<ToolParameter> input_schema;
  const google::protobuf::Descriptor* request_descriptor = nullptr;
  const google::protobuf::Descriptor* response_descriptor = nullptr;
};

// ============================================================================
// ToolRegistry
// ============================================================================

class ToolRegistry {
 public:
  ToolRegistry() = default;
  ~ToolRegistry() = default;

  ToolRegistry(const ToolRegistry&) = delete;
  ToolRegistry& operator=(const ToolRegistry&) = delete;

  rpc::Status registerTool(
      const google::protobuf::ServiceDescriptor* service_descriptor,
      const google::protobuf::MethodDescriptor* method_descriptor);

  rpc::Status registerTool(ToolDescriptor descriptor);

  std::vector<ToolDescriptor> listTools() const;
  const ToolDescriptor* findTool(std::string_view name) const;
  size_t size() const;

 private:
  static ToolParameter buildParameter(
      const google::protobuf::FieldDescriptor* field);
  static std::vector<ToolParameter> buildSchema(
      const google::protobuf::Descriptor* descriptor);

  mutable std::mutex mutex_;
  std::map<std::string, ToolDescriptor, std::less<>> tools_;
};

}  // namespace nexus::mcp

#endif  // NEXUS_MCP_TOOL_REGISTRY_H_
