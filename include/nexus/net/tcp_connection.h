#ifndef NEXUS_NET_TCP_CONNECTION_H_
#define NEXUS_NET_TCP_CONNECTION_H_

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "nexus/net/buffer.h"
#include "nexus/net/socket.h"

namespace nexus::net {

class Channel;
class EventLoop;
class TcpServer;

// ---------------------------------------------------------------------------
// TcpConnection
// ---------------------------------------------------------------------------

/**
 * Manages one established TCP connection.
 *
 * TcpConnection owns the connected socket fd, an input buffer, an output
 * buffer, and a Channel registered on the sub-reactor EventLoop. All
 * callbacks run on that EventLoop thread.
 *
 * The connection is reference-counted through shared_ptr. The owner
 * (typically TcpServer) holds one reference; pending functors queued via
 * runInLoop may hold temporary references during handover.
 */
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
 public:
  /// Fired when a connection is established or closed.
  using ConnectionCallback =
      std::function<void(const std::shared_ptr<TcpConnection>&)>;

  /// Fired when new data arrives in the input buffer.
  using MessageCallback =
      std::function<void(const std::shared_ptr<TcpConnection>&, Buffer*)>;

  /// Fired when the output buffer has been completely flushed.
  using WriteCompleteCallback =
      std::function<void(const std::shared_ptr<TcpConnection>&)>;

  /// Fired when the output buffer exceeds the configured high-water mark.
  using HighWaterMarkCallback =
      std::function<void(const std::shared_ptr<TcpConnection>&, std::size_t)>;

  /// Fired internally when the connection is closed by the peer.
  using CloseCallback =
      std::function<void(const std::shared_ptr<TcpConnection>&)>;

  TcpConnection(EventLoop* loop, std::string name, int fd,
                InetAddress local_addr, InetAddress peer_addr);

  ~TcpConnection();

  TcpConnection(const TcpConnection&) = delete;
  TcpConnection& operator=(const TcpConnection&) = delete;

  // ---- accessors ---------------------------------------------------------

  const std::string& name() const noexcept { return name_; }
  EventLoop* getLoop() const noexcept { return loop_; }
  int fd() const noexcept { return fd_; }
  const InetAddress& localAddress() const noexcept { return local_addr_; }
  const InetAddress& peerAddress() const noexcept { return peer_addr_; }

  // ---- I/O ---------------------------------------------------------------

  void send(const void* data, std::size_t length);
  void send(const char* data, std::size_t length);
  void send(std::string_view data);
  void send(Buffer* buffer);
  void shutdown();
  void forceClose();

  // ---- callbacks ---------------------------------------------------------

  void setConnectionCallback(ConnectionCallback cb) {
    connection_callback_ = std::move(cb);
  }
  void setMessageCallback(MessageCallback cb) {
    message_callback_ = std::move(cb);
  }
  void setWriteCompleteCallback(WriteCompleteCallback cb) {
    write_complete_callback_ = std::move(cb);
  }
  void setHighWaterMarkCallback(HighWaterMarkCallback cb, std::size_t mark) {
    high_water_mark_callback_ = std::move(cb);
    high_water_mark_ = mark;
  }
  void connectEstablished();
  void connectDestroyed();

 private:
  enum class State { kDisconnected, kConnecting, kConnected, kDisconnecting };

  void handleRead();
  void handleWrite();
  void handleClose();
  void handleError();
  void sendInLoop(const void* data, std::size_t length);
  void sendInLoop(const std::string& message);
  void shutdownInLoop();
  void forceCloseInLoop();

  void setState(State s) noexcept { state_ = s; }

  friend class TcpServer;

  EventLoop* loop_;
  const std::string name_;
  std::atomic<State> state_{State::kDisconnected};
  std::unique_ptr<Channel> channel_;
  int fd_;
  Buffer input_buffer_;
  Buffer output_buffer_;
  InetAddress local_addr_;
  InetAddress peer_addr_;
  ConnectionCallback connection_callback_;
  MessageCallback message_callback_;
  WriteCompleteCallback write_complete_callback_;
  HighWaterMarkCallback high_water_mark_callback_;
  CloseCallback close_callback_;
  std::size_t high_water_mark_;
};

}  // namespace nexus::net
#endif  // NEXUS_NET_TCP_CONNECTION_H_
