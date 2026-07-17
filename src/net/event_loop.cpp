#include "nexus/net/event_loop.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "nexus/net/channel.h"

namespace nexus::net {
namespace {

constexpr int kInitialEventListSize = 16;

int createEpollFd() {
    const int fd = ::epoll_create1(EPOLL_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("failed to create epoll fd");
    }
    return fd;
}

int createWakeupFd(int epoll_fd) {
    const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        ::close(epoll_fd);
        throw std::runtime_error("failed to create eventfd");
    }
    return fd;
}

}  // namespace

EventLoop::EventLoop()
    : epoll_fd_(createEpollFd()),
      wakeup_fd_(createWakeupFd(epoll_fd_)),
      looping_(false),
      quit_(false),
      thread_id_(std::this_thread::get_id()),
      active_events_(kInitialEventListSize) {
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = wakeup_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_fd_, &event) < 0) {
        ::close(wakeup_fd_);
        ::close(epoll_fd_);
        throw std::runtime_error("failed to register eventfd with epoll");
    }
}

EventLoop::~EventLoop() {
    assert(!looping_);
    ::close(wakeup_fd_);
    ::close(epoll_fd_);
}

void EventLoop::loop() {
    assertInLoopThread();
    if (looping_.exchange(true)) {
        return;
    }

    quit_ = false;
    try {
        while (!quit_) {
            int event_count;
            do {
                event_count = ::epoll_wait(
                    epoll_fd_, active_events_.data(),
                    static_cast<int>(active_events_.size()), -1);
            } while (event_count < 0 && errno == EINTR);

            if (event_count < 0) {
                throw std::runtime_error("epoll_wait failed");
            }

            for (int index = 0; index < event_count; ++index) {
                const epoll_event& event = active_events_[index];
                if (event.data.fd == wakeup_fd_) {
                    handleWakeupRead();
                    continue;
                }

                const auto channel_iterator = channels_.find(event.data.fd);
                if (channel_iterator == channels_.end()) {
                    continue;
                }

                Channel* channel = channel_iterator->second;
                channel->revents_ = event.events;
                channel->handleEvent();
            }

            if (event_count == static_cast<int>(active_events_.size())) {
                active_events_.resize(active_events_.size() * 2);
            }

            doPendingFunctors();
        }
    } catch (...) {
        looping_ = false;
        throw;
    }

    looping_ = false;
}

void EventLoop::quit() noexcept {
    quit_ = true;
    if (!isInLoopThread()) {
        wakeup();
    }
}

void EventLoop::runInLoop(Functor callback) {
    if (isInLoopThread()) {
        callback();
        return;
    }
    queueInLoop(std::move(callback));
}

void EventLoop::queueInLoop(Functor callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_functors_.push_back(std::move(callback));
    }
    if (!isInLoopThread() || !looping_) {
        wakeup();
    }
}

void EventLoop::updateChannel(Channel* channel) {
    assertInLoopThread();
    assert(channel != nullptr);
    if (channel->isNoneEvent() &&
        (channel->index_ == Channel::kNew ||
         channel->index_ == Channel::kDeleted)) {
        return;
    }

    int operation;
    if (channel->index_ == Channel::kNew ||
        channel->index_ == Channel::kDeleted) {
        operation = EPOLL_CTL_ADD;
    } else if (channel->isNoneEvent()) {
        operation = EPOLL_CTL_DEL;
    } else {
        operation = EPOLL_CTL_MOD;
    }

    epoll_event event{};
    event.events = channel->events_ | EPOLLET;
    event.data.fd = channel->fd_;

    if (::epoll_ctl(epoll_fd_, operation, channel->fd_, &event) < 0) {
        throw std::runtime_error(std::string("epoll_ctl failed: ") +
                                 std::strerror(errno));
    }

    if (operation == EPOLL_CTL_ADD || operation == EPOLL_CTL_MOD) {
        channels_[channel->fd_] = channel;
        channel->index_ = Channel::kAdded;
    } else {
        channels_.erase(channel->fd_);
        channel->index_ = Channel::kDeleted;
    }
}

void EventLoop::removeChannel(Channel* channel) {
    assertInLoopThread();
    assert(channel != nullptr);
    if (channel->index_ == Channel::kAdded) {
        epoll_event event{};
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, channel->fd_, &event) < 0 &&
            errno != ENOENT) {
            throw std::runtime_error(std::string("failed to remove channel: ") +
                                     std::strerror(errno));
        }
    }
    channels_.erase(channel->fd_);
    channel->index_ = Channel::kDeleted;
}

bool EventLoop::isInLoopThread() const noexcept {
    return std::this_thread::get_id() == thread_id_;
}

bool EventLoop::isLooping() const noexcept {
    return looping_;
}

void EventLoop::assertInLoopThread() const {
    if (!isInLoopThread()) {
        throw std::logic_error("EventLoop accessed from the wrong thread");
    }
}

void EventLoop::wakeup() noexcept {
    const std::uint64_t value = 1;
    (void)::write(wakeup_fd_, &value, sizeof(value));
}

void EventLoop::handleWakeupRead() noexcept {
    std::uint64_t value = 0;
    while (::read(wakeup_fd_, &value, sizeof(value)) == sizeof(value)) {
    }
}

void EventLoop::doPendingFunctors() {
    std::vector<Functor> callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks.swap(pending_functors_);
    }

    for (const Functor& callback : callbacks) {
        callback();
    }
}

}  // namespace nexus::net
