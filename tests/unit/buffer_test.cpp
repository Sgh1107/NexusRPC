#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include <unistd.h>

#include "nexus/net/buffer.h"

namespace nexus::net {
namespace {

// ============================================================================
// Construction and basic properties
// ============================================================================

TEST(BufferTest, DefaultConstruction) {
  Buffer buf;
  EXPECT_EQ(buf.readableBytes(), 0U);
  EXPECT_EQ(buf.writableBytes(), 4088U);   // 4096 - 8 prepend
  EXPECT_EQ(buf.prependableBytes(), 8U);
}

TEST(BufferTest, SmallInitialSize) {
  Buffer buf(16);
  EXPECT_EQ(buf.readableBytes(), 0U);
  EXPECT_GE(buf.writableBytes(), 8U);      // at least 16 - 8 prepend
}

// ============================================================================
// Append and retrieve
// ============================================================================

TEST(BufferTest, AppendAndRetrieve) {
  Buffer buf;
  const std::string data = "hello";
  buf.append(data);
  EXPECT_EQ(buf.readableBytes(), 5U);
  EXPECT_EQ(buf.retrieveAllAsString(), data);
  EXPECT_EQ(buf.readableBytes(), 0U);
}

TEST(BufferTest, AppendAndRetrievePartial) {
  Buffer buf;
  buf.append("Hello, World");
  EXPECT_EQ(buf.retrieveAsString(5), "Hello");
  EXPECT_EQ(buf.readableBytes(), 7U);
  EXPECT_EQ(buf.retrieveAllAsString(), ", World");
}

TEST(BufferTest, AppendOverBoundary) {
  Buffer buf(16);
  const std::string data(100, 'x');
  buf.append(data);
  EXPECT_EQ(buf.readableBytes(), 100U);
  EXPECT_EQ(buf.retrieveAllAsString(), data);
}

TEST(BufferTest, AppendMultipleTimes) {
  Buffer buf;
  buf.append("abc");
  buf.append("def");
  EXPECT_EQ(buf.retrieveAllAsString(), "abcdef");
}

TEST(BufferTest, RetrieveEmpty) {
  Buffer buf;
  EXPECT_EQ(buf.retrieveAsString(0), "");
  EXPECT_EQ(buf.retrieveAllAsString(), "");
}

TEST(BufferTest, RetrieveMoreThanAvailable) {
  Buffer buf;
  buf.append("hi");
  EXPECT_EQ(buf.retrieveAsString(100).size(), 2U);
}

// ============================================================================
// Prependable space reclaim
// ============================================================================

TEST(BufferTest, PrependSpaceReclaim) {
  Buffer buf;
  buf.append("Hello");
  EXPECT_EQ(buf.prependableBytes(), 8U);
  EXPECT_EQ(buf.retrieveAllAsString(), "Hello");
  EXPECT_EQ(buf.prependableBytes(), 8U);

  // After retrieve, the data is "consumed" but not yet compacted.
  // New append should still work because ensureWritableBytes compacts.
  buf.append("World");
  EXPECT_EQ(buf.retrieveAllAsString(), "World");
}

// ============================================================================
// peek / beginWrite / hasWritten
// ============================================================================

TEST(BufferTest, PeekAfterAppend) {
  Buffer buf;
  buf.append("test");
  EXPECT_EQ(std::memcmp(buf.peek(), "test", 4), 0);
}

TEST(BufferTest, HasWritten) {
  Buffer buf;
  buf.ensureWritableBytes(10);
  char* pos = buf.beginWrite();
  std::memcpy(pos, "ABCDE", 5);
  buf.hasWritten(5);
  EXPECT_EQ(buf.readableBytes(), 5U);
  EXPECT_EQ(buf.retrieveAllAsString(), "ABCDE");
}

// ============================================================================
// readFd with pipes
// ============================================================================

TEST(BufferTest, ReadFdFromPipe) {
  int fds[2];
  ASSERT_EQ(::pipe(fds), 0);

  Buffer buf;
  const std::string payload(1000, 'a');

  ssize_t written = ::write(fds[1], payload.data(), payload.size());
  ASSERT_EQ(written, static_cast<ssize_t>(payload.size()));

  int saved_errno = 0;
  ssize_t n = buf.readFd(fds[0], &saved_errno);
  EXPECT_EQ(n, static_cast<ssize_t>(payload.size()));
  EXPECT_EQ(saved_errno, 0);
  EXPECT_EQ(buf.readableBytes(), payload.size());
  EXPECT_EQ(buf.retrieveAllAsString(), payload);

  ::close(fds[0]);
  ::close(fds[1]);
}

TEST(BufferTest, ReadFdETLargerThanBuffer) {
  int fds[2];
  ASSERT_EQ(::pipe(fds), 0);

  Buffer buf(64);
  const std::string payload(70000, 'b');  // larger than default extra buffer

  ssize_t written = ::write(fds[1], payload.data(), payload.size());
  ASSERT_EQ(written, static_cast<ssize_t>(payload.size()));

  int saved_errno = 0;
  ssize_t n = buf.readFd(fds[0], &saved_errno);
  EXPECT_GT(n, 0);
  EXPECT_EQ(buf.readableBytes(), static_cast<std::size_t>(n));
  EXPECT_EQ(buf.retrieveAsString(payload.size()).size(), payload.size());

  ::close(fds[0]);
  ::close(fds[1]);
}

// ============================================================================
// ensureWritableBytes / growth
// ============================================================================

TEST(BufferTest, EnsureWritableSpace) {
  Buffer buf;
  buf.ensureWritableBytes(20000);
  EXPECT_GE(buf.writableBytes(), 20000U);
}

// ============================================================================
// makeSpace (internal) ??test via append after consume
// ============================================================================

TEST(BufferTest, MakeSpaceByConsuming) {
  Buffer buf(32);
  buf.append(std::string(20, 'a'));
  EXPECT_EQ(buf.retrieveAllAsString().size(), 20U);

  // Now we have 20 consumed + 8 prepend = 28 prependable bytes.
  // Appending 25 bytes should trigger makeSpace with compaction.
  buf.append(std::string(25, 'b'));
  EXPECT_EQ(buf.readableBytes(), 25U);
  EXPECT_EQ(buf.retrieveAllAsString(), std::string(25, 'b'));
}

}  // namespace
}  // namespace nexus::net
