#ifndef NEXUS_RPC_RPC_CLIENT_H_
#define NEXUS_RPC_RPC_CLIENT_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include <google/protobuf/message.h>

#include "nexus/rpc/rpc_message.h"

namespace nexus::rpc {

/** Identifies one static v1 RPC backend instance. */
struct RpcEndpoint {
  std::string host;
  std::uint16_t port = 0;
};

/** Per-call options for a unary request. */
struct RpcCallOptions {
  std::chrono::milliseconds deadline{5000};
  std::map<std::string, std::string> metadata;
};

/** Receives the final result of an asynchronous unary call. */
using RpcCallback = std::function<void(Result<RpcResponse>)>;

/**
 * Single-endpoint, single-connection unary RPC client.
 *
 * Calls are serialized by an internal mutex. A timed-out or otherwise failed
 * call closes the connection before releasing that mutex, so a delayed
 * response cannot be matched to a later request. v1 does not multiplex calls.
 */
class RpcClient {
 public:
  explicit RpcClient(RpcEndpoint endpoint);
  ~RpcClient();

  RpcClient(const RpcClient&) = delete;
  RpcClient& operator=(const RpcClient&) = delete;

  /** Performs a synchronous unary RPC call. */
  Result<RpcResponse> call(std::string_view service, std::string_view method,
                           std::string_view body,
                           const RpcCallOptions& options = RpcCallOptions());

  /**
   * Serializes a generated Protobuf request and parses a generated response.
   *
   * The transport remains binary and generic; only generated Message types are
   * accepted at this public boundary.
   */
  template <typename Request, typename Response>
  Result<Response> call(std::string_view service, std::string_view method,
                        const Request& request,
                        const RpcCallOptions& options = RpcCallOptions()) {
    static_assert(std::is_base_of_v<google::protobuf::Message, Request>,
                  "Request must be a generated Protobuf message");
    static_assert(std::is_base_of_v<google::protobuf::Message, Response>,
                  "Response must be a generated Protobuf message");

    std::string request_body;
    if (!request.SerializeToString(&request_body)) {
      return Status(StatusCode::kInvalidArgument, "failed to serialize Protobuf request");
    }

    Result<RpcResponse> wire_result = call(service, method, request_body, options);
    if (!wire_result.ok()) {
      return wire_result.status();
    }

    Response response;
    if (!response.ParseFromString(wire_result.value().body)) {
      return Status(StatusCode::kInternal, "failed to parse Protobuf response");
    }
    return response;
  }

  /** Performs a unary RPC call using a future-based completion interface. */
  std::future<Result<RpcResponse>> callFuture(std::string service, std::string method,
                                              std::string body, RpcCallOptions options);

  /** Performs a unary RPC call on a detached worker thread. */
  void callAsync(std::string service, std::string method, std::string body,
                 RpcCallOptions options, RpcCallback callback);

  /** Closes the current connection. The next call reconnects on demand. */
  void close();

 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace nexus::rpc

#endif  // NEXUS_RPC_RPC_CLIENT_H_
