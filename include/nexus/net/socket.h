#ifndef NEXUS_NET_SOCKET_H_
#define NEXUS_NET_SOCKET_H_

#include <cstdint>
#include <string>
#include <string_view>

#include <netinet/in.h>

namespace nexus::net {

/**
 * Wraps a IPv4 sockaddr_in.
 *
 * Provides conversion to/from dotted-quad IP strings and "ip:port"
 * notation. This class is used by Acceptor to record peer and local
 * addresses for established connections.
 */
class InetAddress {
 public:
  /// Constructs an address that binds any interface on @p port.
  explicit InetAddress(uint16_t port);

  /// Constructs a loopback address on @p port.
  static InetAddress loopback(uint16_t port);

  /// Constructs an address from an IP string and port.
  InetAddress(std::string_view ip, uint16_t port);

  /// Wraps a raw sockaddr_in (used internally by Socket::accept).
  explicit InetAddress(const struct sockaddr_in& addr);

  /// Returns the underlying sockaddr_in.
  const struct sockaddr_in& sockAddr() const noexcept;

  /// Returns the IP address as a dotted-quad string.
  std::string toIp() const;

  /// Returns "ip:port".
  std::string toIpPort() const;

  /// Returns the port in host byte order.
  uint16_t port() const noexcept;

 private:
  struct sockaddr_in addr_;
};

// ---------------------------------------------------------------------------
// Socket
// ---------------------------------------------------------------------------

/**
 * RAII wrapper around a POSIX socket descriptor.
 *
 * The Socket owns the fd. When the Socket is destroyed the fd is closed.
 * Socket is move-only.
 */
class Socket {
 public:
  /// Takes ownership of @p fd.
  explicit Socket(int fd);

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;

  ~Socket();

  /// Returns the wrapped file descriptor.
  int fd() const noexcept;

  // ---- options -----------------------------------------------------------

  void setReuseAddr(bool on) const;
  void setReusePort(bool on) const;
  void setTcpNoDelay(bool on) const;
  void setKeepAlive(bool on) const;

  // ---- lifecycle ---------------------------------------------------------

  void bind(const InetAddress& local) const;
  void listen() const;

  /**
   * Accepts one new connection.
   *
   * @param[out] peer_addr  Filled with the peer's address on success.
   * @return The connected fd, or -1 on error (errno is set).
   */
  int accept(InetAddress* peer_addr) const;

 private:
  int fd_;
};

/**
 * Creates a non-blocking SOCK_STREAM socket with CLOEXEC set.
 *
 * @throws std::system_error if socket(2) fails.
 */
int createNonblockingSocket();

}  // namespace nexus::net

#endif  // NEXUS_NET_SOCKET_H_
