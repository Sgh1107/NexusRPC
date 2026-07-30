/// @file logging.cpp
/// @brief spdlog-backed process logger implementation.

#include "nexus/observability/logging.h"

#include <mutex>
#include <utility>
#include <vector>

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace nexus::observability {
namespace {

std::mutex& loggingMutex() {
  static std::mutex mutex;
  return mutex;
}

std::shared_ptr<spdlog::logger>& processLogger() {
  static std::shared_ptr<spdlog::logger> logger;
  return logger;
}

std::shared_ptr<spdlog::logger> makeLogger(const LoggingOptions& options) {
  std::vector<spdlog::sink_ptr> sinks;
  if (options.color_stderr) {
    sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
  } else {
    sinks.push_back(std::make_shared<spdlog::sinks::stderr_sink_mt>());
  }
  if (!options.file_path.empty()) {
    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        options.file_path, options.max_file_size, options.max_file_count));
  }

  auto logger = std::make_shared<spdlog::async_logger>(
      options.logger_name, sinks.begin(), sinks.end(), spdlog::thread_pool(),
      spdlog::async_overflow_policy::block);
  logger->set_level(options.level);
  logger->set_pattern("%Y-%m-%dT%H:%M:%S.%e%z [%^%l%$] [%n] [t%t] %v");
  logger->flush_on(spdlog::level::warn);
  return logger;
}

}  // namespace

void Logging::initialize(const LoggingOptions& options) {
  std::lock_guard<std::mutex> lock(loggingMutex());
  if (processLogger()) {
    return;
  }
  spdlog::init_thread_pool(8192, 1);
  processLogger() = makeLogger(options);
  spdlog::register_logger(processLogger());
}

std::shared_ptr<spdlog::logger> Logging::logger() {
  std::lock_guard<std::mutex> lock(loggingMutex());
  if (!processLogger()) {
    spdlog::init_thread_pool(8192, 1);
    processLogger() = makeLogger(LoggingOptions());
    spdlog::register_logger(processLogger());
  }
  return processLogger();
}

void Logging::setLevel(spdlog::level::level_enum level) {
  logger()->set_level(level);
}

void Logging::shutdown() {
  std::lock_guard<std::mutex> lock(loggingMutex());
  if (processLogger()) {
    processLogger()->flush();
    spdlog::drop(processLogger()->name());
    processLogger().reset();
  }
  spdlog::shutdown();
}

}  // namespace nexus::observability
