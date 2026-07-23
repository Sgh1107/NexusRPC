/// @file rpc_sdk_test.cpp
/// @brief Unit and loopback integration coverage for TASK-013 and TASK-014.

#include <cstdint>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <google/protobuf/wrappers.pb.h>

#include "gtest/gtest.h"

#include "nexus/rpc/rpc_client.h"
#include "nexus/rpc/rpc_server.h"

namespace nexus::rpc {
namespace {

/** Reserves an ephemeral loopback port long enough to obtain its number. */
std::uint16_t allocateLoopbackPort() {
  const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  EXPECT_GE(fd, 0);

  struct sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  EXPECT_EQ(::bind(fd, reinterpret_cast<const struct sockaddr*>(&address), sizeof(address)), 0);

  socklen_t address_length = sizeof(address);
  EXPECT_EQ(::getsockname(fd, reinterpret_cast<struct sockaddr*>(&address), &address_length), 0);
  const std::uint16_t port = ntohs(address.sin_port);
  EXPECT_EQ(::close(fd), 0);
  return port;
}

TEST(RpcSdkTest, UnaryCallRoundTrip) {
  const std::uint16_t port = allocateLoopbackPort();
  RpcServer server(1);
  ASSERT_TRUE(server.registerService(
      "echo", "reply", [](const RpcRequest& request) {
        RpcResponse response;
        response.body = request.body;
        response.metadata["trace_id"] = request.metadata.at("trace_id");
        return response;
      }).ok());
  ASSERT_TRUE(server.start(port).ok());

  RpcClient client({"127.0.0.1", port});
  RpcCallOptions options;
  options.metadata["trace_id"] = "trace-42";
  Result<RpcResponse> result = client.call("echo", "reply", "payload", options);

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().body, "payload");
  EXPECT_EQ(result.value().metadata.at("trace_id"), "trace-42");
  server.stop();
}

TEST(RpcSdkTest, ProtobufUnaryCallRoundTrip) {
  const std::uint16_t port = allocateLoopbackPort();
  RpcServer server(1);
  ASSERT_TRUE((server.registerService<google::protobuf::StringValue,
                                      google::protobuf::StringValue>(
      "echo", "protobuf_reply",
      [](const google::protobuf::StringValue& request,
         google::protobuf::StringValue* response) {
        response->set_value(request.value());
        return Status::Ok();
      })).ok());
  ASSERT_TRUE(server.start(port).ok());

  RpcClient client({"127.0.0.1", port});
  google::protobuf::StringValue request;
  request.set_value("protobuf payload");
  Result<google::protobuf::StringValue> result =
      client.call<google::protobuf::StringValue, google::protobuf::StringValue>(
          "echo", "protobuf_reply", request);

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().value(), "protobuf payload");
  server.stop();
}

TEST(RpcSdkTest, UnknownMethodMapsToNotFound) {
  const std::uint16_t port = allocateLoopbackPort();
  RpcServer server(1);
  ASSERT_TRUE(server.start(port).ok());

  RpcClient client({"127.0.0.1", port});
  Result<RpcResponse> result = client.call("missing", "method", "");

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kNotFound);
  server.stop();
}

TEST(RpcSdkTest, RegistrationRejectsDuplicateServiceMethod) {
  RpcServer server;
  const RpcHandler handler = [](const RpcRequest&) { return RpcResponse{}; };

  ASSERT_TRUE(server.registerService("echo", "reply", handler).ok());
  Status duplicate = server.registerService("echo", "reply", handler);
  EXPECT_EQ(duplicate.code(), StatusCode::kInternal);
}

}  // namespace
}  // namespace nexus::rpc
