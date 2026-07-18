#ifndef NEXUS_NET_ACCEPTOR_H_
#define NEXUS_NET_ACCEPTOR_H_

#include <functional>

#include "nexus/net/channel.h"
#include "nexus/net/socket.h"

namespace nexus::net {

class EventLoop;
class InetAddress;

/**
 * Internal acceptor that listens on one port and distributes new connections
 * via a callback.
 *
 * Acceptor runs on the main reactor EventLoop. The listen socket and its
 * Channel are owned directly. In edge-triggered mode the accept loop drains
 * until EAGAIN before returning to the event loop.
 */
class Acceptor {
 public:
  using NewConnectionCallback =
      std::function<void(int fd, const InetAddress& peer_addr)>;

  /**
   * @param loop        The main-reactor EventLoop.
   * @param listen_addr  Address and port to bind.
   * @param reuse_port Whether to enable SO_REUSEPORT.
   */
  Acceptor(EventLoop* loop, const InetAddress& listen_addr,
           bool reuse_port = false);

  ~Acceptor();

  Acceptor(const Acceptor&) = delete;
  Acceptor& operator=(const Acceptor&) = delete;

  /// Starts listening on the configured address.
  void listen();

  /// Returns true after listen() has been called.
  bool listening() const noexcept { return listening_; }

  /// Sets the callback invoked for each new connection.
  void setNewConnectionCallback(NewConnectionCallback callback);

 private:
  /// Called by the Channel when the listen fd becomes readable.
  void handleRead();

  EventLoop* loop_;
  Socket accept_socket_;
  Channel accept_channel_;
  NewConnectionCallback new_connection_callback_;
  bool listening_;

  /// One reserved fd to handle EMFILE gracefully.
  int idle_fd_;
};

}  // namespace nexus::net

#endif  // NEXUS_NET_ACCEPTOR_H_
