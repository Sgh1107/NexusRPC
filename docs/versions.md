# Minimum Development Environment Versions

> Status: Phase 0 baseline
> Scope: minimum supported development and CI versions, not installation instructions

## 1. Policy

The versions in this document are compatibility floors. A newer patch or minor version may be used unless a later compatibility test records a regression. Exact CI images and dependency lock revisions are implementation details to be added with the CI task.

Phase 0 records requirements only. It does not install, download, or validate any tool or library.

## 2. Required build environment

| Component | Minimum | Primary CI baseline | Notes |
|---|---:|---:|---|
| OS | Ubuntu 22.04 LTS | Ubuntu 22.04 LTS | Primary supported environment |
| Compatibility OS | Ubuntu 24.04 LTS | Manual or scheduled job | Compatibility verification only |
| GCC | 11 | GCC 11 | Required compiler |
| Clang | 14 | Clang 14+ | Best-effort compiler validation |
| CMake | 3.15 | Latest available in Ubuntu 22.04 policy | Required build system floor |
| Ninja | 1.10 | 1.10+ | Recommended generator |
| Make | 4.3 | 4.3+ | Supported alternative generator |
| Git | 2.34 | Distribution version or newer | Source control and CI checkout |
| Bash | 5.1 | Distribution version | Scripts target Linux Bash |

## 3. Required development tools

| Tool | Minimum | Phase |
|---|---:|---|
| `protoc` | 3.x, project baseline to be pinned before Phase 1 | v1.0 |
| `protoc-gen-cpp` | Same release family as `protoc` | v1.0 |
| `clang-format` | 14 | v1.0 |
| `cppcheck` | 2.7 | v1.0 |
| `clang-tidy` | 14 | v1.1 CI |
| AddressSanitizer | GCC 11 or Clang 14 runtime | v1.0 CI |
| ThreadSanitizer | GCC 11 or Clang 14 runtime | v1.1 CI |
| UBSan | GCC 11 or Clang 14 runtime | v1.1 CI |
| Valgrind | 3.18 | v1.2 / long stability |
| Docker Compose | Compose v2 | v1.1 integration tests |

`protoc` is deliberately recorded as a 3.x floor in Phase 0. Before the first `.proto` implementation, the project must pin and test one concrete version family in CI.

## 4. Runtime and library floors

| Library or runtime | Minimum | Usage | Phase |
|---|---:|---|---|
| Protobuf C++ | 3.x | RPC body and generated descriptors | v1.0 |
| nlohmann/json | 3.10 | MCP JSON-RPC and debug JSON | v1.0 |
| GoogleTest | 1.11 | Unit and integration test framework | v1.0 |
| Google Benchmark | 1.6 | RPC and serializer benchmarks | v1.1 |
| hiredis | 1.0 | Redis synchronous client | v1.1 |
| OpenSSL | 3.0 | Optional HTTP/auth utility integration | v1.1 |
| Redis Server | 6.0 | Registry integration tests | v1.1 |
| TOML parser | To be selected before config implementation | Runtime configuration | v1.0 |

The TOML parser and concrete Protobuf version remain explicit pre-implementation decisions because the development decisions document selected the format but did not select a parser or exact package release.

## 5. Platform boundaries

Linux x86_64 is the v1 runtime platform. The network, RPC Server, and networked Gateway depend on epoll and are not required to run on macOS. macOS may build pure modules such as JSON-RPC, schema conversion, configuration, and status handling when their dependencies are available.

Windows is out of scope for v1.

## 6. Version recording rules

When a dependency is introduced, record the concrete version in this document and in the CI installation or lock configuration. Record the reason for any version increase and run the relevant unit, integration, sanitizer, and compatibility tests before changing the minimum.
