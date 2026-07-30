# NexusRPC Build and Test Guide

This document is the operational manual for building and testing the current
NexusRPC repository on a Linux build machine. Acceptance scope is defined in
`docs/testing.md`; this document defines reproducible commands and artifacts.

## 1. Environment and Dependencies

The primary environment is Ubuntu 22.04 LTS; Ubuntu 24.04 is a compatibility
target. Network, RPC, and end-to-end tests require Linux because the network
implementation uses epoll and POSIX sockets.

Required tools:

- CMake 3.15 or newer
- GCC 11+ or Clang 14+ with C++17 support
- Ninja (recommended) or GNU Make
- Protobuf compiler and C++ development package
- GoogleTest development package, discoverable by `find_package(GTest)`
- cppcheck for the static-analysis step

`spdlog` 1.17.0 is already vendored at `include/spdlog-1.17.0`; do not install
or fetch a second copy for this project.

On Ubuntu, install the standard dependencies:

```bash install-dependencies.sh
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build pkg-config \
  protobuf-compiler libprotobuf-dev libgtest-dev cppcheck

cmake --version
g++ --version
protoc --version
```

If CMake cannot locate GTest after installing `libgtest-dev`, install the
platform's GoogleTest CMake package or pass its prefix via `CMAKE_PREFIX_PATH`.
Do not copy GTest sources into the repository.

## 2. Debug Configuration and Build

Run commands from the repository root. Build configurations must use separate
folders; do not reuse the ASan directory for normal Debug builds.

```bash configure-debug.sh
cmake -S . -B build/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DNEXUSRPC_BUILD_EXAMPLES=ON \
  -DNEXUSRPC_BUILD_TESTS=ON \
  -DNEXUSRPC_BUILD_BENCHMARKS=OFF

cmake --build build/debug --parallel
```

CMake generates Protobuf C++ files below `build/debug/proto`. Those files are
build artifacts and must not be edited.

To speed up an edit/test loop, build one target only:

```bash build-one-target.sh
cmake --build build/debug --target logging_test --parallel
cmake --build build/debug --target rpc_sdk_test --parallel
cmake --build build/debug --target json_rpc_test --parallel
```

## 3. Run Unit Tests

CTest is the canonical runner. It discovers GoogleTest cases during the build.

```bash test-all.sh
ctest --test-dir build/debug --output-on-failure --parallel 4
```

List available cases before selecting a regex:

```bash list-tests.sh
ctest --test-dir build/debug -N
```

Run focused test groups:

```bash test-groups.sh
ctest --test-dir build/debug --output-on-failure -R 'Buffer|Socket|Channel|Tcp'
ctest --test-dir build/debug --output-on-failure -R 'FrameCodec|Rpc'
ctest --test-dir build/debug --output-on-failure -R 'Json|Tool'
ctest --test-dir build/debug --output-on-failure -R '^LoggingTest\.'
```

Run an executable directly when using an exact GoogleTest filter or a debugger:

```bash run-directly.sh
./build/debug/tests/unit/logging_test
./build/debug/tests/unit/rpc_sdk_test --gtest_filter='RpcSdkTest.*'
./build/debug/tests/unit/json_rpc_test --gtest_filter='JsonRpcParserTest.*'
```

Use `--gtest_break_on_failure` under gdb or lldb.

## 4. Failure Diagnostics and Logs

Capture serial CTest output for an issue or test report:

```bash capture-tests.sh
mkdir -p artifacts
ctest --test-dir build/debug --output-on-failure --parallel 1 \
  2>&1 | tee artifacts/ctest-debug.log
```

For a failing test, rebuild its target, then run only the failed case. Record
the CMake output, compiler and Protobuf versions, exact command, test log, and
complete sanitizer report. Do not attach raw request/response bodies,
credentials, or full metadata that may be sensitive.

NexusRPC logs use asynchronous `spdlog`. They go to stderr by default; WARN and
ERROR entries flush immediately. MCP JSON-RPC protocol output always stays on
stdout. When testing an MCP executable, capture the two streams separately:

```bash capture-mcp.sh
./your_mcp_gateway >artifacts/mcp.stdout.jsonl \
  2>artifacts/mcp.stderr.log
```

`mcp.stdout.jsonl` must contain protocol JSON only. File logging configuration,
rotation behavior, and sensitive-data rules are in `docs/logging.md`.

## 5. Example Service Smoke Test

The current examples define `weather_server` on TCP port `9601` and
`echo_server` on TCP port `9602`.

```bash run-examples.sh
mkdir -p artifacts
./build/debug/examples/weather_server 2>&1 | tee artifacts/weather.log
./build/debug/examples/echo_server 2>&1 | tee artifacts/echo.log
```

Run the services in separate terminals. Stop them gracefully after confirming
startup output:

```bash stop-examples.sh
pkill -INT -f 'weather_server|echo_server'
```

No `mcp_gateway` executable target exists currently. `McpServer` is a library;
MCP behavior is validated through JSON-RPC and Tool Registry tests until a
separate gateway application is added.

## 6. AddressSanitizer Build

Use a clean build directory for ASan. Run tests serially so the failing case
and sanitizer report remain easy to associate.

```bash asan-tests.sh
cmake -S . -B build/asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DNEXUSRPC_BUILD_EXAMPLES=ON \
  -DNEXUSRPC_BUILD_TESTS=ON \
  -DNEXUSRPC_BUILD_BENCHMARKS=OFF \
  -DCMAKE_CXX_FLAGS='-fsanitize=address -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address'

cmake --build build/asan --parallel
ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1' \
  ctest --test-dir build/asan --output-on-failure --parallel 1
```

Treat every ASan report as a failure even if functional assertions pass.

## 7. Static Analysis and Cleanup

After a successful Debug configuration, run cppcheck against CMake's compile
database:

```bash static-analysis.sh
cppcheck --project=build/debug/compile_commands.json \
  --enable=warning,style,performance,portability \
  --error-exitcode=1 \
  --suppress=missingIncludeSystem
```

Clean only generated build artifacts when a configuration must be recreated:

```bash clean-builds.sh
rm -rf build/debug build/asan
```

Do not delete source files, vendored dependencies, test artifacts needed for a
failure report, or another engineer's build directory.

## 8. Test Report Checklist

A test report should include:

- commit SHA and clean/dirty worktree state;
- operating system, compiler, CMake, Protobuf, and GTest versions;
- configuration command and build command;
- CTest command, total count, passed count, and failed test names;
- ASan and cppcheck result;
- links or paths to retained logs and sanitizer reports;
- known skips, blockers, and environmental deviations.
