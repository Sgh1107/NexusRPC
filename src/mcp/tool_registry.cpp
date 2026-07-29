/// @file tool_registry.cpp
/// @brief Protobuf descriptor introspection for Tool Registry (TASK-021).

#include "nexus/mcp/tool_registry.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <set>
#include <sstream>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include "nexus/options.pb.h"

namespace nexus::mcp {
namespace {

using google::protobuf::FieldDescriptor;

// ---- Custom option accessors ----------------------------------------------

/// Reads the `tool_description` option from a method descriptor.
std::string getToolDescription(
    const google::protobuf::MethodDescriptor* method) {
  const auto& options = method->options();
  if (options.HasExtension(nexus::options::tool_description)) {
    return options.GetExtension(nexus::options::tool_description);
  }
  return method->service()->name() + std::string(".") + method->name();
}

/// Returns true unless the `tool_enabled` option is explicitly false.
bool isToolEnabled(const google::protobuf::MethodDescriptor* method) {
  const auto& options = method->options();
  if (options.HasExtension(nexus::options::tool_enabled)) {
    return options.GetExtension(nexus::options::tool_enabled);
  }
  return true;
}

/// Reads the `param_description` option from a field descriptor.
std::string getParamDescription(
    const google::protobuf::FieldDescriptor* field) {
  const auto& options = field->options();
  if (options.HasExtension(nexus::options::param_description)) {
    return options.GetExtension(nexus::options::param_description);
  }
  return {};
}

/// Reads the `param_required` option; defaults to false.
bool isParamRequired(const google::protobuf::FieldDescriptor* field) {
  const auto& options = field->options();
  if (options.HasExtension(nexus::options::param_required)) {
    return options.GetExtension(nexus::options::param_required);
  }
  return false;
}

// ---- JSON Schema type mapping ---------------------------------------------

std::string mapFieldType(const FieldDescriptor* field) {
  if (field->is_map()) return "object";
  if (field->is_repeated()) return "array";

  switch (field->type()) {
    case FieldDescriptor::TYPE_DOUBLE:
    case FieldDescriptor::TYPE_FLOAT:
      return "number";
    case FieldDescriptor::TYPE_INT64:
    case FieldDescriptor::TYPE_UINT64:
    case FieldDescriptor::TYPE_INT32:
    case FieldDescriptor::TYPE_FIXED64:
    case FieldDescriptor::TYPE_FIXED32:
    case FieldDescriptor::TYPE_UINT32:
    case FieldDescriptor::TYPE_SFIXED32:
    case FieldDescriptor::TYPE_SFIXED64:
    case FieldDescriptor::TYPE_SINT32:
    case FieldDescriptor::TYPE_SINT64:
      return "integer";
    case FieldDescriptor::TYPE_BOOL:
      return "boolean";
    case FieldDescriptor::TYPE_STRING:
      return "string";
    case FieldDescriptor::TYPE_BYTES:
      return "string";
    case FieldDescriptor::TYPE_ENUM:
      return "string";
    case FieldDescriptor::TYPE_MESSAGE: {
      const auto* md = field->message_type();
      const std::string& fn = md->full_name();
      if (fn == "google.protobuf.Timestamp") return "string";
      if (fn == "google.protobuf.Duration") return "string";
      if (fn == "google.protobuf.DoubleValue" ||
          fn == "google.protobuf.FloatValue")
        return "number";
      if (fn == "google.protobuf.Int64Value" ||
          fn == "google.protobuf.UInt64Value" ||
          fn == "google.protobuf.Int32Value" ||
          fn == "google.protobuf.UInt32Value")
        return "integer";
      if (fn == "google.protobuf.BoolValue") return "boolean";
      if (fn == "google.protobuf.StringValue" ||
          fn == "google.protobuf.BytesValue")
        return "string";
      return "object";
    }
    default:
      return "string";
  }
}

}  // namespace

// ============================================================================
// ToolRegistry static helpers
// ============================================================================

ToolParameter ToolRegistry::buildParameter(const FieldDescriptor* field) {
  ToolParameter param;
  param.name = field->json_name();
  param.description = getParamDescription(field);
  param.required = isParamRequired(field);
  param.type = mapFieldType(field);

  if (field->type() == FieldDescriptor::TYPE_ENUM) {
    const auto* ed = field->enum_type();
    std::vector<std::string> values;
    for (int i = 0; i < ed->value_count(); ++i)
      values.push_back(ed->value(i)->name());
    param.enum_values = std::move(values);
  }

  if (field->type() == FieldDescriptor::TYPE_BYTES)
    param.content_encoding = "base64";

  // Well-known type handling.
  if (field->type() == FieldDescriptor::TYPE_MESSAGE) {
    const auto* md = field->message_type();
    const std::string& fn = md->full_name();
    if (fn == "google.protobuf.Timestamp") {
      param.type = "string";
      param.description += " (RFC 3339)";
      return param;
    }
    if (fn == "google.protobuf.Duration") {
      param.type = "string";
      param.description += " (duration in seconds)";
      return param;
    }
    // Wrapper types unwrap.
    if (fn == "google.protobuf.DoubleValue" ||
        fn == "google.protobuf.FloatValue") {
      param.type = "number";
      return param;
    }
    if (fn == "google.protobuf.Int64Value" ||
        fn == "google.protobuf.UInt64Value" ||
        fn == "google.protobuf.Int32Value" ||
        fn == "google.protobuf.UInt32Value") {
      param.type = "integer";
      return param;
    }
    if (fn == "google.protobuf.BoolValue") {
      param.type = "boolean";
      return param;
    }
    if (fn == "google.protobuf.StringValue" ||
        fn == "google.protobuf.BytesValue") {
      param.type = "string";
      return param;
    }
  }

  // Nested message ¡ú object with properties.
  if (param.type == "object" &&
      field->type() == FieldDescriptor::TYPE_MESSAGE && !field->is_map()) {
    param.properties = buildSchema(field->message_type());
  }

  if (field->is_map()) param.type = "object";

  // Repeated ¡ú array with items.
  if (field->is_repeated() && !field->is_map()) {
    ToolParameter item;
    item.type = mapFieldType(field);
    item.description = getParamDescription(field);
    if (field->type() == FieldDescriptor::TYPE_MESSAGE) {
      item.properties = buildSchema(field->message_type());
      item.type = "object";
    }
    if (field->type() == FieldDescriptor::TYPE_ENUM) {
      const auto* ed = field->enum_type();
      std::vector<std::string> values;
      for (int i = 0; i < ed->value_count(); ++i)
        values.push_back(ed->value(i)->name());
      item.enum_values = std::move(values);
    }
    param.items = std::make_unique<ToolParameter>(std::move(item));
  }

  return param;
}

std::vector<ToolParameter> ToolRegistry::buildSchema(
    const google::protobuf::Descriptor* descriptor) {
  if (descriptor == nullptr) return {};
  std::vector<ToolParameter> params;
  params.reserve(descriptor->field_count());
  for (int i = 0; i < descriptor->field_count(); ++i)
    params.push_back(buildParameter(descriptor->field(i)));
  return params;
}

// ============================================================================
// Public API
// ============================================================================

rpc::Status ToolRegistry::registerTool(
    const google::protobuf::ServiceDescriptor* service,
    const google::protobuf::MethodDescriptor* method) {
  if (service == nullptr || method == nullptr)
    return rpc::Status(rpc::StatusCode::kInvalidArgument,
                       "descriptors must not be null");
  if (!isToolEnabled(method)) return rpc::Status::Ok();

  ToolDescriptor desc;
  desc.service_name = service->name();
  desc.method_name = method->name();

  std::ostringstream name;
  for (char c : desc.service_name) name << static_cast<char>(std::tolower(c));
  name << '.';
  for (char c : desc.method_name) name << static_cast<char>(std::tolower(c));
  desc.name = name.str();

  desc.description = getToolDescription(method);
  desc.request_descriptor = method->input_type();
  desc.response_descriptor = method->output_type();
  desc.input_schema = buildSchema(desc.request_descriptor);

  return registerTool(std::move(desc));
}

rpc::Status ToolRegistry::registerTool(ToolDescriptor desc) {
  if (desc.name.empty())
    return rpc::Status(rpc::StatusCode::kInvalidArgument,
                       "tool name must be non-empty");
  std::lock_guard<std::mutex> lock(mutex_);
  if (tools_.find(desc.name) != tools_.end())
    return rpc::Status(rpc::StatusCode::kInvalidArgument,
                       "tool '" + desc.name + "' is already registered");
  // Copy desc.name for the key so the stored ToolDescriptor retains its name.
  tools_.emplace(desc.name, std::move(desc));
  return rpc::Status::Ok();
}

std::vector<ToolDescriptor> ToolRegistry::listTools() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ToolDescriptor> result;
  result.reserve(tools_.size());
  for (const auto& [name, desc] : tools_) result.push_back(desc);
  return result;
}

const ToolDescriptor* ToolRegistry::findTool(std::string_view name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tools_.find(name);
  return it == tools_.end() ? nullptr : &it->second;
}

size_t ToolRegistry::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return tools_.size();
}

}  // namespace nexus::mcp


