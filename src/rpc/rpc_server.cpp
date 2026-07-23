/// @file rpc_server.cpp
/// @brief Unary RPC server implementation for TASK-013.

#include "nexus/rpc/rpc_server.h"

#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <mutex>
#include <queue>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

#include "nexus/net/buffer.h"
#include "nexus/net/event_loop.h"
#include "nexus/net/socket.h"
#include "nexus/net/tcp_connection.h"
#include "nexus/net/tcp_server.h"
#include "nexus/rpc/frame_codec.h"

namespace nexus::rpc {
namespace {

constexpr char kServiceMetadataKey[] = "nexus.service";
constexpr char kMethodMetadataKey[] = "nexus.method";
constexpr char kDeadlineMetadataKey[] = "deadline_unix_ms";
constexpr char kStatusCodeMetadataKey[] = "nexus.status_code";
constexpr char kStatusMessageMetadataKey[] = "nexus.status_message";
constexpr std::size_t kMaxPendingRequestsPerConnection = 1024;
constexpr std::size_t kMaxQueuedRequests = 8192;

/** Returns the response metadata representation of a StatusCode. */
std::string statusCodeToString(StatusCode code) {
  return std::to_string(static_cast<int>(code));
}

/** Returns whether a frame can be passed to encodeFrame without assertion. */
bool isFrameEncodable(const std::map<std::string, std::string>& metadata,
                      const std::string& body) {
  return body.size() <= kMaxBodyLength &&
         encodeMetadata(metadata).size() <= kMaxMetadataLength;
}

/** Validates an optional client deadline before invoking a handler. */
Status validateRequestDeadline(const RpcRequest& request) {
  const auto deadline_it = request.metadata.find(kDeadlineMetadataKey);
  if (deadline_it == request.metadata.end()) {
    return Status::Ok();
  }

  std::uint64_t deadline_unix_ms = 0;
  const char* begin = deadline_it->second.data();
  const char* end = begin + deadline_it->second.size();
  const auto parse_result = std::from_chars(begin, end, deadline_unix_ms);
  if (parse_result.ec != std::errc() || parse_result.ptr != end) {
    return Status(StatusCode::kInvalidArgument, "deadline_unix_ms is invalid");
  }

  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch());
  if (deadline_unix_ms <= static_cast<std::uint64_t>(now.count())) {
    return Status(StatusCode::kDeadlineExceeded, "RPC deadline exceeded");
  }
  return Status::Ok();
}

/** Encodes a response while preserving application metadata. */
std::vector<std::uint8_t> makeResponseFrame(std::uint64_t request_id,
                                            RpcResponse response) {
  response.metadata[kStatusCodeMetadataKey] = statusCodeToString(response.status.code());
  if (!response.status.message().empty()) {
    response.metadata[kStatusMessageMetadataKey] = response.status.message();
  }

  if (!isFrameEncodable(response.metadata, response.body)) {
    response = RpcResponse{Status(StatusCode::kResourceExhausted,
                                  "response frame exceeds protocol limits"), {}, {}};
    response.metadata[kStatusCodeMetadataKey] = statusCodeToString(response.status.code());
    response.metadata[kStatusMessageMetadataKey] = response.status.message();
  }

  return encodeFrame(request_id, MessageType::kResponse, response.metadata, response.body);
}

}  // namespace

class RpcServer::Impl {
 public:
  explicit Impl(std::size_t worker_thread_count)
      : worker_thread_count_(worker_thread_count == 0 ? 1 : worker_thread_count) {}

  ~Impl() { stop(); }

  Status registerService(std::string service, std::string method, RpcHandler handler) {
    if (service.empty() || method.empty() || !handler) {
      return Status(StatusCode::kInvalidArgument,
                    "service, method, and handler must be provided");
    }

    std::lock_guard<std::mutex> lock(handlers_mutex_);
    if (running_) {
      return Status(StatusCode::kInternal, "cannot register a service after start");
    }

    const std::string key = makeHandlerKey(service, method);
    if (handlers_.find(key) != handlers_.end()) {
      return Status(StatusCode::kInternal, "service method is already registered");
    }
    handlers_.emplace(std::move(key), std::move(handler));
    return Status::Ok();
  }

  Status start(std::uint16_t port) {
    if (port == 0) {
      return Status(StatusCode::kInvalidArgument, "port must be non-zero");
    }
    if (running_.exchange(true)) {
      return Status(StatusCode::kInternal, "server is already running");
    }

    stopping_ = false;
    startWorkers();

    std::promise<Status> startup_promise;
    std::future<Status> startup_future = startup_promise.get_future();
    io_thread_ = std::thread([this, port, promise = std::move(startup_promise)]() mutable {
      bool startup_reported = false;
      try {
        net::EventLoop loop;
        loop_ = &loop;
        tcp_server_ = std::make_unique<net::TcpServer>(
            &loop, net::InetAddress(port), "NexusRpcServer");
        tcp_server_->setThreadNum(0);
        tcp_server_->setConnectionCallback(
            [this](const std::shared_ptr<net::TcpConnection>& connection) {
              onConnection(connection);
            });
        tcp_server_->setMessageCallback(
            [this](const std::shared_ptr<net::TcpConnection>& connection, net::Buffer* buffer) {
              onMessage(connection, buffer);
            });
        tcp_server_->start();
        promise.set_value(Status::Ok());
        startup_reported = true;
        loop.loop();
        tcp_server_.reset();
        loop_ = nullptr;
      } catch (const std::exception& exception) {
        loop_ = nullptr;
        if (!startup_reported) {
          promise.set_value(Status(StatusCode::kUnavailable, exception.what()));
        }
      } catch (...) {
        loop_ = nullptr;
        if (!startup_reported) {
          promise.set_value(Status(StatusCode::kInternal, "RPC server startup failed"));
        }
      }
    });

    Status startup_status = startup_future.get();
    if (!startup_status.ok()) {
      if (io_thread_.joinable()) {
        io_thread_.join();
      }
      stopWorkers();
      running_ = false;
    }
    return startup_status;
  }

  void stop() {
    if (!running_.exchange(false)) {
      return;
    }

    stopping_ = true;
    net::EventLoop* loop = loop_;
    if (loop != nullptr) {
      loop->queueInLoop([this]() {
        if (tcp_server_ != nullptr) {
          tcp_server_->stop();
        }
        loop_->quit();
      });
    }
    if (io_thread_.joinable()) {
      io_thread_.join();
    }
    stopWorkers();

    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.clear();
  }

  bool isRunning() const noexcept { return running_; }

 private:
  struct ConnectionState {
    FrameParser parser;
    std::atomic<std::size_t> pending_requests{0};
  };

  struct PendingRequest {
    std::shared_ptr<net::TcpConnection> connection;
    std::shared_ptr<ConnectionState> state;
    RpcRequest request;
  };

  static std::string makeHandlerKey(std::string_view service, std::string_view method) {
    return std::string(service) + '\n' + std::string(method);
  }

  void startWorkers() {
    for (std::size_t index = 0; index < worker_thread_count_; ++index) {
      workers_.emplace_back([this]() { workerLoop(); });
    }
  }

  void stopWorkers() {
    {
      std::lock_guard<std::mutex> lock(work_queue_mutex_);
      stopping_ = true;
    }
    work_queue_cv_.notify_all();
    for (std::thread& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    workers_.clear();

    std::queue<PendingRequest> empty;
    std::lock_guard<std::mutex> lock(work_queue_mutex_);
    work_queue_.swap(empty);
  }

  void onConnection(const std::shared_ptr<net::TcpConnection>& connection) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    if (connections_.find(connection.get()) == connections_.end()) {
      connections_.emplace(connection.get(), std::make_shared<ConnectionState>());
    } else {
      connections_.erase(connection.get());
    }
  }

  void onMessage(const std::shared_ptr<net::TcpConnection>& connection, net::Buffer* buffer) {
    std::shared_ptr<ConnectionState> state;
    {
      std::lock_guard<std::mutex> lock(connections_mutex_);
      const auto it = connections_.find(connection.get());
      if (it == connections_.end()) {
        connection->forceClose();
        return;
      }
      state = it->second;
    }

    const std::size_t readable_bytes = buffer->readableBytes();
    const auto* data = reinterpret_cast<const std::uint8_t*>(buffer->peek());
    const ParseResult feed_result = state->parser.feed(data, readable_bytes);
    buffer->retrieve(readable_bytes);

    if (feed_result != ParseResult::kOk && feed_result != ParseResult::kNeedMoreData) {
      connection->forceClose();
      return;
    }

    while (state->parser.isComplete()) {
      const RpcFrameHeader header = state->parser.header();
      if (header.msg_type != static_cast<std::uint8_t>(MessageType::kRequest)) {
        connection->forceClose();
        return;
      }

      const auto& metadata = state->parser.metadata();
      const auto service_it = metadata.find(kServiceMetadataKey);
      const auto method_it = metadata.find(kMethodMetadataKey);
      if (service_it == metadata.end() || method_it == metadata.end() ||
          service_it->second.empty() || method_it->second.empty()) {
        RpcResponse response{Status(StatusCode::kInvalidArgument,
                                    "request metadata lacks service or method"), {}, {}};
        sendResponse(connection, header.request_id, std::move(response));
        connection->shutdown();
        return;
      }

      RpcRequest request;
      request.service = service_it->second;
      request.method = method_it->second;
      request.metadata = metadata;
      if (state->parser.body().empty()) {
        request.body.clear();
      } else {
        request.body.assign(reinterpret_cast<const char*>(state->parser.body().data()),
                            state->parser.body().size());
      }
      request.request_id = header.request_id;
      state->parser.reset();

      if (state->pending_requests.fetch_add(1) >= kMaxPendingRequestsPerConnection) {
        state->pending_requests.fetch_sub(1);
        RpcResponse response{Status(StatusCode::kResourceExhausted,
                                    "too many in-flight requests"), {}, {}};
        sendResponse(connection, header.request_id, std::move(response));
      } else if (!enqueue(PendingRequest{connection, state, std::move(request)})) {
        state->pending_requests.fetch_sub(1);
        RpcResponse response{Status(StatusCode::kResourceExhausted,
                                    "RPC handler queue is full"), {}, {}};
        sendResponse(connection, header.request_id, std::move(response));
      }

      const ParseResult next_result = state->parser.feed(nullptr, 0);
      if (next_result != ParseResult::kOk && next_result != ParseResult::kNeedMoreData) {
        connection->forceClose();
        return;
      }
    }
  }

  bool enqueue(PendingRequest request) {
    std::lock_guard<std::mutex> lock(work_queue_mutex_);
    if (stopping_ || work_queue_.size() >= kMaxQueuedRequests) {
      return false;
    }
    work_queue_.push(std::move(request));
    work_queue_cv_.notify_one();
    return true;
  }

  void workerLoop() {
    while (true) {
      PendingRequest pending;
      {
        std::unique_lock<std::mutex> lock(work_queue_mutex_);
        work_queue_cv_.wait(lock, [this]() { return stopping_ || !work_queue_.empty(); });
        if (stopping_ && work_queue_.empty()) {
          return;
        }
        pending = std::move(work_queue_.front());
        work_queue_.pop();
      }

      RpcResponse response;
      RpcHandler handler;
      {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        const auto it = handlers_.find(makeHandlerKey(pending.request.service, pending.request.method));
        if (it != handlers_.end()) {
          handler = it->second;
        }
      }

      const Status deadline_status = validateRequestDeadline(pending.request);
      if (!deadline_status.ok()) {
        response.status = deadline_status;
      } else if (!handler) {
        response.status = Status(StatusCode::kNotFound, "service method is not registered");
      } else {
        try {
          response = handler(pending.request);
        } catch (const std::exception& exception) {
          response.status = Status(StatusCode::kInternal, exception.what());
          response.body.clear();
        } catch (...) {
          response.status = Status(StatusCode::kInternal, "unclassified handler exception");
          response.body.clear();
        }
      }

      sendResponse(pending.connection, pending.request.request_id, std::move(response));
      pending.state->pending_requests.fetch_sub(1);
    }
  }

  void sendResponse(const std::shared_ptr<net::TcpConnection>& connection,
                    std::uint64_t request_id, RpcResponse response) {
    const std::vector<std::uint8_t> frame = makeResponseFrame(request_id, std::move(response));
    connection->send(reinterpret_cast<const char*>(frame.data()), frame.size());
  }

  const std::size_t worker_thread_count_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};
  std::thread io_thread_;
  net::EventLoop* loop_ = nullptr;
  std::unique_ptr<net::TcpServer> tcp_server_;

  std::mutex handlers_mutex_;
  std::unordered_map<std::string, RpcHandler> handlers_;

  std::mutex connections_mutex_;
  std::unordered_map<net::TcpConnection*, std::shared_ptr<ConnectionState>> connections_;

  std::mutex work_queue_mutex_;
  std::condition_variable work_queue_cv_;
  std::queue<PendingRequest> work_queue_;
  std::vector<std::thread> workers_;
};

RpcServer::RpcServer(std::size_t worker_thread_count)
    : impl_(std::make_unique<Impl>(worker_thread_count)) {}

RpcServer::~RpcServer() = default;

Status RpcServer::registerService(std::string service, std::string method, RpcHandler handler) {
  return impl_->registerService(std::move(service), std::move(method), std::move(handler));
}

Status RpcServer::start(std::uint16_t port) { return impl_->start(port); }

void RpcServer::stop() { impl_->stop(); }

bool RpcServer::isRunning() const noexcept { return impl_->isRunning(); }

}  // namespace nexus::rpc
