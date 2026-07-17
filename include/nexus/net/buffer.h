#ifndef NEXUS_NET_BUFFER_H_
#define NEXUS_NET_BUFFER_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::net {

/**
 * A dynamically growing byte buffer for non-blocking network IO.
 *
 * The buffer uses a prependable/readable/writable layout. Consumed bytes are
 * reclaimed before growing the underlying storage.
 */
class Buffer {
public:
    explicit Buffer(std::size_t initial_size = 4096);

    std::size_t readableBytes() const noexcept;
    std::size_t writableBytes() const noexcept;
    std::size_t prependableBytes() const noexcept;

    const char* peek() const noexcept;
    char* beginWrite() noexcept;

    void retrieve(std::size_t length);
    void retrieveAll() noexcept;
    std::string retrieveAsString(std::size_t length);
    std::string retrieveAllAsString();

    void ensureWritableBytes(std::size_t length);
    void append(const void* data, std::size_t length);
    void append(std::string_view data);
    void append(const char* data, std::size_t length);

    void hasWritten(std::size_t length);

    /**
     * Reads from a non-blocking file descriptor into the buffer.
     *
     * The implementation uses a stack buffer as overflow space so one read
     * event can consume all currently available data without an extra grow.
     * `saved_errno` receives the errno value when read returns -1.
     */
    std::ptrdiff_t readFd(int fd, int* saved_errno);

private:
    static constexpr std::size_t kCheapPrepend = 8;

    char* begin() noexcept;
    const char* begin() const noexcept;
    void makeSpace(std::size_t length);

    std::vector<char> buffer_;
    std::size_t reader_index_;
    std::size_t writer_index_;
};

}  // namespace nexus::net

#endif  // NEXUS_NET_BUFFER_H_
