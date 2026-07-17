#include "nexus/net/buffer.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <sys/uio.h>
#include <unistd.h>

namespace nexus::net {

Buffer::Buffer(std::size_t initial_size)
    : buffer_(std::max(initial_size, kCheapPrepend + 1)),
      reader_index_(kCheapPrepend),
      writer_index_(kCheapPrepend) {}

std::size_t Buffer::readableBytes() const noexcept {
    return writer_index_ - reader_index_;
}

std::size_t Buffer::writableBytes() const noexcept {
    return buffer_.size() - writer_index_;
}

std::size_t Buffer::prependableBytes() const noexcept {
    return reader_index_;
}

const char* Buffer::peek() const noexcept {
    return begin() + reader_index_;
}

char* Buffer::beginWrite() noexcept {
    return begin() + writer_index_;
}

void Buffer::retrieve(std::size_t length) {
    if (length < readableBytes()) {
        reader_index_ += length;
        return;
    }
    retrieveAll();
}

void Buffer::retrieveAll() noexcept {
    reader_index_ = kCheapPrepend;
    writer_index_ = kCheapPrepend;
}

std::string Buffer::retrieveAsString(std::size_t length) {
    const std::size_t actual_length = std::min(length, readableBytes());
    std::string result(peek(), actual_length);
    retrieve(actual_length);
    return result;
}

std::string Buffer::retrieveAllAsString() {
    return retrieveAsString(readableBytes());
}

void Buffer::ensureWritableBytes(std::size_t length) {
    if (writableBytes() < length) {
        makeSpace(length);
    }
}

void Buffer::append(const void* data, std::size_t length) {
    assert(data != nullptr || length == 0);
    if (data == nullptr || length == 0) {
        return;
    }
    ensureWritableBytes(length);
    std::memcpy(beginWrite(), data, length);
    hasWritten(length);
}

void Buffer::append(std::string_view data) {
    append(data.data(), data.size());
}

void Buffer::append(const char* data, std::size_t length) {
    append(static_cast<const void*>(data), length);
}

void Buffer::hasWritten(std::size_t length) {
    assert(length <= writableBytes());
    if (length > writableBytes()) {
        return;
    }
    writer_index_ += length;
}

std::ptrdiff_t Buffer::readFd(int fd, int* saved_errno) {
    if (saved_errno != nullptr) {
        *saved_errno = 0;
    }

    char extra_buffer[65536];
    struct iovec vectors[2];
    const std::size_t writable = writableBytes();

    vectors[0].iov_base = beginWrite();
    vectors[0].iov_len = writable;
    vectors[1].iov_base = extra_buffer;
    vectors[1].iov_len = sizeof(extra_buffer);

    const int vector_count = writable < sizeof(extra_buffer) ? 2 : 1;
    const ssize_t bytes_read = ::readv(fd, vectors, vector_count);
    if (bytes_read < 0) {
        if (saved_errno != nullptr) {
            *saved_errno = errno;
        }
        return bytes_read;
    }

    if (static_cast<std::size_t>(bytes_read) <= writable) {
        writer_index_ += static_cast<std::size_t>(bytes_read);
    } else {
        writer_index_ = buffer_.size();
        append(extra_buffer,
               static_cast<std::size_t>(bytes_read) - writable);
    }
    return bytes_read;
}

char* Buffer::begin() noexcept {
    return buffer_.data();
}

const char* Buffer::begin() const noexcept {
    return buffer_.data();
}

void Buffer::makeSpace(std::size_t length) {
    if (length > std::numeric_limits<std::size_t>::max() - writer_index_) {
        throw std::length_error("buffer size exceeds addressable memory");
    }

    if (writableBytes() + prependableBytes() - kCheapPrepend >= length) {
        const std::size_t readable = readableBytes();
        std::memmove(begin() + kCheapPrepend, peek(), readable);
        reader_index_ = kCheapPrepend;
        writer_index_ = reader_index_ + readable;
        return;
    }

    buffer_.resize(writer_index_ + length);
}

}  // namespace nexus::net
