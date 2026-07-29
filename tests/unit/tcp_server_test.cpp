#include <gtest/gtest.h>

#include "nexus/net/tcp_server.h"
#include "nexus/net/event_loop.h"

namespace nexus::net {
namespace {

// ============================================================================
// TcpServer construction and configuration tests (no running event loop)
// ============================================================================

TEST(TcpServerTest, Construction) {
  EventLoop loop;
  TcpServer server(&loop, InetAddress(9600), "TestServer");

  EXPECT_EQ(server.name(), "TestServer");
  EXPECT_EQ(server.connectionCount(), 0);
}

TEST(TcpServerTest, SetThreadNum) {
  EventLoop loop;
  TcpServer server(&loop, InetAddress(9601), "ThreadNumTest");

  server.setThreadNum(8);
  // No easy way to read thread count back; verify no crash.
}

TEST(TcpServerTest, SetCallbacks) {
  EventLoop loop;
  TcpServer server(&loop, InetAddress(9603), "CallbackTest");

  bool conn_called = false;
  server.setConnectionCallback(
      [&](const std::shared_ptr<TcpConnection>&) { conn_called = true; });

  bool msg_called = false;
  server.setMessageCallback(
      [&](const std::shared_ptr<TcpConnection>&, Buffer*) { msg_called = true; });

  // Callbacks stored but won't fire without start() + running loop.
  EXPECT_FALSE(conn_called);
  EXPECT_FALSE(msg_called);
}

TEST(TcpServerTest, TwoServersDifferentPorts) {
  EventLoop loop;
  TcpServer s1(&loop, InetAddress(9610), "ServerA");
  TcpServer s2(&loop, InetAddress(9611), "ServerB");

  EXPECT_EQ(s1.name(), "ServerA");
  EXPECT_EQ(s2.name(), "ServerB");
  EXPECT_EQ(s1.connectionCount(), 0);
  EXPECT_EQ(s2.connectionCount(), 0);
}

}  // namespace
}  // namespace nexus::net
