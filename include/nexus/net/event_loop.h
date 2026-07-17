#ifndef NEXUS_NET_EVENT_LOOP_H_
#define NEXUS_NET_EVENT_LOOP_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__linux__)
#include <sys/epoll.h>
#endif

namespace nexus::net {

class Channel;

/**
 * Runs one Linux epoll event loop and owns the cross-thread wakeup mechanism.
 *
 * An EventLoop must be constructed and run by the same thread. Channel state
 * changes are serialized through this loop; callers from other threads must
 * use runInLoop or queueInLoop.
 */
class EventLoop {
public:
    using Functor = std::function<void()>;

    EventLoop();
    /**
     * Destroys the loop after `loop()` has returned.
     *
     * The owner must stop the loop and join its thread before destruction.
     */
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void loop();
    void quit() noexcept;

    void runInLoop(Functor callback);
    void queueInLoop(Functor callback);

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

    bool isInLoopThread() const noexcept;
    bool isLooping() const noexcept;

private:
    void assertInLoopThread() const;
    void wakeup() noexcept;
    void handleWakeupRead() noexcept;
    void doPendingFunctors();

    int epoll_fd_;
    int wakeup_fd_;
    std::atomic<bool> looping_;
    std::atomic<bool> quit_;
    const std::thread::id thread_id_;

#if defined(__linux__)
    std::vector<epoll_event> active_events_;
#else
    std::vector<std::uint64_t> active_events_;
#endif
    std::unordered_map<int, Channel*> channels_;

    std::mutex mutex_;
    std::vector<Functor> pending_functors_;
};

}  // namespace nexus::net

#endif  // NEXUS_NET_EVENT_LOOP_H_
