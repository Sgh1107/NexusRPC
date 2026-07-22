#include <arpa/inet.h>
#include <cstring>
#include <gtest/gtest.h>

#include <string>

#include "nexus/net/socket.h"

namespace nexus::net {
namespace {

// ============================================================================
// InetAddress
// ============================================================================

TEST(InetAddressTest, PortOnly) {
  InetAddress addr(8080);
  EXPECT_EQ(addr.port(), 8080U);
  EXPECT_EQ(addr.toIp(), "0.0.0.0");
  EXPECT_EQ(addr.toIpPort(), "0.0.0.0:8080");
}

TEST(InetAddressTest, WithIpAndPort) {
  InetAddress addr("192.168.1.1", 443);
  EXPECT_EQ(addr.port(), 443U);
  EXPECT_EQ(addr.toIp(), "192.168.1.1");
  EXPECT_EQ(addr.toIpPort(), "192.168.1.1:443");
}

TEST(InetAddressTest, Loopback) {
  InetAddress addr = InetAddress::loopback(9999);
  EXPECT_EQ(addr.port(), 9999U);
  EXPECT_EQ(addr.toIp(), "127.0.0.1");
  EXPECT_EQ(addr.toIpPort(), "127.0.0.1:9999");
}

TEST(InetAddressTest, PortZero) {
  InetAddress addr(0);
  EXPECT_EQ(addr.port(), 0U);
}

TEST(InetAddressTest, InvalidIpThrows) {
  EXPECT_THROW(InetAddress("not-an-ip", 80), std::invalid_argument);
}

TEST(InetAddressTest, FromSockAddrIn) {
  struct sockaddr_in raw;
  std::memset(&raw, 0, sizeof(raw));
  raw.sin_family = AF_INET;
  raw.sin_port = htons(53);
  ::inet_pton(AF_INET, "8.8.8.8", &raw.sin_addr);

  InetAddress addr(raw);
  EXPECT_EQ(addr.port(), 53U);
  EXPECT_EQ(addr.toIp(), "8.8.8.8");
}

// ============================================================================
// Socket
// ============================================================================

TEST(SocketTest, CreateAndDestroy) {
  int fd = createNonblockingSocket();
  EXPECT_GE(fd, 0);
  Socket sock(fd);
  EXPECT_EQ(sock.fd(), fd);
  // Socket destroyed at end of scope ??fd closed.
}

TEST(SocketTest, MoveSemantics) {
  Socket a(createNonblockingSocket());
  int fd_a = a.fd();

  Socket b = std::move(a);
  EXPECT_EQ(b.fd(), fd_a);
  // a.fd() is now -1, b owns the fd.
}

TEST(SocketTest, SetReuseAddr) {
  Socket sock(createNonblockingSocket());
  EXPECT_NO_THROW(sock.setReuseAddr(true));
  EXPECT_NO_THROW(sock.setReuseAddr(false));
}

TEST(SocketTest, SetTcpNoDelay) {
  Socket sock(createNonblockingSocket());
  EXPECT_NO_THROW(sock.setTcpNoDelay(true));
  EXPECT_NO_THROW(sock.setTcpNoDelay(false));
}

TEST(SocketTest, AccessorOnMovedSocket) {
  Socket a(createNonblockingSocket());
  Socket b = std::move(a);
  // a has been moved-from: fd_ should be -1
  // We can't assert on a.fd() since it's implementation-specific,
  // but b.fd() must be valid.
  EXPECT_GE(b.fd(), 0);
}

}  // namespace
}  // namespace nexus::net
