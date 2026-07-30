/// @file mcp_server.cpp
/// @brief MCP stdio gateway implementation (TASK-022).

#include "nexus/mcp/mcp_server.h"

#include <cassert>
#include <iostream>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/util/json_util.h>

#include "nexus/observability/logging.h"
#include "nexus/rpc/status.h"

namespace nexus::mcp {

// ============================================================================

// Impl
// ============================================================================

struct McpServer::Impl {
  rpc::RpcClient* rpc_client;
  ToolRegistry* registry;
  std::string server_name;
  std::string server_version;
  McpSession session;
  JsonRpcParser parser;
  std::atomic<bool> stopped{false};
};

// ============================================================================
// Construction / destruction
// ============================================================================

McpServer::McpServer(rpc::RpcClient* rpc_client, ToolRegistry* registry,
                     std::string server_name, std::string server_version)
    : impl_(std::make_unique<Impl>()) {
  impl_->rpc_client = rpc_client;
  impl_->registry = registry;
  impl_->server_name = std::move(server_name);
  impl_->server_version = std::move(server_version);
}

McpServer::~McpServer() = default;

// ============================================================================
// run / stop
// ============================================================================

void McpServer::run() {
  NEXUS_LOG_INFO("mcp stdio server started");
  std::string line;
  line.reserve(4096);

  while (!impl_->stopped && std::getline(std::cin, line)) {

    if (line.empty()) continue;

        const auto parsed = impl_->parser.parseRequest(line);
    if (!parsed.has_value()) {

      NEXUS_LOG_WARN("mcp parse error: {}", impl_->parser.lastError());
            std::cout << JsonRpcSerializer::serializeParseError() << std::endl;
      continue;
    }


    const std::string response = dispatch(*parsed);
    if (!response.empty()) {
      std::cout << response << std::endl;
    }
  }

      impl_->stopped = true;
  NEXUS_LOG_INFO("mcp stdio server stopped");

}

void McpServer::stop() {
  NEXUS_LOG_INFO("mcp stdio server stopping");
  impl_->stopped = true;
}

bool McpServer::isStopped() const noexcept { return impl_->stopped; }

// ============================================================================

// dispatch
// ============================================================================

std::string McpServer::dispatch(const JsonRpcRequest& request) {
  const std::string& method = request.method;

  // Lifecycle enforcement: every method except initialize and
  // notifications/initialized requires the server to be initialized.
  if (method != "initialize" && method != "notifications/initialized" &&
      impl_->session.state != McpSession::State::kInitialized) {
    return JsonRpcSerializer::serializeError(
        request.id, -32002, "Server not initialized");
  }

  if (method == "initialize") return handleInitialize(request);
  if (method == "notifications/initialized") return handleInitialized(request);
  if (method == "ping") return handlePing(request);
  if (method == "tools/list") return handleToolsList(request);
  if (method == "tools/call") return handleToolsCall(request);

        NEXUS_LOG_WARN("mcp unknown method={}", method);

    return JsonRpcSerializer::serializeError(
      request.id, ErrorCode::kMethodNotFound,
      "Unknown method: " + method);

}

// ============================================================================
// handleInitialize
// ============================================================================

std::string McpServer::handleInitialize(const JsonRpcRequest& request) {
  if (impl_->session.state != McpSession::State::kUninitialized) {
    return JsonRpcSerializer::serializeError(
        request.id, ErrorCode::kInvalidRequest,
        "Server is already initialized");
  }

  impl_->session.state = McpSession::State::kInitialized;

    JsonValue capabilities = JsonValue::object();

  JsonValue tools_cap = JsonValue::object();

  tools_cap["listChanged"] = false;
  capabilities["tools"] = std::move(tools_cap);

  JsonValue result = JsonValue::object();
  result["protocolVersion"] = "2024-11-05";
  JsonValue server_info = JsonValue::object();
  server_info["name"] = impl_->server_name;
  server_info["version"] = impl_->server_version;
  result["serverInfo"] = std::move(server_info);
  result["capabilities"] = std::move(capabilities);

  return JsonRpcSerializer::serializeResponse(request.id, std::move(result));
}


// ============================================================================
// handleInitialized
// ============================================================================

std::string McpServer::handleInitialized(const JsonRpcRequest& /*request*/) {
  NEXUS_LOG_DEBUG("mcp initialized notification received");
  return {};  // Notification: no response.
}


// ============================================================================
// handlePing
// ============================================================================

std::string McpServer::handlePing(const JsonRpcRequest& request) {
  return JsonRpcSerializer::serializeResponse(request.id, JsonValue::object());
}

// ============================================================================
// handleToolsList

// ============================================================================

std::string McpServer::handleToolsList(const JsonRpcRequest& request) {
  const auto tools = impl_->registry->listTools();

    JsonValue tools_array = JsonValue::array();

  for (const auto& desc : tools) {

    tools_array.push_back(buildToolSchema(desc));
  }

  JsonValue result = JsonValue::object();
  result["tools"] = std::move(tools_array);

  return JsonRpcSerializer::serializeResponse(request.id, std::move(result));
}


// ============================================================================
// handleToolsCall
// ============================================================================

std::string McpServer::handleToolsCall(const JsonRpcRequest& request) {
  if (!request.params.is_object() || !request.params.contains("name") ||
      !request.params["name"].is_string()) {


    return JsonRpcSerializer::serializeError(
        request.id, ErrorCode::kInvalidParams,
        "params must include 'name'");
  }

    const std::string tool_name = request.params["name"].get<std::string>();

  const ToolDescriptor* tool = impl_->registry->findTool(tool_name);

  if (tool == nullptr) {
    NEXUS_LOG_WARN("mcp tool not found tool={}", tool_name);
    return JsonRpcSerializer::serializeError(

        request.id, ErrorCode::kMethodNotFound,
        "Tool not found: " + tool_name);
  }

    JsonValue arguments = JsonValue::object();

  if (request.params.contains("arguments") &&
      request.params["arguments"].is_object()) {
    arguments = request.params["arguments"];
  }

  // Convert JSON arguments to Protobuf.

  auto proto_body = jsonToProtobuf(arguments, tool->request_descriptor);
  if (!proto_body.ok()) {
    return JsonRpcSerializer::serializeError(
        request.id, ErrorCode::kInvalidParams,
        "Failed to convert arguments: " + proto_body.status().message());
  }

  // Call the RPC backend.
    auto rpc_result = impl_->rpc_client->call(

      tool->service_name, tool->method_name, proto_body.value());
  if (!rpc_result.ok()) {
    NEXUS_LOG_WARN("mcp rpc call failed tool={} status={} message={}", tool_name,
                   static_cast<int>(rpc_result.status().code()),
                   rpc_result.status().message());

    int mcp_code = ErrorCode::kInternalError;

    switch (rpc_result.status().code()) {
      case rpc::StatusCode::kNotFound:
        mcp_code = ErrorCode::kMethodNotFound;
        break;
      case rpc::StatusCode::kInvalidArgument:
        mcp_code = ErrorCode::kInvalidParams;
        break;
      case rpc::StatusCode::kDeadlineExceeded:
        mcp_code = -32001;
        break;
      case rpc::StatusCode::kUnavailable:
        mcp_code = -32002;
        break;
      default:
        break;
    }
    return JsonRpcSerializer::serializeError(
        request.id, mcp_code,
        "RPC call failed: " + rpc_result.status().message());
  }

  // Check response status embedded in metadata.
  const auto& meta = rpc_result.value().metadata;
  auto code_it = meta.find("nexus.status_code");
  if (code_it != meta.end() && code_it->second != "0") {
    auto msg_it = meta.find("nexus.status_message");
    const std::string detail =
        msg_it != meta.end() ? msg_it->second : "handler returned an error";
    return JsonRpcSerializer::serializeError(
        request.id, ErrorCode::kInternalError, detail);
  }

    // Convert Protobuf response to JSON.

  auto json_result =
      protobufToJson(rpc_result.value().body, tool->response_descriptor);
  if (!json_result.ok()) {
    return JsonRpcSerializer::serializeError(
        request.id, ErrorCode::kInternalError,
        "Failed to convert response: " + json_result.status().message());
  }

  // Build MCP result: content[0].text + structuredContent.
    JsonValue content = JsonValue::array();

  JsonValue text_item = JsonValue::object();
  text_item["type"] = "text";
  text_item["text"] = json_result.value().dump();
  content.push_back(std::move(text_item));

  JsonValue result = JsonValue::object();
  result["content"] = std::move(content);
  result["structuredContent"] = std::move(json_result).value();

  return JsonRpcSerializer::serializeResponse(request.id, std::move(result));
}


// ============================================================================
// buildToolSchema / buildPropertySchema
// ============================================================================

JsonValue McpServer::buildToolSchema(const ToolDescriptor& desc) {
  JsonValue tool = JsonValue::object();
  tool["name"] = desc.name;
  tool["description"] = desc.description;

  JsonValue schema = JsonValue::object();
  schema["type"] = "object";

  JsonValue properties = JsonValue::object();
  JsonValue required = JsonValue::array();
  for (const auto& param : desc.input_schema) {
    properties[param.name] = buildPropertySchema(param);
    if (param.required) {
      required.push_back(param.name);
    }
  }
  schema["properties"] = std::move(properties);
  if (!required.empty()) {
    schema["required"] = std::move(required);
  }

  tool["inputSchema"] = std::move(schema);
  return tool;
}

JsonValue McpServer::buildPropertySchema(const ToolParameter& param) {
  JsonValue property = JsonValue::object();
  property["type"] = param.type;
  if (!param.description.empty()) {
    property["description"] = param.description;
  }
  if (!param.content_encoding.empty()) {
    property["contentEncoding"] = param.content_encoding;
  }

  if (param.enum_values.has_value()) {
    JsonValue values = JsonValue::array();
    for (const auto& value : *param.enum_values) {
      values.push_back(value);
    }
    property["enum"] = std::move(values);
  }

  if (!param.properties.empty()) {
    JsonValue nested_properties = JsonValue::object();
    JsonValue nested_required = JsonValue::array();
    for (const auto& nested : param.properties) {
      nested_properties[nested.name] = buildPropertySchema(nested);
      if (nested.required) {
        nested_required.push_back(nested.name);
      }
    }
    property["properties"] = std::move(nested_properties);
    if (!nested_required.empty()) {
      property["required"] = std::move(nested_required);
    }
  }

  if (param.items != nullptr) {
    property["items"] = buildPropertySchema(*param.items);
  }

  return property;
}

// ============================================================================
// JSON to Protobuf

// ============================================================================

rpc::Result<std::string> McpServer::jsonToProtobuf(
    const JsonValue& json,
    const google::protobuf::Descriptor* descriptor) {
  if (descriptor == nullptr) {
    return rpc::Status(rpc::StatusCode::kInvalidArgument,
                       "descriptor must not be null");
  }

  google::protobuf::DynamicMessageFactory factory;
  auto message = std::unique_ptr<google::protobuf::Message>(
      factory.GetPrototype(descriptor)->New());

  google::protobuf::util::JsonParseOptions options;
  options.ignore_unknown_fields = false;

    const std::string json_text = json.dump();


  const auto status = google::protobuf::util::JsonStringToMessage(
      json_text, message.get(), options);
  if (!status.ok()) {
    return rpc::Status(rpc::StatusCode::kInvalidArgument,
                       status.ToString());
  }

  std::string binary;
  if (!message->SerializeToString(&binary)) {
    return rpc::Status(rpc::StatusCode::kInternal,
                       "failed to serialize Protobuf message");
  }
  return binary;
}

rpc::Result<JsonValue> McpServer::protobufToJson(
    const std::string& binary,
    const google::protobuf::Descriptor* descriptor) {
  if (descriptor == nullptr) {
    return rpc::Status(rpc::StatusCode::kInvalidArgument,
                       "descriptor must not be null");
  }

  google::protobuf::DynamicMessageFactory factory;
  auto message = std::unique_ptr<google::protobuf::Message>(
      factory.GetPrototype(descriptor)->New());

  if (!message->ParseFromString(binary)) {
    return rpc::Status(rpc::StatusCode::kInternal,
                       "failed to parse Protobuf response");
  }

  google::protobuf::util::JsonPrintOptions options;
  options.always_print_primitive_fields = true;
  options.preserve_proto_field_names = false;

  std::string json_text;
  const auto status =
      google::protobuf::util::MessageToJsonString(*message, &json_text,
                                                   options);
  if (!status.ok()) {
    return rpc::Status(rpc::StatusCode::kInternal, status.ToString());
  }

    JsonValue json = JsonValue::parse(json_text, nullptr, false);

  if (json.is_discarded()) {
    return rpc::Status(rpc::StatusCode::kInternal,
                       "failed to parse JSON response from Protobuf");
  }
  return json;
}


}  // namespace nexus::mcp

