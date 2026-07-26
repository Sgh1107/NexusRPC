/// @file echo_service/main.cpp
/// @brief Echo RPC server listening on port 9602 (TASK-015).
///
/// Registers the "echo.Echo" method that returns the request payload
/// unchanged.  This service serves as a connectivity smoke-test and
/// benchmark target for later phases.

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "examples/echo.pb.h"
#include "nexus/rpc/rpc_server.h"
#include "nexus/rpc/status.h"

namespace {

/// Graceful-shutdown flag.
std::atomic<bool> g_running{true};

void installSignalHandlers() {
  const auto handler = [](int /*signal*/) { g_running = false; };
  std::signal(SIGINT, handler);
  std::signal(SIGTERM, handler);
}

}  // namespace

int main() {
  installSignalHandlers();

  nexus::rpc::RpcServer server(/*worker_thread_count=*/4);

  // -----------------------------------------------------------------------
  // Register the typed Echo handler
  // -----------------------------------------------------------------------
  using nexus::examples::echo::EchoRequest;
  using nexus::examples::echo::EchoResponse;

  const auto register_status = server.registerService<EchoRequest, EchoResponse>(
      "echo", "Echo",
      [](const EchoRequest& request, EchoResponse* response) {
        response->set_message(request.message());
        return nexus::rpc::Status::Ok();
      });

  if (!register_status.ok()) {
    std::cerr << "Failed to register handler: " << register_status.message() << std::endl;
    return EXIT_FAILURE;
  }

  // -----------------------------------------------------------------------
  // Start listening
  // -----------------------------------------------------------------------
  constexpr std::uint16_t kEchoPort = 9602;
  const auto start_status = server.start(kEchoPort);
  if (!start_status.ok()) {
    std::cerr << "Failed to start Echo server on port " << kEchoPort
              << ": " << start_status.message() << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "Echo service listening on 0.0.0.0:" << kEchoPort << std::endl;

  // -----------------------------------------------------------------------
  // Wait for shutdown signal
  // -----------------------------------------------------------------------
  while (g_running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "Shutting down Echo service..." << std::endl;
  server.stop();
  std::cout << "Echo service stopped." << std::endl;
  return EXIT_SUCCESS;
}
