#ifndef NEXUS_RPC_RPC_SERVER_H_
#define NEXUS_RPC_RPC_SERVER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <google/protobuf/message.h>

#include "nexus/rpc/rpc_message.h"

namespace nexus::rpc {

/** Application callback used to process one unary RPC request. */
using RpcHandler = std::function<RpcResponse(const RpcRequest&)>;

/** Typed application callback for generated Protobuf request/response types. */
template <typename Request, typename Response>
using ProtobufRpcHandler = std::function<Status(const Request&, Response*)>;

/**
 * Hosts unary RPC services on a TCP port.
 *
 * TcpConnection callbacks only parse and enqueue valid requests. Registered
 * handlers execute on the server-owned worker threads and responses are sent
 * back through the connection's owning EventLoop.
 */
class RpcServer {
 public:
  /** Creates a server with the requested number of handler worker threads. */
  explicit RpcServer(std::size_t worker_thread_count = 0);
  ~RpcServer();

  RpcServer(const RpcServer&) = delete;
  RpcServer& operator=(const RpcServer&) = delete;

  /**
   * Registers a unique service and method pair.
   *
   * @return kInvalidArgument for empty names or handlers, and kInternal when
   *     the pair has already been registered or the server is running.
   */
  Status registerService(std::string service, std::string method,
                         RpcHandler handler);

  template <typename Request, typename Response>
  Status registerService(std::string service, std::string method,
                         ProtobufRpcHandler<Request, Response> handler) {
    static_assert(std::is_base_of_v<google::protobuf::Message, Request>,
                  "Request must be a generated Protobuf message");
    static_assert(std::is_base_of_v<google::protobuf::Message, Response>,
                  "Response must be a generated Protobuf message");
    if (!handler) {
      return Status(StatusCode::kInvalidArgument, "handler must be provided");
    }

    return registerService(
        std::move(service), std::move(method),
        [handler = std::move(handler)](const RpcRequest& wire_request) mutable {
          Request request;
          if (!request.ParseFromString(wire_request.body)) {
            return RpcResponse{Status(StatusCode::kInvalidArgument,
                                      "failed to parse Protobuf request"), {}, {}};
          }

          Response response;
          Status status = handler(request, &response);
          if (!status.ok()) {
            return RpcResponse{std::move(status), {}, {}};
          }

          RpcResponse wire_response;
          wire_response.status = Status::Ok();
          if (!response.SerializeToString(&wire_response.body)) {
            wire_response.status = Status(StatusCode::kInternal,
                                          "failed to serialize Protobuf response");
          }
          return wire_response;
        });
  }

  /** Starts the TCP listener on every IPv4 interface at @p port. */
  Status start(std::uint16_t port);

  /** Stops accepting requests and joins the I/O and handler threads. */
  void stop();

  /** Returns whether the listener has completed startup. */
  bool isRunning() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace nexus::rpc

#endif  // NEXUS_RPC_RPC_SERVER_H_
