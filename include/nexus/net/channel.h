#ifndef NEXUS_NET_CHANNEL_H_
#define NEXUS_NET_CHANNEL_H_

#include <cstdint>
#include <functional>

namespace nexus::net {

class EventLoop;

/**
 * Binds a file descriptor to callbacks managed by an EventLoop.
 *
 * Channel does not own the file descriptor. The owner must keep the descriptor
 * alive until the channel is removed from its EventLoop.
 */
class Channel {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd);
    ~Channel() = default;

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    void handleEvent();

    void setReadCallback(EventCallback callback);
    void setWriteCallback(EventCallback callback);
    void setCloseCallback(EventCallback callback);
    void setErrorCallback(EventCallback callback);

    void enableReading();
    void disableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();
    void remove();

    int fd() const noexcept;
    std::uint32_t events() const noexcept;
    bool isNoneEvent() const noexcept;

private:
    friend class EventLoop;

    static constexpr int kNew = -1;
    static constexpr int kAdded = 1;
    static constexpr int kDeleted = 2;

    void update();

    EventLoop* loop_;
    const int fd_;
    std::uint32_t events_;
    std::uint32_t revents_;
    int index_;

    EventCallback read_callback_;
    EventCallback write_callback_;
    EventCallback close_callback_;
    EventCallback error_callback_;
};

}  // namespace nexus::net

#endif  // NEXUS_NET_CHANNEL_H_
