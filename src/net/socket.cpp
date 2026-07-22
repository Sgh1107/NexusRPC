#include "nexus/net/socket.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <system_error>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nexus::net {

// ============================================================================
// InetAddress
// ============================================================================

InetAddress::InetAddress(uint16_t port) {
  std::memset(&addr_, 0, sizeof(addr_));
  addr_.sin_family = AF_INET;
  addr_.sin_addr.s_addr = htonl(INADDR_ANY);
  addr_.sin_port = htons(port);
}

InetAddress InetAddress::loopback(uint16_t port) {
  InetAddress addr(port);
  addr.addr_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.addr_.sin_port = htons(port);
  return addr;
}

InetAddress::InetAddress(std::string_view ip, uint16_t port) {
  std::memset(&addr_, 0, sizeof(addr_));
  addr_.sin_family = AF_INET;
  addr_.sin_port = htons(port);
  if (::inet_pton(AF_INET, ip.data(), &addr_.sin_addr) <= 0) {
    throw std::invalid_argument("InetAddress: invalid IP address");
  }
}

InetAddress::InetAddress(const struct sockaddr_in& addr) : addr_(addr) {}

const struct sockaddr_in& InetAddress::sockAddr() const noexcept {
  return addr_;
}

std::string InetAddress::toIp() const {
  char buf[64];
  if (::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf)) == nullptr) {
    return {};
  }
  return buf;
}

std::string InetAddress::toIpPort() const {
  char buf[64];
  if (::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf)) == nullptr) {
    return {};
  }
  const std::size_t len = std::strlen(buf);
  snprintf(buf + len, sizeof(buf) - len, ":%u", ntohs(addr_.sin_port));
  return buf;
}

uint16_t InetAddress::port() const noexcept {
  return ntohs(addr_.sin_port);
}

// ============================================================================
// Socket
// ============================================================================

Socket::Socket(int fd) : fd_(fd) {}

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) {
  other.fd_ = -1;
}

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}

Socket::~Socket() {
  if (fd_ >= 0) ::close(fd_);
}

int Socket::fd() const noexcept {
  return fd_;
}

void Socket::setReuseAddr(bool on) const {
  int opt = on ? 1 : 0;
  if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    throw std::system_error(errno, std::system_category(),
                            "setsockopt SO_REUSEADDR");
  }
}

void Socket::setReusePort(bool on) const {
  int opt = on ? 1 : 0;
  if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
    throw std::system_error(errno, std::system_category(),
                            "setsockopt SO_REUSEPORT");
  }
}

void Socket::setTcpNoDelay(bool on) const {
  int opt = on ? 1 : 0;
  if (::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
    throw std::system_error(errno, std::system_category(),
                            "setsockopt TCP_NODELAY");
  }
}

void Socket::setKeepAlive(bool on) const {
  int opt = on ? 1 : 0;
  if (::setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
    throw std::system_error(errno, std::system_category(),
                            "setsockopt SO_KEEPALIVE");
  }
}

void Socket::bind(const InetAddress& local) const {
  const struct sockaddr_in& addr = local.sockAddr();
  if (::bind(fd_, reinterpret_cast<const struct sockaddr*>(&addr),
             sizeof(addr)) < 0) {
    throw std::system_error(errno, std::system_category(), "bind");
  }
}

void Socket::listen() const {
  if (::listen(fd_, 1024) < 0) {
    throw std::system_error(errno, std::system_category(), "listen");
  }
}

int Socket::accept(InetAddress* peer_addr) const {
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  const int conn_fd = ::accept4(fd_, reinterpret_cast<struct sockaddr*>(&addr),
                                &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (conn_fd >= 0 && peer_addr != nullptr) {
    *peer_addr = InetAddress(addr);
  }
  return conn_fd;
}

// ============================================================================
// helpers
// ============================================================================

int createNonblockingSocket() {
  const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          0);
  if (fd < 0) {
    throw std::system_error(errno, std::system_category(), "socket");
  }
  return fd;
}

}  // namespace nexus::net