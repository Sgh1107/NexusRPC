/// @file service_integration_test.cpp
/// @brief End-to-end TCP integration tests for Weather and Echo services
///        (TASK-015).
///
/// Each test starts a real RpcServer on a loopback port, connects with an
/// RpcClient, issues a typed Protobuf call, and verifies the response.

#include <cstdint>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gtest/gtest.h"

#include "examples/echo.pb.h"
#include "examples/weather.pb.h"
#include "nexus/rpc/rpc_client.h"
#include "nexus/rpc/rpc_server.h"

namespace nexus::rpc {
namespace {

// ============================================================================
// Helpers
// ============================================================================

/// Binds to an ephemeral port on loopback and returns the port number.
/// The socket is closed immediately; the caller can reuse the port.
std::uint16_t allocateLoopbackPort() {
  const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  EXPECT_GE(fd, 0);

  struct sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  EXPECT_EQ(::bind(fd, reinterpret_cast<const struct sockaddr*>(&address),
                   sizeof(address)),
            0);

  socklen_t address_length = sizeof(address);
  EXPECT_EQ(
      ::getsockname(fd, reinterpret_cast<struct sockaddr*>(&address),
                    &address_length),
      0);
  const std::uint16_t port = ntohs(address.sin_port);
  EXPECT_EQ(::close(fd), 0);
  return port;
}

// ============================================================================
// Weather service integration tests
// ============================================================================

TEST(WeatherServiceIntegrationTest, GetCurrentBeijing) {
  const std::uint16_t port = allocateLoopbackPort();
  RpcServer server(1);

  using nexus::examples::weather::GetCurrentRequest;
  using nexus::examples::weather::GetCurrentResponse;

  ASSERT_TRUE(
      (server
           .registerService<GetCurrentRequest, GetCurrentResponse>(
               "weather", "GetCurrent",
               [](const GetCurrentRequest& request,
                  GetCurrentResponse* response) {
                 response->set_city(request.city());
                 response->set_temperature_celsius(28.5);
                 response->set_humidity_percent(55.0);
                 response->set_condition("Sunny");
                 return Status::Ok();
               })
           .ok()));
  ASSERT_TRUE(server.start(port).ok());

  RpcClient client({"127.0.0.1", port});
  GetCurrentRequest request;
  request.set_city("Beijing");
  Result<GetCurrentResponse> result =
      client.call<GetCurrentRequest, GetCurrentResponse>(
          "weather", "GetCurrent", request);

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().city(), "Beijing");
  EXPECT_DOUBLE_EQ(result.value().temperature_celsius(), 28.5);
  EXPECT_DOUBLE_EQ(result.value().humidity_percent(), 55.0);
  EXPECT_EQ(result.value().condition(), "Sunny");

  server.stop();
}

TEST(WeatherServiceIntegrationTest, GetCurrentUnknownCity) {
  const std::uint16_t port = allocateLoopbackPort();
  RpcServer server(1);

  using nexus::examples::weather::GetCurrentRequest;
  using nexus::examples::weather::GetCurrentResponse;

  ASSERT_TRUE(
      (server
           .registerService<GetCurrentRequest, GetCurrentResponse>(
               "weather", "GetCurrent",
               [](const GetCurrentRequest& request,
                  GetCurrentResponse* response) {
                 response->set_city(request.city());
                 response->set_temperature_celsius(20.0);
                 response->set_humidity_percent(60.0);
                 response->set_condition("Unknown");
                 return Status::Ok();
               })
           .ok()));
  ASSERT_TRUE(server.start(port).ok());

  RpcClient client({"127.0.0.1", port});
  GetCurrentRequest request;
  request.set_city("Atlantis");
  Result<GetCurrentResponse> result =
      client.call<GetCurrentRequest, GetCurrentResponse>(
          "weather", "GetCurrent", request);

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().city(), "Atlantis");
  EXPECT_EQ(result.value().condition(), "Unknown");

  server.stop();
}

// ============================================================================
// Echo service integration tests
// ============================================================================

TEST(EchoServiceIntegrationTest, EchoRoundTrip) {
  const std::uint16_t port = allocateLoopbackPort();
  RpcServer server(1);

  using nexus::examples::echo::EchoRequest;
  using nexus::examples::echo::EchoResponse;

  ASSERT_TRUE(
      (server
           .registerService<EchoRequest, EchoResponse>(
               "echo", "Echo",
               [](const EchoRequest& request, EchoResponse* response) {
                 response->set_message(request.message());
                 return Status::Ok();
               })
           .ok()));
  ASSERT_TRUE(server.start(port).ok());

  RpcClient client({"127.0.0.1", port});
  EchoRequest request;
  request.set_message("Hello, NexusRPC!");
  Result<EchoResponse> result =
      client.call<EchoRequest, EchoResponse>("echo", "Echo", request);

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().message(), "Hello, NexusRPC!");

  server.stop();
}

TEST(EchoServiceIntegrationTest, EchoEmptyMessage) {
  const std::uint16_t port = allocateLoopbackPort();
  RpcServer server(1);

  using nexus::examples::echo::EchoRequest;
  using nexus::examples::echo::EchoResponse;

  ASSERT_TRUE(
      (server
           .registerService<EchoRequest, EchoResponse>(
               "echo", "Echo",
               [](const EchoRequest& request, EchoResponse* response) {
                 response->set_message(request.message());
                 return Status::Ok();
               })
           .ok()));
  ASSERT_TRUE(server.start(port).ok());

  RpcClient client({"127.0.0.1", port});
  EchoRequest request;
  // message left empty intentionally.
  Result<EchoResponse> result =
      client.call<EchoRequest, EchoResponse>("echo", "Echo", request);

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_TRUE(result.value().message().empty());

  server.stop();
}

}  // namespace
}  // namespace nexus::rpc
