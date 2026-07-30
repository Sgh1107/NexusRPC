/// @file weather_service/main.cpp
/// @brief Weather RPC server listening on port 9601 (TASK-015).
///
/// Registers the "weather.GetCurrent" method backed by a simple hard-coded
/// weather database. Uses the NexusRPC RpcServer SDK with Protobuf message
/// types generated from proto/examples/weather.proto.

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>

#include "examples/weather.pb.h"
#include "nexus/observability/logging.h"
#include "nexus/rpc/rpc_server.h"
#include "nexus/rpc/status.h"


namespace {

/// Graceful-shutdown flag set by SIGINT / SIGTERM.
std::atomic<bool> g_running{true};

/// Installs signal handlers that set g_running to false.
void installSignalHandlers() {
  const auto handler = [](int /*signal*/) { g_running = false; };
  std::signal(SIGINT, handler);
  std::signal(SIGTERM, handler);
}

}  // namespace

int main() {
  nexus::observability::Logging::initialize();
  installSignalHandlers();

  nexus::rpc::RpcServer server(/*worker_thread_count=*/4);


  // -----------------------------------------------------------------------
  // Register typed handlers (Protobuf request / response)
  // -----------------------------------------------------------------------
  using nexus::examples::weather::GetCurrentRequest;
  using nexus::examples::weather::GetCurrentResponse;

  const auto register_status = server.registerService<GetCurrentRequest, GetCurrentResponse>(
      "weather", "GetCurrent",
      [](const GetCurrentRequest& request, GetCurrentResponse* response) {
        if (request.city().empty()) {
          return nexus::rpc::Status(nexus::rpc::StatusCode::kInvalidArgument,
                                    "city must be non-empty");
        }

        // Hard-coded weather database for demonstration purposes.
        response->set_city(request.city());
        if (request.city() == "Beijing") {
          response->set_temperature_celsius(28.5);
          response->set_humidity_percent(55.0);
          response->set_condition("Sunny");
        } else if (request.city() == "London") {
          response->set_temperature_celsius(15.2);
          response->set_humidity_percent(78.0);
          response->set_condition("Cloudy");
        } else if (request.city() == "Tokyo") {
          response->set_temperature_celsius(22.0);
          response->set_humidity_percent(68.0);
          response->set_condition("Rain");
        } else {
          response->set_temperature_celsius(20.0);
          response->set_humidity_percent(60.0);
          response->set_condition("Unknown");
        }
        return nexus::rpc::Status::Ok();
      });

      if (!register_status.ok()) {
    NEXUS_LOG_ERROR("weather handler registration failed: {}",
                    register_status.message());
    nexus::observability::Logging::shutdown();
    return EXIT_FAILURE;
  }


  // -----------------------------------------------------------------------
  // Start listening
  // -----------------------------------------------------------------------
  constexpr std::uint16_t kWeatherPort = 9601;
  const auto start_status = server.start(kWeatherPort);
      if (!start_status.ok()) {
    NEXUS_LOG_ERROR("weather server startup failed port={} message={}",
                    kWeatherPort, start_status.message());
    nexus::observability::Logging::shutdown();
    return EXIT_FAILURE;
  }

  NEXUS_LOG_INFO("weather service listening address=0.0.0.0 port={}",
                 kWeatherPort);



  // -----------------------------------------------------------------------
  // Wait for shutdown signal
  // -----------------------------------------------------------------------
  while (g_running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

    NEXUS_LOG_INFO("weather service stopping");

  server.stop();
  NEXUS_LOG_INFO("weather service stopped");
  nexus::observability::Logging::shutdown();

  return EXIT_SUCCESS;
}
