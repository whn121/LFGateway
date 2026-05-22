#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <functional>
#include <sys/epoll.h>
#include "base/nocopyable.hpp"

class EventLoop;

class Channel : nocopyable {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    void handleEvent();
    void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; }

    void enableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();
    void remove();

    bool isWriting() const { return events_ & EPOLLOUT; }

    int index() const { return index_; }
    void set_index(int idx) { index_ = idx; }

    void markAsRemoved() { removed_ = true; }
    bool isRemoved() const { return removed_; }

    EventLoop* ownerLoop() const { return loop_; }

private:
    void update();

    EventLoop* loop_;
    int fd_;
    int events_;
    int revents_;
    int index_;
    bool removed_ = false;

    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
};

#endif
