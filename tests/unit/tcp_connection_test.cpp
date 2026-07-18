#include <gtest/gtest.h>

#include <sys/socket.h>
#include <unistd.h>

#include "nexus/net/tcp_connection.h"
#include "nexus/net/event_loop.h"
#include "nexus/net/socket.h"

namespace nexus::net {
namespace {

// ============================================================================
// TcpConnection construction tests
// ============================================================================

struct ConnFixture {
  EventLoop loop;
  int fd_a;
  int fd_b;
  std::string name;
  InetAddress local;
  InetAddress remote;

  ConnFixture()
      : fd_a(-1),
        fd_b(-1),
        name("test-conn"),
        local(InetAddress::loopback(9601)),
        remote(InetAddress::loopback(40000)) {
    int fds[2];
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    fd_a = fds[0];
    fd_b = fds[1];
  }

  ~ConnFixture() {
    if (fd_a >= 0) ::close(fd_a);
    if (fd_b >= 0) ::close(fd_b);
  }
};

TEST(TcpConnectionTest, Construction) {
  ConnFixture f;

  auto conn = std::make_shared<TcpConnection>(
      &f.loop, "conn-1", f.fd_a, f.local, f.remote);

  EXPECT_EQ(conn->name(), "conn-1");
  EXPECT_EQ(conn->fd(), f.fd_a);
  EXPECT_EQ(conn->getLoop(), &f.loop);
  EXPECT_EQ(conn->peerAddress().port(), 40000U);
  EXPECT_EQ(conn->localAddress().port(), 9601U);

  // Steal the fd from the fixture so the test doesn't double-close.
  f.fd_a = -1;
}

TEST(TcpConnectionTest, NameFormat) {
  ConnFixture f;

  auto conn = std::make_shared<TcpConnection>(
      &f.loop, "echo:127.0.0.1:40000#1", f.fd_a, f.local, f.remote);

  EXPECT_EQ(conn->name(), "echo:127.0.0.1:40000#1");
  f.fd_a = -1;  // ownership transferred
}

TEST(TcpConnectionTest, CallbackSetters) {
  ConnFixture f;

  auto conn = std::make_shared<TcpConnection>(
      &f.loop, "cb-test", f.fd_a, f.local, f.remote);

  int established_count = 0;
  conn->setConnectionCallback(
      [&](const std::shared_ptr<TcpConnection>&) { ++established_count; });

  int message_count = 0;
  conn->setMessageCallback(
      [&](const std::shared_ptr<TcpConnection>&, Buffer*) { ++message_count; });

  int write_count = 0;
  conn->setWriteCompleteCallback(
      [&](const std::shared_ptr<TcpConnection>&) { ++write_count; });

  // Callbacks are wired but won't fire without a running EventLoop.
  // Construction validation only.
  EXPECT_EQ(established_count, 0);
  EXPECT_EQ(message_count, 0);
  EXPECT_EQ(write_count, 0);

  f.fd_a = -1;
}

}  // namespace
}  // namespace nexus::net
