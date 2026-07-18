#ifndef NEXUS_NET_TCP_SERVER_H_
#define NEXUS_NET_TCP_SERVER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "nexus/net/socket.h"
#include "nexus/net/tcp_connection.h"

namespace nexus::net {

class EventLoop;

/**
 * Multi-reactor TCP server (main/sub reactor pattern).
 *
 * One main reactor EventLoop (caller-owned) runs the Acceptor.  A configurable
 * number of sub-reactor threads each run their own EventLoop and handle I/O
 * for established connections. New connections are distributed round-robin.
 *
 * All user callbacks fire on the sub-reactor thread that owns the connection.
 *
 * Example (Echo server):
 * @code
 *   EventLoop loop;
 *   TcpServer server(&loop, InetAddress(9600), "Echo");
 *   server.setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buf) {
 *     conn->send(buf);
 *   });
 *   server.start(4);
 *   loop.loop();
 * @endcode
 */
class TcpServer {
 public:
  using ConnectionCallback = TcpConnection::ConnectionCallback;
  using MessageCallback = TcpConnection::MessageCallback;

  TcpServer(EventLoop* loop, InetAddress listen_addr, std::string name);
  ~TcpServer();

  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  // ---- options (must be called before start) -----------------------------

  void setThreadNum(int num_threads) { num_threads_ = num_threads; }
  void setConnectionCallback(ConnectionCallback cb) {
    connection_callback_ = std::move(cb);
  }
  void setMessageCallback(MessageCallback cb) {
    message_callback_ = std::move(cb);
  }

  // ---- lifecycle ---------------------------------------------------------

  void start();
  void stop();

  // ---- accessors ---------------------------------------------------------

  const std::string& name() const noexcept { return name_; }
  int connectionCount() const noexcept;

 private:
  void onNewConnection(int fd, const InetAddress& peer_addr);
  void removeConnectionInLoop(const std::shared_ptr<TcpConnection>& conn);
  EventLoop* getNextSubLoop();

  EventLoop* main_loop_;
  const InetAddress listen_addr_;
  const std::string name_;
  int num_threads_;

  // Sub-reactor threads and their stack-allocated EventLoops.
  std::vector<EventLoop*> sub_loops_;
  std::vector<std::unique_ptr<std::thread>> threads_;

  class Acceptor;
  std::unique_ptr<Acceptor> acceptor_;

  std::unordered_map<std::string, std::shared_ptr<TcpConnection>> connections_;
  mutable std::mutex connections_mutex_;

  std::uint32_t next_conn_id_;
  std::uint32_t next_sub_loop_index_;

  ConnectionCallback connection_callback_;
  MessageCallback message_callback_;
};

}  // namespace nexus::net
#endif  // NEXUS_NET_TCP_SERVER_H_
