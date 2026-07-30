/// @file logging.h
/// @brief Process-wide spdlog configuration for NexusRPC components.

#ifndef NEXUS_OBSERVABILITY_LOGGING_H_
#define NEXUS_OBSERVABILITY_LOGGING_H_

#include <cstddef>
#include <memory>
#include <string>

#include <spdlog/common.h>
#include <spdlog/logger.h>

namespace nexus::observability {

struct LoggingOptions {
  std::string logger_name = "nexus";
  spdlog::level::level_enum level = spdlog::level::info;
  std::string file_path;
  std::size_t max_file_size = 10 * 1024 * 1024;
  std::size_t max_file_count = 5;
  bool color_stderr = true;
};

class Logging {
 public:
  /** Initializes the process logger. Safe to call once before worker startup. */
  static void initialize(const LoggingOptions& options = LoggingOptions());

  /** Returns the configured logger, initializing a stderr logger if required. */
  static std::shared_ptr<spdlog::logger> logger();

  /** Updates the level of the process logger. */
  static void setLevel(spdlog::level::level_enum level);

  /** Flushes and releases logger resources during process shutdown. */
  static void shutdown();
};

}  // namespace nexus::observability

#define NEXUS_LOG_TRACE(...) ::nexus::observability::Logging::logger()->trace(__VA_ARGS__)
#define NEXUS_LOG_DEBUG(...) ::nexus::observability::Logging::logger()->debug(__VA_ARGS__)
#define NEXUS_LOG_INFO(...) ::nexus::observability::Logging::logger()->info(__VA_ARGS__)
#define NEXUS_LOG_WARN(...) ::nexus::observability::Logging::logger()->warn(__VA_ARGS__)
#define NEXUS_LOG_ERROR(...) ::nexus::observability::Logging::logger()->error(__VA_ARGS__)

#endif  // NEXUS_OBSERVABILITY_LOGGING_H_
