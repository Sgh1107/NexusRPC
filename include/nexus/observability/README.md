# Observability Headers

`logging.h` provides the process-wide `spdlog` facade used by NexusRPC.
`Logging::initialize()` configures a stderr sink and can optionally add a
size-rotated file sink through `LoggingOptions::file_path`. MCP protocol output
always remains on stdout; logging uses stderr and the optional log file.
