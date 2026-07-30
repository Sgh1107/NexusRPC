/// @file rpc_client.cpp
/// @brief Single-connection unary RPC client implementation for TASK-014.

#include "nexus/rpc/rpc_client.h"

#include <arpa/inet.h>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <random>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <limits>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include "nexus/observability/logging.h"
#include "nexus/rpc/frame_codec.h"

namespace nexus::rpc {
namespace {

constexpr char kServiceMetadataKey[] = "nexus.service";
constexpr char kMethodMetadataKey[] = "nexus.method";
constexpr char kDeadlineMetadataKey[] = "deadline_unix_ms";
constexpr char kStatusCodeMetadataKey[] = "nexus.status_code";
constexpr char kStatusMessageMetadataKey[] = "nexus.status_message";

/** Returns a random non-zero starting point for the per-connection sequence. */
std::uint64_t makeInitialRequestId() {
  std::random_device random_device;
  const std::uint64_t value = (static_cast<std::uint64_t>(random_device()) << 32) |
                              static_cast<std::uint64_t>(random_device());
  return value == 0 ? 1 : value;
}

/** Converts a remaining deadline duration to a poll timeout in milliseconds. */
int remainingTimeoutMilliseconds(std::chrono::steady_clock::time_point deadline) {
  const auto remaining = deadline - std::chrono::steady_clock::now();
  if (remaining <= std::chrono::steady_clock::duration::zero()) {
    return 0;
  }
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
  return milliseconds > std::numeric_limits<int>::max()
             ? std::numeric_limits<int>::max()
             : static_cast<int>(milliseconds == 0 ? 1 : milliseconds);
}

/** Waits until a descriptor is ready for the requested event or times out. */
Status waitForEvent(int fd, short events, std::chrono::steady_clock::time_point deadline) {
  while (true) {
    struct pollfd poll_fd {};
    poll_fd.fd = fd;
    poll_fd.events = events;
    const int result = ::poll(&poll_fd, 1, remainingTimeoutMilliseconds(deadline));
    if (result > 0) {
      if ((poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return Status(StatusCode::kUnavailable, "connection became unavailable");
      }
      if ((poll_fd.revents & events) != 0) {
        return Status::Ok();
      }
      continue;
    }
    if (result == 0) {
      return Status(StatusCode::kDeadlineExceeded, "RPC deadline exceeded");
    }
    if (errno != EINTR) {
      return Status(StatusCode::kUnavailable, std::strerror(errno));
    }
  }
}

/** Writes the complete buffer before the supplied absolute deadline. */
Status writeAll(int fd, const std::uint8_t* data, std::size_t length,
                std::chrono::steady_clock::time_point deadline) {
  std::size_t offset = 0;
  while (offset < length) {
    const ssize_t written = ::send(fd, data + offset, length - offset, MSG_NOSIGNAL);
    if (written > 0) {
      offset += static_cast<std::size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      Status status = waitForEvent(fd, POLLOUT, deadline);
      if (!status.ok()) {
        return status;
      }
      continue;
    }
    return Status(StatusCode::kUnavailable, written == 0 ? "socket closed" : std::strerror(errno));
  }
  return Status::Ok();
}

/** Reads the requested number of bytes before the supplied absolute deadline. */
Status readAll(int fd, std::uint8_t* data, std::size_t length,
               std::chrono::steady_clock::time_point deadline) {
  std::size_t offset = 0;
  while (offset < length) {
    const ssize_t read_count = ::recv(fd, data + offset, length - offset, 0);
    if (read_count > 0) {
      offset += static_cast<std::size_t>(read_count);
      continue;
    }
    if (read_count == 0) {
      return Status(StatusCode::kUnavailable, "peer closed the connection");
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      Status status = waitForEvent(fd, POLLIN, deadline);
      if (!status.ok()) {
        return status;
      }
      continue;
    }
    return Status(StatusCode::kUnavailable, std::strerror(errno));
  }
  return Status::Ok();
}

/** Decodes the response status carried in reserved response metadata keys. */
Status decodeResponseStatus(const std::map<std::string, std::string>& metadata,
                            Status* response_status) {
  const auto code_it = metadata.find(kStatusCodeMetadataKey);
  if (code_it == metadata.end()) {
    return Status(StatusCode::kInternal, "response is missing status metadata");
  }

  int value = 0;
  const char* begin = code_it->second.data();
  const char* end = begin + code_it->second.size();
  const auto parse_result = std::from_chars(begin, end, value);
  if (parse_result.ec != std::errc() || parse_result.ptr != end || value < 0 ||
      value > static_cast<int>(StatusCode::kInternal)) {
    return Status(StatusCode::kInternal, "response contains an invalid status code");
  }

  const auto message_it = metadata.find(kStatusMessageMetadataKey);
  *response_status = Status(static_cast<StatusCode>(value),
                            message_it == metadata.end() ? std::string() : message_it->second);
  return Status::Ok();
}

/** Returns whether a call frame fits within the v1 wire limits. */
bool isFrameEncodable(const std::map<std::string, std::string>& metadata,
                      std::string_view body) {
  return body.size() <= kMaxBodyLength &&
         encodeMetadata(metadata).size() <= kMaxMetadataLength;
}

}  // namespace

class RpcClient::Impl {
 public:
  explicit Impl(RpcEndpoint endpoint)
      : endpoint_(std::move(endpoint)), next_request_id_(makeInitialRequestId()) {}

  ~Impl() { close(); }

  Result<RpcResponse> call(std::string_view service, std::string_view method,
                           std::string_view body, const RpcCallOptions& options) {
    if (service.empty() || method.empty()) {
      return Status(StatusCode::kInvalidArgument, "service and method must be provided");
    }
    if (options.deadline <= std::chrono::milliseconds::zero()) {
      return Status(StatusCode::kInvalidArgument, "deadline must be positive");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + options.deadline;
    Status status = ensureConnected(deadline);
    if (!status.ok()) {
      NEXUS_LOG_WARN("rpc connection failed endpoint={}:{} status={} message={}",
                     endpoint_.host, endpoint_.port,
                     static_cast<int>(status.code()), status.message());
      closeUnlocked();
      return status;
    }

    std::map<std::string, std::string> metadata = options.metadata;
    metadata[kServiceMetadataKey] = std::string(service);
    metadata[kMethodMetadataKey] = std::string(method);
    const auto deadline_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch() + options.deadline);
    metadata[kDeadlineMetadataKey] = std::to_string(deadline_unix_ms.count());
    if (!isFrameEncodable(metadata, body)) {
      NEXUS_LOG_WARN("rpc request exceeds frame limit service={} method={} body_bytes={}",
                     service, method, body.size());
      return Status(StatusCode::kResourceExhausted, "request frame exceeds protocol limits");
    }

    const std::uint64_t request_id = nextRequestId();
    const std::vector<std::uint8_t> frame =
        encodeFrame(request_id, MessageType::kRequest, metadata, body);
    status = writeAll(fd_, frame.data(), frame.size(), deadline);
    if (!status.ok()) {
      NEXUS_LOG_WARN("rpc request write failed request_id={} service={} method={} message={}",
                     request_id, service, method, status.message());
      closeUnlocked();
      return status;
    }

    std::array<std::uint8_t, kFixedHeaderSize> header_bytes {};
    status = readAll(fd_, header_bytes.data(), header_bytes.size(), deadline);
    if (!status.ok()) {
      closeUnlocked();
      return status;
    }

    RpcFrameHeader header;
    const ParseResult header_result = decodeHeader(header_bytes.data(), &header);
    if (header_result != ParseResult::kOk ||
        header.msg_type != static_cast<std::uint8_t>(MessageType::kResponse) ||
        header.request_id != request_id) {
      closeUnlocked();
      return Status(StatusCode::kUnavailable, "invalid or mismatched RPC response frame");
    }

    const std::size_t payload_length = header.total_length - kFixedHeaderSize;
    std::vector<std::uint8_t> payload(payload_length);
    if (!payload.empty()) {
      status = readAll(fd_, payload.data(), payload.size(), deadline);
      if (!status.ok()) {
        closeUnlocked();
        return status;
      }
    }

    std::map<std::string, std::string> response_metadata;
    if (header.meta_length != 0 &&
        decodeMetadata(payload.data(), header.meta_length, &response_metadata) != ParseResult::kOk) {
      closeUnlocked();
      return Status(StatusCode::kUnavailable, "invalid response metadata");
    }

    Status response_status;
    status = decodeResponseStatus(response_metadata, &response_status);
    if (!status.ok()) {
      closeUnlocked();
      return status;
    }

    if (!response_status.ok()) {
      return response_status;
    }

    RpcResponse response;
    response.status = Status::Ok();
    response.metadata = std::move(response_metadata);
    if (header.body_length != 0) {
      response.body.assign(
          reinterpret_cast<const char*>(payload.data() + header.meta_length), header.body_length);
    }
    return response;
  }

  void close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closeUnlocked();
  }

 private:
  Status ensureConnected(std::chrono::steady_clock::time_point deadline) {
    if (fd_ >= 0) {
      return Status::Ok();
    }
    if (endpoint_.host.empty() || endpoint_.port == 0) {
      return Status(StatusCode::kInvalidArgument, "endpoint host and port must be provided");
    }

    struct addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo* addresses = nullptr;
    const std::string port = std::to_string(endpoint_.port);
    const int getaddrinfo_result = ::getaddrinfo(endpoint_.host.c_str(), port.c_str(), &hints, &addresses);
    if (getaddrinfo_result != 0) {
      return Status(StatusCode::kUnavailable, ::gai_strerror(getaddrinfo_result));
    }

    Status status(StatusCode::kUnavailable, "failed to connect to endpoint");
    for (struct addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
      const int socket_fd = ::socket(address->ai_family,
                                     address->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                                     address->ai_protocol);
      if (socket_fd < 0) {
        continue;
      }
      if (::connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0) {
        fd_ = socket_fd;
        status = Status::Ok();
        break;
      }
      if (errno == EINPROGRESS) {
        status = waitForEvent(socket_fd, POLLOUT, deadline);
        if (status.ok()) {
          int socket_error = 0;
          socklen_t error_length = sizeof(socket_error);
          if (::getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length) == 0 &&
              socket_error == 0) {
            fd_ = socket_fd;
            break;
          }
          status = Status(StatusCode::kUnavailable,
                          socket_error == 0 ? "connection failed" : std::strerror(socket_error));
        }
      } else {
        status = Status(StatusCode::kUnavailable, std::strerror(errno));
      }
      ::close(socket_fd);
      if (status.code() == StatusCode::kDeadlineExceeded) {
        break;
      }
    }
    ::freeaddrinfo(addresses);
    return status;
  }

  std::uint64_t nextRequestId() noexcept {
    const std::uint64_t request_id = next_request_id_;
    ++next_request_id_;
    if (next_request_id_ == 0) {
      ++next_request_id_;
    }
    return request_id;
  }

  void closeUnlocked() noexcept {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  const RpcEndpoint endpoint_;
  std::mutex mutex_;
  int fd_ = -1;
  std::uint64_t next_request_id_;
};

RpcClient::RpcClient(RpcEndpoint endpoint) : impl_(std::make_shared<Impl>(std::move(endpoint))) {}

RpcClient::~RpcClient() = default;

Result<RpcResponse> RpcClient::call(std::string_view service, std::string_view method,
                                    std::string_view body, const RpcCallOptions& options) {
  return impl_->call(service, method, body, options);
}

std::future<Result<RpcResponse>> RpcClient::callFuture(
    std::string service, std::string method, std::string body, RpcCallOptions options) {
  std::shared_ptr<Impl> impl = impl_;
  return std::async(std::launch::async,
                    [impl = std::move(impl), service = std::move(service),
                     method = std::move(method), body = std::move(body),
                     options = std::move(options)]() mutable {
                      return impl->call(service, method, body, options);
                    });
}

void RpcClient::callAsync(std::string service, std::string method, std::string body,
                          RpcCallOptions options, RpcCallback callback) {
  std::shared_ptr<Impl> impl = impl_;
  std::thread([impl = std::move(impl), service = std::move(service), method = std::move(method),
               body = std::move(body), options = std::move(options), callback = std::move(callback)]() mutable {
    Result<RpcResponse> result = impl->call(service, method, body, options);
    if (callback) {
      try {
        callback(std::move(result));
      } catch (...) {
      }
    }
  }).detach();
}

void RpcClient::close() { impl_->close(); }

}  // namespace nexus::rpc
