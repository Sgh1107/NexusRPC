#include "nexus/net/tcp_connection.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <system_error>

#include <sys/socket.h>
#include <unistd.h>

#include "nexus/net/channel.h"
#include "nexus/net/event_loop.h"

namespace nexus::net {

// ============================================================================
// Construction / destruction
// ============================================================================

TcpConnection::TcpConnection(EventLoop* loop, std::string name, int fd,
                             InetAddress local_addr, InetAddress peer_addr)
    : loop_(loop),
      name_(std::move(name)),
      state_(State::kConnecting),
      channel_(std::make_unique<Channel>(loop, fd)),
      fd_(fd),
      input_buffer_(),
      output_buffer_(),
      local_addr_(std::move(local_addr)),
      peer_addr_(std::move(peer_addr)),
      high_water_mark_(0) {
  assert(loop_ != nullptr);
  assert(fd_ >= 0);
  channel_->setReadCallback([this]() { handleRead(); });
  channel_->setWriteCallback([this]() { handleWrite(); });
  channel_->setCloseCallback([this]() { handleClose(); });
  channel_->setErrorCallback([this]() { handleError(); });
}

TcpConnection::~TcpConnection() {
  assert(state_ == State::kDisconnected || state_ == State::kConnecting);
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

// ============================================================================
// connectEstablished / connectDestroyed
// ============================================================================

void TcpConnection::connectEstablished() {
  loop_->assertInLoopThread();
  assert(state_ == State::kConnecting);
  setState(State::kConnected);
  channel_->enableReading();
  // Notify the user that the connection is now active.
  if (connection_callback_) {
    connection_callback_(shared_from_this());
  }
}

/**
 * Final cleanup for a closed connection.
 *
 * This is called from removeConnectionInLoop (TcpServer), which runs on
 * the sub-reactor loop.  If handleClose already disabled the channel and
 * set the state to kDisconnected, this method just notifies the user and
 * removes the channel from epoll.
 */
void TcpConnection::connectDestroyed() {
  loop_->assertInLoopThread();
  // Only disable if handleClose hasn't run yet (e.g. forceClose path).
  if (state_ != State::kDisconnected) {
    setState(State::kDisconnected);
    channel_->disableAll();
  }
  // User-facing close notification (fires exactly once).
  if (connection_callback_) {
    connection_callback_(shared_from_this());
  }
  channel_->remove();
}

// ============================================================================
// send (public, thread-safe)
// ============================================================================

void TcpConnection::send(const void* data, std::size_t length) {
  if (state_ != State::kConnected) {
    return;
  }
  if (loop_->isInLoopThread()) {
    sendInLoop(data, length);
    return;
  }
  // Copy into a string so the caller's buffer does not need to outlive the
  // functor.
  std::string message(static_cast<const char*>(data), length);
  loop_->runInLoop(
      [self = shared_from_this(), msg = std::move(message)]() mutable {
        self->sendInLoop(std::move(msg));
      });
}

void TcpConnection::send(const char* data, std::size_t length) {
  send(static_cast<const void*>(data), length);
}

void TcpConnection::send(std::string_view data) {
  send(data.data(), data.size());
}

void TcpConnection::send(Buffer* buffer) {
  if (state_ != State::kConnected) {
    return;
  }
  const std::size_t length = buffer->readableBytes();
  if (length == 0) {
    return;
  }
  if (loop_->isInLoopThread()) {
    sendInLoop(buffer->peek(), length);
    buffer->retrieve(length);
    return;
  }
  std::string message(buffer->peek(), length);
  buffer->retrieve(length);
  loop_->runInLoop(
      [self = shared_from_this(), msg = std::move(message)]() mutable {
        self->sendInLoop(std::move(msg));
      });
}

// ============================================================================
// shutdown / forceClose (public, thread-safe)
// ============================================================================

void TcpConnection::shutdown() {
  if (state_ != State::kConnected) {
    return;
  }
  setState(State::kDisconnecting);
  loop_->runInLoop([self = shared_from_this()]() { self->shutdownInLoop(); });
}

void TcpConnection::forceClose() {
  loop_->runInLoop([self = shared_from_this()]() { self->forceCloseInLoop(); });
}

// ============================================================================
// handleRead  (ET: loop until EAGAIN)
// ============================================================================

/**
 * Edge-triggered read handler.
 *
 * Loops Buffer::readFd until it returns 0 (EOF) or a negative value with
 * errno EAGAIN (all data drained). For each chunk successfully read, the
 * message callback is invoked so the upper layer can process partial frames.
 */
void TcpConnection::handleRead() {
  loop_->assertInLoopThread();
  int saved_errno = 0;

  // Drain all available data (ET semantics).
  while (true) {
    saved_errno = 0;
    const ssize_t n = input_buffer_.readFd(fd_, &saved_errno);
    if (n > 0) {
      if (message_callback_) {
        message_callback_(shared_from_this(), &input_buffer_);
      }
    } else if (n == 0) {
      // Peer closed the connection.
      handleClose();
      return;
    } else {
      // n < 0
      if (saved_errno == EAGAIN) {
        break;  // All data has been consumed.
      }
      // Real error.
      errno = saved_errno;
      handleError();
      return;
    }
  }
}

// ============================================================================
// handleWrite (ET: write until EAGAIN or buffer drained)
// ============================================================================

void TcpConnection::handleWrite() {
  loop_->assertInLoopThread();
  if (state_ == State::kDisconnected) {
    return;
  }

  // Drain the output buffer.
  while (output_buffer_.readableBytes() > 0) {
    const ssize_t n = ::write(fd_, output_buffer_.peek(),
                              output_buffer_.readableBytes());
    if (n > 0) {
      output_buffer_.retrieve(static_cast<std::size_t>(n));
    } else {
      if (errno == EAGAIN) {
        break;  // Socket write buffer is full, wait for next EPOLLOUT.
      }
      handleError();
      return;
    }
  }

  if (output_buffer_.readableBytes() == 0) {
    channel_->disableWriting();
    if (write_complete_callback_) {
      write_complete_callback_(shared_from_this());
    }
    if (state_ == State::kDisconnecting) {
      shutdownInLoop();
    }
  }
}

// ============================================================================
// handleClose / handleError
// ============================================================================

/**
 * Called when the Channel detects that the peer has closed the connection.
 *
 * This method disables all channel events and notifies TcpServer via
 * close_callback_.  The TcpServer will then defer the actual removal
 * (removeConnectionInLoop) via queueInLoop to avoid use-after-free on
 * the Channel object.
 *
 * The user-facing connection_callback_ is NOT called here; it is called
 * later in connectDestroyed after the connection has been removed from
 * the TcpServer's map, ensuring a single clean notification.
 */
void TcpConnection::handleClose() {
  loop_->assertInLoopThread();
  if (state_ == State::kDisconnected) {
    return;
  }
  setState(State::kDisconnected);
  channel_->disableAll();

  // Notify the owner (TcpServer) so it can schedule removal.
  if (close_callback_) {
    close_callback_(shared_from_this());
  }
}

void TcpConnection::handleError() {
  loop_->assertInLoopThread();
  handleClose();
}

// ============================================================================
// sendInLoop (must run on loop thread)
// ============================================================================

void TcpConnection::sendInLoop(const void* data, std::size_t length) {
  loop_->assertInLoopThread();
  if (state_ != State::kConnected) {
    return;
  }

  ssize_t n_written = 0;
  const std::size_t remaining_before = output_buffer_.readableBytes();

  // If nothing is queued, try a direct write to avoid an extra epoll cycle.
  if (remaining_before == 0 && !channel_->isWriting()) {
    n_written = ::write(fd_, data, length);
    if (n_written < 0) {
      if (errno == EAGAIN) {
        n_written = 0;
      } else {
        handleError();
        return;
      }
    }
  }

  const std::size_t remaining = length - n_written;
  if (remaining > 0) {
    // High-water mark notification.
    const std::size_t new_buffer_size = remaining_before + remaining;
    if (high_water_mark_ > 0 && remaining_before < high_water_mark_ &&
        new_buffer_size >= high_water_mark_) {
      if (high_water_mark_callback_) {
        high_water_mark_callback_(shared_from_this(), new_buffer_size);
      }
    }
    output_buffer_.append(static_cast<const char*>(data) + n_written,
                          remaining);
    if (!channel_->isWriting()) {
      channel_->enableWriting();
    }
  }

  // If everything was written directly, fire write-complete.
  if (n_written > 0 && remaining == 0 && remaining_before == 0) {
    if (write_complete_callback_) {
      write_complete_callback_(shared_from_this());
    }
  }
}

void TcpConnection::sendInLoop(const std::string& message) {
  sendInLoop(message.data(), message.size());
}

// ============================================================================
// shutdownInLoop / forceCloseInLoop
// ============================================================================

void TcpConnection::shutdownInLoop() {
  loop_->assertInLoopThread();
  // Only call shutdown(SHUT_WR) when the output buffer is empty.
  if (state_ != State::kDisconnecting || output_buffer_.readableBytes() > 0) {
    return;
  }
  if (::shutdown(fd_, SHUT_WR) < 0) {
    // If the connection is already closed, ignore the error.
    if (errno != ENOTCONN && errno != ECONNRESET) {
      handleError();
    }
  }
}

void TcpConnection::forceCloseInLoop() {
  loop_->assertInLoopThread();
  if (state_ != State::kConnected && state_ != State::kDisconnecting) {
    return;
  }
  handleClose();
}

}  // namespace nexus::net
