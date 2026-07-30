/// @file logging_test.cpp
/// @brief Unit tests for the spdlog-backed logging facade.

#include <gtest/gtest.h>

#include "nexus/observability/logging.h"

namespace nexus::observability {
namespace {

TEST(LoggingTest, InitializesProcessLoggerWithRequestedLevel) {
  LoggingOptions options;
  options.logger_name = "nexus_test";
  options.level = spdlog::level::debug;
  options.color_stderr = false;

  Logging::initialize(options);
  const auto logger = Logging::logger();

  ASSERT_NE(logger, nullptr);
  EXPECT_EQ(logger->level(), spdlog::level::debug);
  logger->debug("logging facade test");
  Logging::shutdown();
}

}  // namespace
}  // namespace nexus::observability
