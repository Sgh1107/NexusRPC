# Logging Guide

NexusRPC uses the vendored `spdlog` 1.17.0 source under
`include/spdlog-1.17.0`. CMake builds it as an excluded third-party target and
links it through `nexus_observability`.

## Default Behavior

Call `nexus::observability::Logging::initialize()` before starting worker or
network threads. The default asynchronous logger writes structured text to
stderr, flushes on warnings and errors, and includes timestamp, level, logger
name, and thread ID. The async queue has 8192 entries and one consumer thread;
logging blocks briefly rather than silently dropping records when it is full.
`Logging::shutdown()` flushes and releases the logger during orderly process
shutdown.

MCP stdio has a strict output boundary: JSON-RPC responses use stdout only;
logs use stderr and never use stdout.

## File Rotation

Provide a file path to add a rotating file sink alongside stderr. The default
rotation limit is 10 MiB per file and five retained files.

```cpp logging_example.cpp
nexus::observability::LoggingOptions options;
options.logger_name = "weather";
options.level = spdlog::level::debug;
options.file_path = "logs/weather.log";
nexus::observability::Logging::initialize(options);
```

The parent directory must exist. File creation errors are reported by spdlog.

## Project Macros

Use `NEXUS_LOG_TRACE`, `NEXUS_LOG_DEBUG`, `NEXUS_LOG_INFO`,
`NEXUS_LOG_WARN`, and `NEXUS_LOG_ERROR` from
`nexus/observability/logging.h`. Do not log raw Protobuf bodies, credentials,
or complete RPC metadata. Prefer request ID, service, method, status code,
and an error summary for RPC diagnostics.
