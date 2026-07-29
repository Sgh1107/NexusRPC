/// @file mcp_server.cpp
/// @brief MCP stdio gateway implementation (TASK-022).

#include "nexus/mcp/mcp_server.h"

#include <cassert>
#include <iostream>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/util/json_util.h>

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
  std::string line;
  line.reserve(4096);

  while (!impl_->stopped && std::getline(std::cin, line)) {
    if (line.empty()) continue;

    const auto parsed = impl_->parser.parseRequest(line);
    if (!parsed.has_value()) {
      std::cerr << "[mcp] parse error: " << impl_->parser.lastError()
                << std::endl;
      std::cout << JsonRpcSerializer::serializeParseError() << std::endl;
      continue;
    }

    const std::string response = dispatch(*parsed);
    if (!response.empty()) {
      std::cout << response << std::endl;
    }
  }

  impl_->stopped = true;
}

void McpServer::stop() { impl_->stopped = true; }

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

  std::cerr << "[mcp] unknown method: " << method << std::endl;
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

  auto capabilities = JsonValue::MakeObject();
  auto tools_cap = JsonValue::MakeObject();
  tools_cap.set("listChanged", JsonValue(false));
  capabilities.set("tools", std::move(tools_cap));

  auto result = JsonValue::MakeObject();
  result.set("protocolVersion", JsonValue("2024-11-05"));
  result.set("serverInfo",
             JsonValue::MakeObject()
                 .set("name", JsonValue(impl_->server_name))
                 .set("version", JsonValue(impl_->server_version)));
  result.set("capabilities", std::move(capabilities));

  return JsonRpcSerializer::serializeResponse(request.id, std::move(result));
}

// ============================================================================
// handleInitialized
// ============================================================================

std::string McpServer::handleInitialized(const JsonRpcRequest& /*request*/) {
  std::cerr << "[mcp] client sent initialized notification" << std::endl;
  return {};  // Notification: no response.
}

// ============================================================================
// handlePing
// ============================================================================

std::string McpServer::handlePing(const JsonRpcRequest& request) {
  return JsonRpcSerializer::serializeResponse(request.id,
                                               JsonValue::MakeObject());
}

// ============================================================================
// handleToolsList
// ============================================================================

std::string McpServer::handleToolsList(const JsonRpcRequest& request) {
  const auto tools = impl_->registry->listTools();

  auto tools_array = JsonValue::MakeArray();
  for (const auto& desc : tools) {
    tools_array.push(buildToolSchema(desc));
  }

  auto result = JsonValue::MakeObject();
  result.set("tools", std::move(tools_array));

  return JsonRpcSerializer::serializeResponse(request.id, std::move(result));
}

// ============================================================================
// handleToolsCall
// ============================================================================

std::string McpServer::handleToolsCall(const JsonRpcRequest& request) {
  if (!request.params.isObject() || !request.params.hasMember("name")) {
    return JsonRpcSerializer::serializeError(
        request.id, ErrorCode::kInvalidParams,
        "params must include 'name'");
  }

  const std::string tool_name = request.params["name"].toString();
  const ToolDescriptor* tool = impl_->registry->findTool(tool_name);
  if (tool == nullptr) {
    return JsonRpcSerializer::serializeError(
        request.id, ErrorCode::kMethodNotFound,
        "Tool not found: " + tool_name);
  }

  JsonValue arguments;
  if (request.params.hasMember("arguments") &&
      request.params["arguments"].isObject()) {
    arguments = request.params["arguments"];
  } else {
    arguments = JsonValue::MakeObject();
  }

  // Convert JSON arguments ¡ú Protobuf.
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

  // Convert Protobuf response ¡ú JSON.
  auto json_result =
      protobufToJson(rpc_result.value().body, tool->response_descriptor);
  if (!json_result.ok()) {
    return JsonRpcSerializer::serializeError(
        request.id, ErrorCode::kInternalError,
        "Failed to convert response: " + json_result.status().message());
  }

  // Build MCP result: content[0].text + structuredContent.
  auto content = JsonValue::MakeArray();
  auto text_item = JsonValue::MakeObject();
  text_item.set("type", JsonValue("text"));
  text_item.set("text", JsonValue(json_result.value().serialize()));
  content.push(std::move(text_item));

  auto result = JsonValue::MakeObject();
  result.set("content", std::move(content));
  result.set("structuredContent", std::move(json_result).value());

  return JsonRpcSerializer::serializeResponse(request.id, std::move(result));
}

// ============================================================================
// buildToolSchema / buildPropertySchema
// ============================================================================

JsonValue McpServer::buildToolSchema(const ToolDescriptor& desc) {
  auto tool = JsonValue::MakeObject();
  tool.set("name", JsonValue(desc.name));
  tool.set("description", JsonValue(desc.description));

  auto schema = JsonValue::MakeObject();
  schema.set("type", JsonValue("object"));

  auto properties = JsonValue::MakeObject();
  auto required_arr = JsonValue::MakeArray();
  for (const auto& param : desc.input_schema) {
    properties.set(param.name, buildPropertySchema(param));
    if (param.required) {
      required_arr.push(JsonValue(param.name));
    }
  }
  schema.set("properties", std::move(properties));
  if (required_arr.size() > 0) {
    schema.set("required", std::move(required_arr));
  }

  tool.set("inputSchema", std::move(schema));
  return tool;
}

JsonValue McpServer::buildPropertySchema(const ToolParameter& param) {
  auto prop = JsonValue::MakeObject();
  prop.set("type", JsonValue(param.type));
  if (!param.description.empty())
    prop.set("description", JsonValue(param.description));
  if (!param.content_encoding.empty())
    prop.set("contentEncoding", JsonValue(param.content_encoding));

  if (param.enum_values.has_value()) {
    auto arr = JsonValue::MakeArray();
    for (const auto& v : *param.enum_values) arr.push(JsonValue(v));
    prop.set("enum", std::move(arr));
  }

  if (!param.properties.empty()) {
    auto nested_props = JsonValue::MakeObject();
    auto nested_req = JsonValue::MakeArray();
    for (const auto& p : param.properties) {
      nested_props.set(p.name, buildPropertySchema(p));
      if (p.required) nested_req.push(JsonValue(p.name));
    }
    prop.set("properties", std::move(nested_props));
    if (nested_req.size() > 0) prop.set("required", std::move(nested_req));
  }

  if (param.items != nullptr) {
    prop.set("items", buildPropertySchema(*param.items));
  }

  return prop;
}

// ============================================================================
// JSON ? Protobuf
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

  const std::string json_text = json.serialize();
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

  // Parse the JSON text back into our JsonValue DOM.
  // We embed it inside a wrapper object to leverage the existing parser.
  std::string wrapper = R"({"jsonrpc":"2.0","method":"_","id":1,"params":)" +
                        json_text + "}";
  const auto parsed = impl_->parser.parseRequest(wrapper);
  if (!parsed.has_value() || !parsed->params.isObject()) {
    return rpc::Status(rpc::StatusCode::kInternal,
                       "failed to parse JSON response from Protobuf");
  }
  return parsed->params;
}

}  // namespace nexus::mcp

