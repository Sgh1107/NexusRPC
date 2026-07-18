#include "nexus/net/channel.h"

#include <cassert>
#include <sys/epoll.h>
#include <utility>

#include "nexus/net/event_loop.h"

namespace nexus::net {

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop),
      fd_(fd),
      events_(0),
      revents_(0),
      index_(kNew) {
    assert(loop_ != nullptr);
    assert(fd_ >= 0);
}

void Channel::handleEvent() {
    if ((revents_ & EPOLLHUP) != 0 && (revents_ & EPOLLIN) == 0) {
        if (close_callback_) {
            close_callback_();
        }
        return;
    }

    if ((revents_ & EPOLLERR) != 0) {
        if (error_callback_) {
            error_callback_();
        }
    }

    if ((revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) != 0) {
        if (read_callback_) {
            read_callback_();
        }
    }

    if ((revents_ & EPOLLOUT) != 0) {
        if (write_callback_) {
            write_callback_();
        }
    }
}

void Channel::setReadCallback(EventCallback callback) {
    read_callback_ = std::move(callback);
}

void Channel::setWriteCallback(EventCallback callback) {
    write_callback_ = std::move(callback);
}

void Channel::setCloseCallback(EventCallback callback) {
    close_callback_ = std::move(callback);
}

void Channel::setErrorCallback(EventCallback callback) {
    error_callback_ = std::move(callback);
}

void Channel::enableReading() {
    events_ |= EPOLLIN | EPOLLPRI | EPOLLRDHUP;
    update();
}

void Channel::disableReading() {
    events_ &= ~(EPOLLIN | EPOLLPRI | EPOLLRDHUP);
    update();
}

void Channel::enableWriting() {
    events_ |= EPOLLOUT;
    update();
}

void Channel::disableWriting() {
    events_ &= ~EPOLLOUT;
    update();
}

void Channel::disableAll() {
    events_ = 0;
    update();
}

void Channel::remove() {
    loop_->removeChannel(this);
}

int Channel::fd() const noexcept {
    return fd_;
}

std::uint32_t Channel::events() const noexcept {
    return events_;
}

bool Channel::isNoneEvent() const noexcept {
    return events_ == 0;
}

bool Channel::isWriting() const noexcept {
    return (events_ & EPOLLOUT) != 0;
}

void Channel::update() {
    loop_->updateChannel(this);
}

}  // namespace nexus::net
