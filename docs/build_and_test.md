# Build and Test Guide

This guide covers the current NexusRPC build and unit-test workflow on a Linux
build machine. The network implementation uses Linux socket and epoll APIs.

## Prerequisites

Install or provide the following dependencies before configuring CMake:

- CMake 3.15 or newer
- A C++17 compiler
- Protobuf development files, including `protoc`, headers, and
  `protobuf::libprotobuf`
- GTest development files discoverable by `find_package(GTest REQUIRED)`

## Debug Build and Unit Tests

Run these commands from the repository root:

```bash build-and-test.sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug \
  -DNEXUSRPC_BUILD_TESTS=ON \
  -DNEXUSRPC_BUILD_EXAMPLES=OFF

cmake --build build/debug --parallel

ctest --test-dir build/debug --output-on-failure
```

A successful configuration confirms that CMake can locate Protobuf and GTest.
The full test run covers the network unit tests, frame codec tests, and the RPC
SDK loopback tests, plus the Phase 2 MCP unit tests (JSON-RPC, Tool Registry,
stdio Gateway).

## RPC-Focused Tests

Run only the frame codec and RPC SDK test suites after the Debug build:

```bash rpc-tests.sh
ctest --test-dir build/debug --output-on-failure \
  -R 'FrameCodecTest|RpcSdkTest'
```

If CTest does not discover the dynamically registered GTest cases, build and
run the RPC SDK executable directly:

```bash rpc-sdk-test.sh
cmake --build build/debug --target rpc_sdk_test --parallel
./build/debug/tests/unit/rpc_sdk_test
```

The SDK coverage currently includes:

- Raw Unary request and response loopback.
- Typed Protobuf Unary loopback using `google::protobuf::StringValue`.
- Unknown service and method mapping to `NOT_FOUND`.
- Duplicate service and method registration rejection.

## MCP-Focused Tests

Run the Phase 2 MCP suites after the Debug build:

```bash mcp-tests.sh
ctest --test-dir build/debug --output-on-failure \
  -R 'JsonRpc|ToolRegistry|McpServer'
```

Or build and run each executable directly:

```bash mcp-test-binaries.sh
cmake --build build/debug --target json_rpc_test tool_registry_test \
  mcp_server_test --parallel
./build/debug/tests/unit/json_rpc_test
./build/debug/tests/unit/tool_registry_test
./build/debug/tests/unit/mcp_server_test
```

The MCP coverage includes:

- JSON-RPC parse failures, batch arrays, notifications and error serialization.
- Descriptor-driven Tool Registry (`weather.get_current`, `echo.echo`),
  custom option descriptions / required flags, json_name conversion, and
  base / enum / nested / repeated / map / optional / Timestamp / Duration /
  wrapper schema mapping.
- stdio Gateway lifecycle (`initialize` / `ping` / `tools/list` /
  `tools/call`), session enforcement, a real TCP tools/call round trip, and
  stdout/stderr isolation.

## MCP stdio Gateway Example

Build the example binaries and run the local loop:

```bash mcp-gateway-example.sh
cmake --build build/debug --target weather_server mcp_gateway --parallel

./build/debug/examples/weather_server &
GATEWAY_PID=$!

printf '%s\n' \
  '{"jsonrpc":"2.0","method":"initialize","id":1,"params":{"protocolVersion":"2024-11-05"}}' \
  '{"jsonrpc":"2.0","method":"tools/list","id":2}' \
  '{"jsonrpc":"2.0","method":"tools/call","id":3,"params":{"name":"weather.get_current","arguments":{"city":"Beijing"}}}' \
  | ./build/debug/examples/mcp_gateway

kill $GATEWAY_PID
```

`tools/list` must expose `weather.get_current`; `tools/call` must return both
`content[0].text` and `structuredContent`. The same binary can be attached to
MCP Inspector by launching it as the stdio command.

## AddressSanitizer Build

Use this configuration to check the network and worker-thread lifecycle paths
for memory and undefined-behavior faults:

```bash asan-tests.sh
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug \
  -DNEXUSRPC_BUILD_TESTS=ON \
  -DNEXUSRPC_BUILD_EXAMPLES=OFF \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'

cmake --build build/asan --parallel
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build/asan --output-on-failure
```

## Current Status

These commands have not been run from the current development environment.
Run them on the designated Linux build machine and retain the CMake, build, and
CTest output with the task validation records.

Phase 1 (TASK-010 through TASK-015) is complete with 72/72 tests passing.
Phase 2 (TASK-020 through TASK-022) is code-complete with unit tests written
but not yet executed on the build machine.
