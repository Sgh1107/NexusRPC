#include "nexus/net/tcp_server.h"

#include <cassert>
#include <future>
#include <utility>

#include "nexus/net/event_loop.h"
#include "src/net/acceptor.h"

namespace nexus::net {

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

// ============================================================================
// Construction / destruction
// ============================================================================

TcpServer::TcpServer(EventLoop* loop, InetAddress listen_addr, std::string name)
    : main_loop_(loop),
      listen_addr_(std::move(listen_addr)),
      name_(std::move(name)),
      num_threads_(4),
      next_conn_id_(0),
      next_sub_loop_index_(0) {
  assert(main_loop_ != nullptr);
  acceptor_ = std::make_unique<Acceptor>(main_loop_, listen_addr_, false);
  acceptor_->setNewConnectionCallback(
      [this](int fd, const InetAddress& peer_addr) {
        onNewConnection(fd, peer_addr);
      });
}

TcpServer::~TcpServer() {
  if (!threads_.empty()) {
    stop();
  }
}

// ============================================================================
// start / stop
// ============================================================================

void TcpServer::start() {
  assert(!acceptor_->listening());
  assert(threads_.empty());

  for (int i = 0; i < num_threads_; ++i) {
    auto promise = std::make_shared<std::promise<EventLoop*>>();
    auto future = promise->get_future();

    threads_.push_back(std::make_unique<std::thread>(
        [promise = std::move(promise)]() {
          EventLoop loop;
          promise->set_value(&loop);
          loop.loop();
        }));

    sub_loops_.push_back(future.get());
  }

  acceptor_->listen();
}

void TcpServer::stop() {
  main_loop_->assertInLoopThread();

  // Destroy the acceptor first so no new connections arrive.
  acceptor_.reset();

  // Quit all sub-reactor loops and join their threads.
  for (EventLoop* loop : sub_loops_) {
    loop->quit();
  }
  for (auto& thread : threads_) {
    thread->join();
  }
  sub_loops_.clear();
  threads_.clear();

  // At this point all connections have been destroyed because the
  // EventLoops that owned them have stopped.
  {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.clear();
  }
}

// ============================================================================
// onNewConnection  (runs on main reactor)
// ============================================================================

void TcpServer::onNewConnection(int fd, const InetAddress& peer_addr) {
  main_loop_->assertInLoopThread();
  assert(fd >= 0);

  EventLoop* io_loop = getNextSubLoop();
  const std::uint32_t conn_id = next_conn_id_++;

  std::string conn_name = name_ + ":" + peer_addr.toIpPort() + "#" +
                          std::to_string(conn_id);

  TcpConnectionPtr conn = std::make_shared<TcpConnection>(
      io_loop, std::move(conn_name), fd, listen_addr_, peer_addr);

  conn->setConnectionCallback(connection_callback_);
  conn->setMessageCallback(message_callback_);

  // Internal close callback: defer removal to avoid use-after-free on Channel.
  conn->close_callback_ = [this](const TcpConnectionPtr& c) {
    c->getLoop()->queueInLoop(
        [this, c]() { removeConnectionInLoop(c); });
  };

  // Insert into map (main loop only — protected by mutex for sub-reactor reads).
  {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_[conn->name()] = conn;
  }

  io_loop->runInLoop(
      [conn = std::move(conn)]() { conn->connectEstablished(); });
}

// ============================================================================
// removeConnectionInLoop  (runs on sub-reactor via queueInLoop)
// ============================================================================

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn) {
  // Remove from the map under the mutex, but hold the shared_ptr until we
  // finish finalising on the sub-reactor loop.
  TcpConnectionPtr local_ref;
  {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(conn->name());
    if (it == connections_.end() || it->second != conn) {
      return;
    }
    local_ref = std::move(it->second);
    connections_.erase(it);
  }

  conn->getLoop()->runInLoop(
      [conn = std::move(local_ref)]() { conn->connectDestroyed(); });
}

// ============================================================================
// helpers
// ============================================================================

EventLoop* TcpServer::getNextSubLoop() {
  if (sub_loops_.empty()) {
    return main_loop_;
  }
  const std::uint32_t index =
      next_sub_loop_index_.fetch_add(1, std::memory_order_relaxed) %
      sub_loops_.size();
  return sub_loops_[index];
}

int TcpServer::connectionCount() const noexcept {
  std::lock_guard<std::mutex> lock(connections_mutex_);
  return static_cast<int>(connections_.size());
}

}  // namespace nexus::net
