/// @file tool_registry.cpp
/// @brief Protobuf descriptor introspection for Tool Registry (TASK-021).

#include "nexus/mcp/tool_registry.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <set>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include "nexus/options.pb.h"

namespace nexus::mcp {
namespace {

using google::protobuf::FieldDescriptor;

// ---- Identifier conversion ------------------------------------------------

/// Converts a CamelCase identifier to lowercase snake_case
/// ("GetCurrent" -> "get_current", "Weather" -> "weather").
///
/// The MCP tool name convention is `service_name.method_name` in lowercase
/// snake_case (docs/mcp.md §5), and the RPC transport registers service names
/// in lowercase (examples register "weather" / "echo").
std::string toSnakeCase(std::string_view input) {
  std::string output;
  output.reserve(input.size() + 4);
  for (std::size_t i = 0; i < input.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(input[i]);
    if (c >= 'A' && c <= 'Z') {
      const bool preceded_by_lower =
          i > 0 && (std::islower(static_cast<unsigned char>(input[i - 1])) ||
                    std::isdigit(static_cast<unsigned char>(input[i - 1])));
      const bool followed_by_lower =
          i + 1 < input.size() &&
          std::islower(static_cast<unsigned char>(input[i + 1]));
      if (i > 0 && (preceded_by_lower || followed_by_lower)) {
        output.push_back('_');
      }
      output.push_back(static_cast<char>(std::tolower(c)));
    } else {
      output.push_back(static_cast<char>(c));
    }
  }
  return output;
}

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

/// Maps a single element (non-container) field to its JSON Schema type,
/// including well-known message unwrapping.
std::string mapScalarType(const FieldDescriptor* field) {
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

/// Maps a field to its JSON Schema type (array/object for containers).
std::string mapFieldType(const FieldDescriptor* field) {
  if (field->is_map()) return "object";
  if (field->is_repeated()) return "array";
  return mapScalarType(field);
}

/// Returns the enum value names for a JSON Schema "enum" list.
std::vector<std::string> enumValueNames(
    const google::protobuf::EnumDescriptor* descriptor) {
  std::vector<std::string> values;
  values.reserve(descriptor->value_count());
  for (int i = 0; i < descriptor->value_count(); ++i) {
    values.push_back(descriptor->value(i)->name());
  }
  return values;
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

  const bool is_container = field->is_repeated() || field->is_map();
  const bool is_message = field->type() == FieldDescriptor::TYPE_MESSAGE;

  // Scalar attributes only apply to single-value parameters.
  if (!is_container && field->type() == FieldDescriptor::TYPE_ENUM) {
    param.enum_values = enumValueNames(field->enum_type());
  }
  if (!is_container && field->type() == FieldDescriptor::TYPE_BYTES) {
    param.content_encoding = "base64";
  }

  // Non-container message fields: unwrap well-known types or expand nested
  // objects into properties.
  if (!is_container && is_message) {
    const auto* md = field->message_type();
    const std::string& full_name = md->full_name();
    if (full_name == "google.protobuf.Timestamp") {
      param.type = "string";
      param.description += " (RFC 3339)";
    } else if (full_name == "google.protobuf.Duration") {
      param.type = "string";
      param.description += " (duration in seconds)";
    } else {
      param.type = mapScalarType(field);
      if (param.type == "object") {
        param.properties = buildSchema(md);
      }
    }
  }

  if (field->is_map()) param.type = "object";

  // Repeated fields expose the element schema in "items".
  if (field->is_repeated() && !field->is_map()) {
    ToolParameter item;
    item.type = mapScalarType(field);
    item.description = getParamDescription(field);
    if (is_message && item.type == "object") {
      item.properties = buildSchema(field->message_type());
    }
    if (field->type() == FieldDescriptor::TYPE_ENUM) {
      item.enum_values = enumValueNames(field->enum_type());
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
  // The RPC transport matches service names exactly; examples register them
  // in lowercase, so the descriptor-derived name is normalized here.
  desc.service_name = toSnakeCase(service->name());
  desc.method_name = method->name();

  desc.name = toSnakeCase(service->name()) + "." +
              toSnakeCase(method->name());

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
