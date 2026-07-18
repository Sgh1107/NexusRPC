#include <gtest/gtest.h>

#include <sys/socket.h>
#include <unistd.h>

#include "nexus/net/channel.h"
#include "nexus/net/event_loop.h"

namespace nexus::net {
namespace {

// ============================================================================
// Channel construction & state queries
// ============================================================================

/// Returns a pair of connected sockets used as test fds.
struct FdPair {
  int first;
  int second;
};

static FdPair makeSocketPair() {
  int fds[2];
  EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  return {fds[0], fds[1]};
}

TEST(ChannelTest, Construction) {
  EventLoop loop;
  auto [fd, peer] = makeSocketPair();

  Channel ch(&loop, fd);
  EXPECT_EQ(ch.fd(), fd);
  EXPECT_TRUE(ch.isNoneEvent());
  EXPECT_FALSE(ch.isWriting());
  EXPECT_EQ(ch.events(), 0U);

  ::close(fd);
  ::close(peer);
}

TEST(ChannelTest, EnableWriting) {
  EventLoop loop;
  auto [fd, peer] = makeSocketPair();

  Channel ch(&loop, fd);
  EXPECT_FALSE(ch.isWriting());

  ch.enableWriting();
  EXPECT_TRUE(ch.isWriting());
  EXPECT_FALSE(ch.isNoneEvent());
  EXPECT_NE(ch.events() & EPOLLOUT, 0U);

  ::close(fd);
  ::close(peer);
}

TEST(ChannelTest, DisableWriting) {
  EventLoop loop;
  auto [fd, peer] = makeSocketPair();

  Channel ch(&loop, fd);
  ch.enableWriting();
  EXPECT_TRUE(ch.isWriting());

  ch.disableWriting();
  EXPECT_FALSE(ch.isWriting());

  ::close(fd);
  ::close(peer);
}

TEST(ChannelTest, EnableReading) {
  EventLoop loop;
  auto [fd, peer] = makeSocketPair();

  Channel ch(&loop, fd);
  ch.enableReading();
  EXPECT_FALSE(ch.isNoneEvent());
  EXPECT_NE(ch.events() & (EPOLLIN), 0U);

  ::close(fd);
  ::close(peer);
}

TEST(ChannelTest, DisableAll) {
  EventLoop loop;
  auto [fd, peer] = makeSocketPair();

  Channel ch(&loop, fd);
  ch.enableReading();
  ch.enableWriting();
  EXPECT_FALSE(ch.isNoneEvent());

  ch.disableAll();
  EXPECT_TRUE(ch.isNoneEvent());
  EXPECT_FALSE(ch.isWriting());

  ::close(fd);
  ::close(peer);
}

TEST(ChannelTest, DisableAllFromNoneEvent) {
  EventLoop loop;
  auto [fd, peer] = makeSocketPair();

  // disableAll on a Channel that was never enabled should be a no-op.
  Channel ch(&loop, fd);
  EXPECT_TRUE(ch.isNoneEvent());
  EXPECT_NO_THROW(ch.disableAll());
  EXPECT_TRUE(ch.isNoneEvent());

  ::close(fd);
  ::close(peer);
}

}  // namespace
}  // namespace nexus::net
