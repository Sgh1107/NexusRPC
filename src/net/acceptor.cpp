#include "acceptor.h"

#include <cassert>
#include <cerrno>
#include <stdexcept>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "nexus/net/event_loop.h"

namespace nexus::net {

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listen_addr,
                   bool reuse_port)
    : loop_(loop),
      accept_socket_(createNonblockingSocket()),
      accept_channel_(loop_, accept_socket_.fd()),
      listening_(false),
      idle_fd_(::open("/dev/null", O_RDONLY | O_CLOEXEC)) {
  assert(loop_ != nullptr);
  accept_socket_.setReuseAddr(true);
  accept_socket_.setReusePort(reuse_port);
  accept_socket_.bind(listen_addr);
  accept_channel_.setReadCallback([this]() { handleRead(); });
}

Acceptor::~Acceptor() {
  accept_channel_.disableAll();
  accept_channel_.remove();
  if (idle_fd_ >= 0) {
    ::close(idle_fd_);
  }
}

void Acceptor::listen() {
  loop_->assertInLoopThread();
  accept_socket_.listen();
  listening_ = true;
  accept_channel_.enableReading();
}

void Acceptor::setNewConnectionCallback(NewConnectionCallback callback) {
  new_connection_callback_ = std::move(callback);
}

/**
 * Edge-triggered accept loop.
 *
 * Accepts connections in a loop until EAGAIN is returned. If EMFILE or ENFILE
 * is encountered, the idle fd is closed to free a slot, the new connection is
 * accepted and immediately closed, and the idle fd is re-opened.
 */
void Acceptor::handleRead() {
  loop_->assertInLoopThread();
  while (true) {
    InetAddress peer_addr(0);
    const int conn_fd = accept_socket_.accept(&peer_addr);
    if (conn_fd >= 0) {
      if (new_connection_callback_) {
        new_connection_callback_(conn_fd, peer_addr);
      } else {
        ::close(conn_fd);
      }
    } else {
      const int saved_errno = errno;
      if (saved_errno == EAGAIN) {
        break;  // All pending connections have been drained.
      }
      if (saved_errno == EMFILE || saved_errno == ENFILE) {
        // Free one descriptor slot, accept, and discard immediately.
        if (idle_fd_ >= 0) {
          ::close(idle_fd_);
          idle_fd_ = -1;
        }
        InetAddress discard_addr(0);
        const int tmp_fd = accept_socket_.accept(&discard_addr);
        if (tmp_fd >= 0) {
          ::close(tmp_fd);
        }
        if (idle_fd_ < 0) {
          idle_fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        }
        continue;
      }
      break;  // Unrecoverable error.
    }
  }
}

}  // namespace nexus::net
