#ifndef NEXUS_RPC_RPC_MESSAGE_H_
#define NEXUS_RPC_RPC_MESSAGE_H_

#include <cstdint>
#include <map>
#include <string>

#include "nexus/rpc/status.h"

namespace nexus::rpc {

/** Fully decoded unary request passed to a registered handler. */
struct RpcRequest {
  std::string service;
  std::string method;
  std::map<std::string, std::string> metadata;
  std::string body;
  std::uint64_t request_id = 0;
};

/** Handler result serialized into a unary response frame. */
struct RpcResponse {
  Status status;
  std::map<std::string, std::string> metadata;
  std::string body;
};

}  // namespace nexus::rpc

#endif  // NEXUS_RPC_RPC_MESSAGE_H_
